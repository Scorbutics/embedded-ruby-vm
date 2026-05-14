# remote_eval.rb
#
# Tiny line-eval console for the embedded Ruby VM, served over a TCP socket.
# Sibling to the `debug` gem's DAP listener — same eager-boot/cookie-auth
# lifecycle, different protocol (plain text, nc-friendly, no Reline).
#
# Loaded from memory by the C runtime (rb_eval_string) when
# ruby_vm_enable_remote_eval was called on the VM. Started via
# `RemoteEval.start(host:, port:, token:)` immediately after load.
#
# Wire protocol (after TCP accept):
#
#   S->C: rubyvm-eval/1 token?\n
#   C->S: <token>\n
#   S->C: OK\n                       (or close on mismatch)
#   S->C: rubyvm[TOPLEVEL]>          (prompt, no trailing newline)
#   C->S: 1 + 1\n
#   S->C: => 2\n
#   S->C: rubyvm[TOPLEVEL]>
#
# Special commands (parsed BEFORE eval):
#   attach <name>    switch scope to a binding registered via RemoteEval.expose
#   detach           back to TOPLEVEL_BINDING
#   scopes           list registered scope names
#   help             short reminder of these commands
#   exit / quit      close the session
#   .                on its own line, end of multi-line input
#
# Multi-line: if `Ripper.sexp(buf)` returns nil, the buffer is syntactically
# incomplete and we keep reading lines until it parses or the user sends `.`.

require 'socket'
require 'stringio'
require 'ripper'

module RemoteEval
  PROTOCOL_GREETING = "rubyvm-eval/1 token?"

  @session_mutex   = Mutex.new          # at most one active session at a time
  @bindings_mutex  = Mutex.new
  @bindings        = {}                 # name(Symbol) => Binding
  @listener_thread = nil
  @started_lock    = Mutex.new

  class << self
    # Script-side API: register a Binding under a name so a remote-console user
    # can `attach <name>` to eval against that scope. Intended use is from
    # inside a long-running script:
    #
    #   class MyScene
    #     def initialize
    #       RemoteEval.expose(:scene, binding) if defined?(RemoteEval)
    #     end
    #   end
    def expose(name, binding_obj)
      raise ArgumentError, "name must respond to to_sym" unless name.respond_to?(:to_sym)
      raise ArgumentError, "expected a Binding, got #{binding_obj.class}" unless binding_obj.is_a?(Binding)
      @bindings_mutex.synchronize { @bindings[name.to_sym] = binding_obj }
      nil
    end

    def unexpose(name)
      @bindings_mutex.synchronize { @bindings.delete(name.to_sym) }
      nil
    end

    # Snapshot of currently registered scope names. Lookups in the eval loop
    # take their own lock — this is for the `scopes` command and tests.
    def exposed_names
      @bindings_mutex.synchronize { @bindings.keys.dup }
    end

    # Entry point called from exec-main-vm.c right after Ruby boot. Spawns the
    # listener Thread and returns immediately. Idempotent: calling twice is a
    # no-op once the first listener is up.
    def start(host:, port:, token:, session_name: nil)
      raise ArgumentError, "port must be a positive Integer" unless port.is_a?(Integer) && port > 0
      raise ArgumentError, "token must be a non-empty String" unless token.is_a?(String) && !token.empty?

      @started_lock.synchronize do
        return if @listener_thread && @listener_thread.alive?

        host = host && !host.empty? ? host : "127.0.0.1"
        session_label = session_name && !session_name.empty? ? session_name : "remote-eval"

        @listener_thread = Thread.new do
          # Make sure a crash in the accept loop is visible — but do NOT take
          # the whole VM down with us. abort_on_exception=false (the default)
          # is what we want; we just want stack traces in the log.
          Thread.current.name = session_label
          begin
            accept_loop(host, port, token)
          rescue => e
            VMLogger.error "[RemoteEval] listener crashed: #{e.class}: #{e.message}"
            e.backtrace.first(15).each { |l| VMLogger.error "[RemoteEval]   #{l}" }
          end
        end

        VMLogger.info "[RemoteEval] listening on #{host}:#{port} (session: #{session_label})"
      end

      nil
    end

    private

    def accept_loop(host, port, token)
      # Socket.tcp_server_sockets + Socket.accept_loop is the same pattern the
      # debug gem uses in debug/server.rb — handles IPv4/IPv6 dual-stack and
      # cleans up sockets on Terminate-style exceptions automatically.
      Socket.tcp_server_sockets(host, port) do |socks|
        Socket.accept_loop(socks) do |sock, client_addr|
          handle_connection(sock, client_addr, token)
        end
      end
    end

    def handle_connection(sock, client_addr, expected_token)
      sock.sync = true

      # Cookie handshake. We send a one-line greeting that doubles as a
      # token challenge; client sends the token on a single line.
      sock.write("#{PROTOCOL_GREETING}\n")
      received = sock.gets&.chomp
      if received.nil? || received != expected_token
        # Drop without echoing what they sent — same posture as debug/server.rb.
        # We DO log the source address so an operator can spot brute-force
        # attempts in the VM log.
        addr = client_addr.respond_to?(:ip_address) ? "#{client_addr.ip_address}:#{client_addr.ip_port}" : client_addr.to_s
        VMLogger.error "[RemoteEval] auth rejected for #{addr}"
        sock.close rescue nil
        return
      end
      sock.write("OK\n")

      # Enforce one-session-at-a-time.
      unless @session_mutex.try_lock
        sock.write("BUSY: another session is active; try again later\n")
        sock.close rescue nil
        return
      end

      begin
        VMLogger.info "[RemoteEval] session opened"
        run_session(sock)
      rescue => e
        VMLogger.error "[RemoteEval] session error: #{e.class}: #{e.message}"
        e.backtrace.first(10).each { |l| VMLogger.error "[RemoteEval]   #{l}" }
      ensure
        @session_mutex.unlock
        sock.close rescue nil
        VMLogger.info "[RemoteEval] session closed"
      end
    end

    def run_session(sock)
      scope_name = :TOPLEVEL
      current_binding = TOPLEVEL_BINDING

      loop do
        sock.write("rubyvm[#{scope_name}]> ")
        first_line = sock.gets
        return if first_line.nil?              # EOF — client disconnected
        first_line = first_line.chomp

        # Special commands operate on the raw first line; they don't compose
        # with multi-line eval. handle_command returns:
        #   :passthrough — not a command, fall through to eval
        #   :exit        — close the session
        #   :stay        — handled, keep current scope, re-prompt
        #   :reset       — switch to TOPLEVEL_BINDING, re-prompt
        #   [name, bnd]  — switch to a named scope, re-prompt
        handled = handle_command(sock, first_line)
        unless handled == :passthrough
          case handled
          when :exit  then return
          when :reset then scope_name, current_binding = :TOPLEVEL, TOPLEVEL_BINDING
          when Array  then scope_name, current_binding = handled
          when :stay  then # no-op, just re-prompt
          end
          next
        end

        # Multi-line: keep reading until the buffer parses, or the user sends
        # `.` on its own line (force-end).
        buf = first_line
        until complete?(buf)
          extra = sock.gets
          return if extra.nil?
          extra_chomped = extra.chomp
          break if extra_chomped == "."
          buf << "\n" << extra_chomped
        end
        next if buf.strip.empty?

        eval_and_respond(sock, buf, current_binding, scope_name)
      end
    end

    # Returns one of:
    #   :passthrough  — not a special command, fall through to eval
    #   :exit         — close the session
    #   :stay         — handled, keep current scope, re-prompt
    #   :reset        — switch to TOPLEVEL
    #   [name, bnd]   — switch to a named scope
    def handle_command(sock, line)
      case line
      when ""
        :stay
      when "help"
        sock.write(
          "commands: attach <name> | detach | scopes | help | exit\n" \
          "multi-line: end with `.` on its own line, or rely on auto-detect\n"
        )
        :stay
      when "exit", "quit"
        sock.write("bye\n")
        :exit
      when "detach"
        sock.write("=> detached, back to TOPLEVEL_BINDING\n")
        :reset
      when "scopes"
        names = exposed_names
        if names.empty?
          sock.write("(no scopes exposed — scripts use RemoteEval.expose(:name, binding))\n")
        else
          sock.write("scopes: #{names.map(&:inspect).join(', ')}\n")
        end
        :stay
      when /\Aattach\s+(\S+)\z/
        name = Regexp.last_match(1).to_sym
        bnd = @bindings_mutex.synchronize { @bindings[name] }
        if bnd.nil?
          sock.write("ERROR: no scope named #{name.inspect}; try `scopes`\n")
          :stay
        else
          sock.write("=> attached to #{name.inspect}\n")
          [name, bnd]
        end
      else
        :passthrough
      end
    end

    # `Ripper.sexp` returns nil for incomplete code. It does NOT raise on real
    # syntax errors (it returns nil there too) — but if the input contains a
    # real syntax error, the subsequent `eval` will surface it cleanly, so we
    # don't try to distinguish here.
    def complete?(src)
      !Ripper.sexp(src).nil?
    rescue
      true   # Defensive: if Ripper itself raises, let eval handle it
    end

    def eval_and_respond(sock, src, bnd, scope_name)
      captured_out = StringIO.new
      captured_err = StringIO.new
      saved_out, saved_err = $stdout, $stderr
      $stdout, $stderr = captured_out, captured_err

      result = nil
      raised = nil
      begin
        result = bnd.eval(src, "(remote-eval:#{scope_name})", 1)
      rescue Exception => e
        raised = e
      ensure
        $stdout, $stderr = saved_out, saved_err
      end

      captured_out.string.each_line { |l| sock.write("out: #{l.chomp}\n") }
      captured_err.string.each_line { |l| sock.write("err: #{l.chomp}\n") }

      if raised
        sock.write("ERROR: #{raised.class}: #{raised.message}\n")
        (raised.backtrace || []).first(10).each { |l| sock.write("  #{l}\n") }
      else
        # Use #inspect to render; cap pathological outputs at 64 KiB so a
        # `1_000_000.times.to_a.inspect` doesn't DoS the socket.
        rendered = safe_inspect(result)
        sock.write("=> #{rendered}\n")
      end
    end

    def safe_inspect(obj)
      s = obj.inspect
      limit = 65_536
      s.bytesize > limit ? "#{s.byteslice(0, limit)}... (truncated, #{s.bytesize} bytes)" : s
    rescue => e
      "#<inspect raised #{e.class}: #{e.message}>"
    end
  end
end
