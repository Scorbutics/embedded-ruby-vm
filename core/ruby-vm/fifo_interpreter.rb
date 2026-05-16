#!/usr/bin/env ruby
# fifo_interpreter.rb
#
# FIFO-based Ruby script dispatcher for the embedded Ruby VM.
# VMLogger, exit interception (Kernel.exit/exit!, Process.exit/exit!)
# and $__safe_runner_exit_code are provided by safe_runner.rb, which is
# loaded from memory before this script runs.
#
# Usage: ruby fifo_interpreter.rb <socket_fd> <ruby_stdout_fd> <ruby_stderr_fd> <vmlogger_fd>
#
# Wire protocol:
#   Request  (C -> Ruby):  "<interp_id>\n<length>\n<script_content>"
#                          where length is the byte-size of script_content.
#   Response (Ruby -> C):  "<interp_id>\n<exit_code>\n"
#
# Architecture:
#   - The MAIN dispatcher Thread (this script's top-level) reads requests
#     off the socket, looks up (or lazily creates) the worker Thread for
#     the requested interpreter_id, and pushes the script body into that
#     worker's SizedQueue.
#   - Each WORKER Thread is single-threaded: it pops scripts from its
#     queue and `eval`s them one at a time, then writes a tagged response
#     back over the shared socket under a mutex so concurrent workers
#     don't interleave bytes.
#   - This preserves the "scripts from the same interpreter run
#     sequentially" invariant the Kotlin PsdkInterpreter relies on, while
#     allowing long-running scripts (e.g. a render loop) on one
#     interpreter to coexist with sync scripts on another.
#
# Stop sentinel:
#   The C side sends a script whose body equals the constant defined
#   below to ask a specific worker to exit its loop cleanly. The worker
#   replies with exit code 0 and dies. A subsequent request to the same
#   interpreter_id lazily spawns a fresh worker.

STOP_SENTINEL_SCRIPT = "__RGSS_STOP_INTERPRETER__".freeze

begin
  if ARGV.length < 4
    raise ArgumentError, "Usage: #{$0} <socket_fd> <ruby_stdout_fd> <ruby_stderr_fd> <vmlogger_fd>"
  end

  socket_fd      = ARGV[0].to_i
  ruby_stdout_fd = ARGV[1].to_i
  ruby_stderr_fd = ARGV[2].to_i
  vmlogger_fd    = ARGV[3].to_i

  raise ArgumentError, "Invalid socket file descriptor: #{ARGV[0]}" if socket_fd <= 0
  if ruby_stdout_fd <= 0 || ruby_stderr_fd <= 0 || vmlogger_fd <= 0
    raise ArgumentError, "Invalid log file descriptors: stdout=#{ARGV[1]} stderr=#{ARGV[2]} vmlogger=#{ARGV[3]}"
  end

  socket = IO.for_fd(socket_fd, "r+")
  socket.sync = true  # critical: real-time, no buffering on either direction

  # See the long-form comment in the previous version: $stdout / $stderr are
  # assigned (not IO#reopen'd) so the host can keep RUBY-stream output
  # separable from NATIVE-stream output by FD identity.
  $stdout = IO.for_fd(ruby_stdout_fd, "w")
  $stdout.sync = true
  $stderr = IO.for_fd(ruby_stderr_fd, "w")
  $stderr.sync = true

  vmlogger_io = IO.for_fd(vmlogger_fd, "w")
  VMLogger.set_output(vmlogger_io)

  VMLogger.info "[Ruby VM] FIFO dispatcher started on socket_fd=#{socket_fd}"
  VMLogger.info "[Ruby VM] Log streams: ruby_stdout=#{ruby_stdout_fd}, ruby_stderr=#{ruby_stderr_fd}, vmlogger=#{vmlogger_fd}"

  # Mutex protecting socket writes — multiple worker Threads call
  # write_response concurrently, and their <id>\n<exit_code>\n frames must
  # not interleave with each other (the C-side reader thread parses by
  # line and would mis-tag bytes if a write straddled).
  socket_write_mutex = Mutex.new
  write_response = lambda do |interp_id, exit_code|
    begin
      socket_write_mutex.synchronize do
        socket.write("#{interp_id}\n#{exit_code}\n")
        socket.flush
      end
    rescue IOError, Errno::EPIPE, Errno::EBADF
      # The C side has closed the channel. Nothing to do — the dispatcher
      # is about to see EOF on its own gets() and shut us all down.
    end
  end

  # Per-interpreter bookkeeping. The dispatcher is the sole writer here
  # (it spawns workers on first request and removes entries on stop), so
  # no lock is required. Each value is a hash:
  #   { queue: SizedQueue, thread: Thread }
  workers = {}

  start_worker = lambda do |interp_id|
    queue = SizedQueue.new(64)
    thread = Thread.new do
      Thread.current.name = "rgss-worker-#{interp_id}"
      loop do
        script = queue.pop

        if script.equal?(:stop)
          # Stop sentinel reached this worker. Drain has finished — every
          # eval-style script queued before the :stop has run. Emit the
          # response (exit_code 0) and exit. The C-side waiter on
          # ruby_interpreter_destroy is parked on exactly this response.
          VMLogger.protocol "<<<LOGS_FLUSHED>>>"
          $stdout.flush
          $stderr.flush
          write_response.call(interp_id, 0)
          break
        end

        $__safe_runner_exit_code = nil
        exit_code = 0
        begin
          eval(script, TOPLEVEL_BINDING, "<socket-script-#{interp_id}>")
          if $__safe_runner_exit_code && $__safe_runner_exit_code != 0
            VMLogger.error "[Ruby VM] interp=#{interp_id} called exit(#{$__safe_runner_exit_code}) but caught it internally"
            exit_code = $__safe_runner_exit_code
          end
        rescue Exception => error
          VMLogger.error "[Ruby Error] interp=#{interp_id} #{error.class}: #{error.message}"
          error.backtrace&.each { |line| VMLogger.error "  #{line}" }
          exit_code = ($__safe_runner_exit_code && $__safe_runner_exit_code != 0) ? $__safe_runner_exit_code : 1
        end

        VMLogger.protocol "<<<LOGS_FLUSHED>>>"
        $stdout.flush
        $stderr.flush

        write_response.call(interp_id, exit_code)
      end
      VMLogger.debug "[Ruby VM] worker exiting (interp=#{interp_id})"
    end
    { queue: queue, thread: thread }
  end

  loop do
    id_line = socket.gets
    if id_line.nil?
      VMLogger.info "[Ruby VM] Socket closed by peer, shutting down dispatcher"
      break
    end
    id_str = id_line.strip
    next if id_str.empty?

    interpreter_id = begin
      Integer(id_str)
    rescue ArgumentError
      VMLogger.error "[Ruby Error] Invalid interpreter id: '#{id_str}'"
      # We cannot tag a response without a valid id, so the C side has to
      # treat this as a protocol fatal. Drop the rest of the frame so we
      # don't desync — read the length line and discard the body.
      length_line = socket.gets
      next if length_line.nil?
      begin
        n = Integer(length_line.strip)
        socket.read(n) if n.positive?
      rescue StandardError
      end
      next
    end

    length_line = socket.gets
    if length_line.nil?
      VMLogger.info "[Ruby VM] EOF after id #{interpreter_id}, shutting down"
      break
    end

    script_length = begin
      Integer(length_line.strip)
    rescue ArgumentError
      VMLogger.error "[Ruby Error] interp=#{interpreter_id} invalid length: '#{length_line.strip}'"
      write_response.call(interpreter_id, 1)
      next
    end

    if script_length <= 0 || script_length > 10_000_000
      VMLogger.error "[Ruby Error] interp=#{interpreter_id} invalid length: #{script_length}"
      write_response.call(interpreter_id, 1)
      next
    end

    script_content = socket.read(script_length)
    if script_content.nil? || script_content.bytesize != script_length
      VMLogger.error "[Ruby Error] interp=#{interpreter_id} short read (expected #{script_length} bytes)"
      write_response.call(interpreter_id, 1)
      next
    end

    # Stop sentinel: drain the matching worker's queue, then let it write
    # exit_code=0 itself once it pops :stop. We immediately remove the
    # entry from `workers` so a subsequent (illegal) request for the same
    # id would spawn a fresh worker rather than feed the dying one.
    if script_content == STOP_SENTINEL_SCRIPT
      entry = workers.delete(interpreter_id)
      if entry.nil?
        # Worker never spawned for this id (no scripts were ever
        # submitted). Reply on the dispatcher Thread directly.
        write_response.call(interpreter_id, 0)
      else
        entry[:queue] << :stop
      end
      next
    end

    entry = (workers[interpreter_id] ||= start_worker.call(interpreter_id))
    entry[:queue] << script_content
  end

  # Dispatcher exit path: politely ask every live worker to stop and join
  # them with a generous timeout so the Ruby VM thread doesn't return out
  # of ExecMainRubyVM before all eval frames have unwound. Each worker
  # will write its own final response as it exits, but the C side may
  # already be gone (channel closed) — write_response will EPIPE and is
  # caught by the rescue blocks in the worker; that's fine.
  VMLogger.info "[Ruby VM] Dispatcher draining #{workers.size} worker(s)" rescue nil
  workers.each_value { |entry| entry[:queue] << :stop rescue nil }
  workers.each_value { |entry| entry[:thread].join(2) rescue nil }

  socket.close rescue nil
  VMLogger.info "[Ruby VM] Shutdown complete" rescue nil

rescue ArgumentError => error
  VMLogger.error "[Ruby VM Fatal] #{error.message}" rescue nil
  exit(1)

rescue => error
  VMLogger.error "[Ruby VM Fatal] #{error.class}: #{error.message}" rescue nil
  error.backtrace&.each { |line| VMLogger.error "  #{line}" rescue nil }
  exit(1)
end
