#!/usr/bin/env ruby
# fifo_interpreter.rb
#
# Usage: ruby fifo_interpreter.rb <socket_fd>
#
# Protocol:
# 1. C side sends: "<length>\n<script_content>"
# 2. Ruby side executes the full script
# 3. Ruby side responds: "<exit_code>\n"

begin
  # Get socket file descriptor from command-line argument
  if ARGV.empty?
    raise ArgumentError, "Usage: #{$0} <socket_fd>"
  end

  ruby_fd = ARGV[0].to_i

  if ruby_fd <= 0
    raise ArgumentError, "Invalid socket file descriptor: #{ARGV[0]}"
  end

  # Wrap the file descriptor in an IO object (bidirectional)
  socket = IO.for_fd(ruby_fd, "r+")
  socket.sync = true  # Disable buffering - critical for real-time communication!

  # Log startup (useful for debugging)
  STDOUT.puts "[Ruby VM] FIFO interpreter started on fd=#{ruby_fd}"
  STDOUT.flush

  # Main REPL loop
  loop do
    # Read the length prefix (format: "<bytes>\n")
    length_line = socket.gets

    # EOF means the C side closed the socket - time to exit
    if length_line.nil?
      STDOUT.puts "[Ruby VM] Socket closed by peer, shutting down"
      break
    end

    length_str = length_line.strip

    # Skip empty lines
    next if length_str.empty?

    # Parse the length
    begin
      script_length = Integer(length_str)
    rescue ArgumentError
      STDERR.puts "[Ruby Error] Invalid length prefix: '#{length_str}'"
      socket.write("1\n")
      socket.flush
      next
    end

    # Validate length
    if script_length <= 0 || script_length > 10_000_000  # 10MB max
      STDERR.puts "[Ruby Error] Invalid script length: #{script_length}"
      socket.write("1\n")
      socket.flush
      next
    end

    # Read exactly script_length bytes
    script_content = socket.read(script_length)

    if script_content.nil? || script_content.bytesize != script_length
      STDERR.puts "[Ruby Error] Failed to read complete script (expected #{script_length} bytes)"
      socket.write("1\n")
      socket.flush
      next
    end

    STDOUT.puts "[Ruby VM] Executing script (#{script_length} bytes)"
    STDOUT.flush

    # Execute the Ruby script
    begin
      # Use TOPLEVEL_BINDING so code has access to top-level context
      result = eval(script_content, TOPLEVEL_BINDING, "<socket-script>")

      # Send success exit code
      socket.write("0\n")
      STDOUT.puts "[Ruby VM] Script executed successfully"
      STDOUT.flush

    rescue ScriptError, StandardError => error
      # Log the error to stderr (visible in logcat on Android)
      STDERR.puts "[Ruby Error] #{error.class}: #{error.message}"
      error.backtrace.each { |line| STDERR.puts "  #{line}" }
      STDERR.flush

      # Send failure exit code
      socket.write("1\n")
    end

    # Flush to ensure exit code is sent immediately
    socket.flush
  end

  # Clean shutdown
  socket.close
  STDOUT.puts "[Ruby VM] Shutdown complete"

rescue ArgumentError => error
  STDERR.puts "[Ruby VM Fatal] #{error.message}"
  exit(1)

rescue => error
  # Catch any other unexpected errors
  STDERR.puts "[Ruby VM Fatal] #{error.class}: #{error.message}"
  error.backtrace.each { |line| STDERR.puts "  #{line}" }
  STDERR.flush
  exit(1)
end