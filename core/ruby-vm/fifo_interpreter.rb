#!/usr/bin/env ruby
# fifo_interpreter.rb
#
# FIFO-based Ruby script interpreter for the embedded Ruby VM.
# VMLogger, exit interception (Kernel.exit/exit!, Process.exit/exit!)
# and $__safe_runner_exit_code are provided by safe_runner.rb,
# which is loaded from memory before this script runs.
#
# Usage: ruby fifo_interpreter.rb <socket_fd> <ruby_stdout_fd> <ruby_stderr_fd> <vmlogger_fd>
#
# Protocol:
# 1. C side sends: "<length>\n<script_content>"
# 2. Ruby side executes the full script
# 3. Ruby side responds: "<exit_code>\n"

begin
  # Get file descriptors from command-line arguments
  if ARGV.length < 4
    raise ArgumentError, "Usage: #{$0} <socket_fd> <ruby_stdout_fd> <ruby_stderr_fd> <vmlogger_fd>"
  end

  socket_fd = ARGV[0].to_i
  ruby_stdout_fd = ARGV[1].to_i
  ruby_stderr_fd = ARGV[2].to_i
  vmlogger_fd = ARGV[3].to_i

  if socket_fd <= 0
    raise ArgumentError, "Invalid socket file descriptor: #{ARGV[0]}"
  end

  if ruby_stdout_fd <= 0 || ruby_stderr_fd <= 0 || vmlogger_fd <= 0
    raise ArgumentError, "Invalid log file descriptors: stdout=#{ARGV[1]} stderr=#{ARGV[2]} vmlogger=#{ARGV[3]}"
  end

  # Wrap the socket file descriptor in an IO object (bidirectional)
  socket = IO.for_fd(socket_fd, "r+")
  socket.sync = true  # Disable buffering - critical for real-time communication!

  # Redirect Ruby's stdout and stderr to dedicated log streams
  # We need to use IO.reopen to actually replace the underlying file descriptors
  # This ensures both Ruby-level ($stdout) and C-level (STDOUT_FILENO) use the same FD
  ruby_stdout_io = IO.for_fd(ruby_stdout_fd, "w")
  ruby_stdout_io.sync = true
  $stdout.reopen(ruby_stdout_io)

  ruby_stderr_io = IO.for_fd(ruby_stderr_fd, "w")
  ruby_stderr_io.sync = true
  $stderr.reopen(ruby_stderr_io)

  # Redirect VMLogger output to its dedicated FD
  vmlogger_io = IO.for_fd(vmlogger_fd, "w")
  VMLogger.set_output(vmlogger_io)

  # Log startup (useful for debugging)
  VMLogger.info "[Ruby VM] FIFO interpreter started on socket_fd=#{socket_fd}"
  VMLogger.info "[Ruby VM] Log streams: ruby_stdout=#{ruby_stdout_fd}, ruby_stderr=#{ruby_stderr_fd}, vmlogger=#{vmlogger_fd}"

  # Main REPL loop
  loop do
    VMLogger.debug "[Ruby VM] Waiting for script length prefix..."

    # Read the length prefix (format: "<bytes>\n")
    length_line = socket.gets

    # EOF means the C side closed the socket - time to exit
    if length_line.nil?
      VMLogger.info "[Ruby VM] Socket closed by peer, shutting down"
      break
    end

    length_str = length_line.strip

    # Skip empty lines
    next if length_str.empty?

    # Parse the length
    begin
      script_length = Integer(length_str)
    rescue ArgumentError
      VMLogger.error "[Ruby Error] Invalid length prefix: '#{length_str}'"
      socket.write("1\n")
      socket.flush
      next
    end

    # Validate length
    if script_length <= 0 || script_length > 10_000_000  # 10MB max
      VMLogger.error "[Ruby Error] Invalid script length: #{script_length}"
      socket.write("1\n")
      socket.flush
      next
    end

    # Read exactly script_length bytes
    script_content = socket.read(script_length)

    if script_content.nil? || script_content.bytesize != script_length
      VMLogger.error "[Ruby Error] Failed to read complete script (expected #{script_length} bytes)"
      socket.write("1\n")
      socket.flush
      next
    end

    VMLogger.debug "[Ruby VM] Executing script (#{script_length} bytes)"

    # Execute the Ruby script
    $__safe_runner_exit_code = nil
    begin
      # Use TOPLEVEL_BINDING so code has access to top-level context
      result = eval(script_content, TOPLEVEL_BINDING, "<socket-script>")

      # Send sentinel to VM log to signal all logs have been flushed
      # This allows synchronous script execution to wait for log completion
      $stdout.puts "<<<LOGS_FLUSHED>>>"

      # Flush stdout and stderr to ensure all output is visible
      # Ruby buffers stdout when not connected to a TTY, so we flush explicitly
      $stdout.flush
      $stderr.flush

      # Check if exit/exit! was called but caught internally by the script
      if $__safe_runner_exit_code && $__safe_runner_exit_code != 0
        VMLogger.error "[Ruby VM] Script called exit(#{$__safe_runner_exit_code}) but caught it internally"
        socket.write("#{$__safe_runner_exit_code}\n")
      else
        socket.write("0\n")
        VMLogger.debug "[Ruby VM] Script executed successfully"
      end

    rescue Exception => error
      # Log the error to stderr (visible in logcat on Android)
      VMLogger.error "[Ruby Error] #{error.class}: #{error.message}"
      error.backtrace.each { |line| VMLogger.error "  #{line}" }

      # Send sentinel to VM log to signal all logs have been flushed
      $stdout.puts "<<<LOGS_FLUSHED>>>"

      # Flush stdout and stderr to ensure all error output is visible
      $stdout.flush
      $stderr.flush

      # Send failure exit code
      exit_code = ($__safe_runner_exit_code && $__safe_runner_exit_code != 0) ? $__safe_runner_exit_code : 1
      socket.write("#{exit_code}\n")
    end

    # Flush to ensure exit code is sent immediately
    socket.flush
  end

  # Clean shutdown
  socket.close
  VMLogger.info "[Ruby VM] Shutdown complete"

rescue ArgumentError => error
  VMLogger.error "[Ruby VM Fatal] #{error.message}"
  exit(1)

rescue => error
  # Catch any other unexpected errors
  VMLogger.error "[Ruby VM Fatal] #{error.class}: #{error.message}"
  error.backtrace.each { |line| VMLogger.error "  #{line}" }
  exit(1)
end
