#!/usr/bin/env ruby
# fifo_interpreter.rb
#
# Usage: ruby fifo_interpreter.rb <socket_fd>
#
# This script runs an infinite REPL that reads Ruby commands from a socket,
# executes them, and writes back exit codes (0 = success, 1 = failure).

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
    # Read one line of Ruby code
    line = socket.gets

    # EOF means the C side closed the socket - time to exit
    if line.nil?
      STDOUT.puts "[Ruby VM] Socket closed by peer, shutting down"
      break
    end

    command = line.strip

    # Skip empty commands
    next if command.empty?

    # Execute the Ruby code
    begin
      # Use TOPLEVEL_BINDING so code has access to top-level context
      result = eval(command, TOPLEVEL_BINDING)

      # Send success exit code
      socket.write('0')

    rescue ScriptError, StandardError => error
      # Log the error to stderr (visible in logcat on Android)
      STDERR.puts "[Ruby Error] #{error.class}: #{error.message}"
      error.backtrace.each { |line| STDERR.puts "  #{line}" }
      STDERR.flush

      # Send failure exit code
      socket.write('1')
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
