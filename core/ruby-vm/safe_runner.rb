#!/usr/bin/env ruby
# safe_runner.rb
#
# Reusable wrapper for running Ruby scripts in a safe environment
# with logging, exit interception, and comprehensive exception handling.
#
# This script is loaded from memory by the C runtime (rb_eval_string + rb_provide)
# before any user script runs. It provides:
#   - VMLogger: unified logging to STDOUT/STDERR, log files, or a dedicated IO
#   - Exit interception: Kernel.exit/exit!/Process.exit/exit! raise SystemExit
#   - SafeRunner.run { }: block-based execution with full exception handling
#
# Usage:
#   require 'safe_runner'
#   SafeRunner.run(
#     log_path: ENV['RGSS_LOG_FILE'],
#     error_log_path: ENV['RGSS_ERROR_LOG_FILE']
#   ) do
#     Dir.chdir './Release'
#     require './Game.rb'
#   end

# Logging system - supports multiple output modes:
#   - Default: STDOUT/STDERR (shows in logcat on Android)
#   - File-based: optional log files for UI display (via setup_log_files)
#   - IO-based: dedicated IO object overrides all output (via set_output)
#
# Log levels: DEBUG (0), INFO (1), ERROR (2)
# Set via RUBY_VM_LOG_LEVEL environment variable.
module VMLogger
  LOG_DEBUG = 0
  LOG_INFO = 1
  LOG_ERROR = 2

  @log_level = if ENV['RUBY_VM_LOG_LEVEL']
                 ENV['RUBY_VM_LOG_LEVEL'].to_i
               elsif ENV['NDEBUG'] == '1'
                 LOG_ERROR
               else
                 LOG_INFO
               end

  @log_file = nil
  @error_log_file = nil
  @output_io = nil

  # Redirect all VMLogger output to a dedicated IO (e.g. a pipe FD).
  # When set, STDOUT/STDERR and file-based outputs are bypassed.
  def self.set_output(io)
    @output_io = io
    @output_io.sync = true if @output_io
  end

  # Set up file-based logging (additive to STDOUT/STDERR).
  # Has no effect when set_output is active.
  def self.setup_log_files(log_path, error_log_path)
    @log_file = File.open(log_path, 'w') if log_path && !log_path.empty?
    @error_log_file = File.open(error_log_path, 'w') if error_log_path && !error_log_path.empty?
  rescue => e
    STDERR.puts "[VMLogger] Failed to open log files: #{e.message}"
    STDERR.flush
  end

  def self.close
    @log_file&.close
    @error_log_file&.close
    @log_file = nil
    @error_log_file = nil
  end

  def self.debug(message)
    return if @log_level > LOG_DEBUG
    write_out(message)
  end

  def self.info(message)
    return if @log_level > LOG_INFO
    write_out(message)
  end

  def self.error(message)
    if @output_io
      @output_io.puts(message)
      @output_io.flush
    else
      STDERR.puts(message)
      STDERR.flush
      write_to_error_log(message)
      write_to_log(message)
    end
  end

  private

  def self.write_out(message)
    if @output_io
      @output_io.puts(message)
      @output_io.flush
    else
      STDOUT.puts(message)
      STDOUT.flush
      write_to_log(message)
    end
  end

  def self.write_to_log(message)
    return unless @log_file
    @log_file.puts(message)
    @log_file.flush
  end

  def self.write_to_error_log(message)
    return unless @error_log_file
    @error_log_file.puts(message)
    @error_log_file.flush
  end
end

# Prevent scripts from killing the host process.
# Intercept exit/exit! to log the call site before raising SystemExit,
# so SafeRunner.run or fifo_interpreter can catch it and report what happened.
$__safe_runner_exit_code = nil

module Kernel
  alias_method :original_exit!, :exit!
  def exit!(status = false)
    status_code = status.is_a?(Integer) ? status : (status ? 0 : 1)
    $__safe_runner_exit_code = status_code
    VMLogger.info "[SafeRunner] exit!(#{status_code}) called"
    caller.each { |line| VMLogger.info "[SafeRunner]   #{line}" }
    raise SystemExit.new(status_code, "exit! intercepted by safe_runner")
  end

  alias_method :original_exit, :exit
  def exit(status = true)
    status_code = status.is_a?(Integer) ? status : (status ? 0 : 1)
    $__safe_runner_exit_code = status_code
    VMLogger.info "[SafeRunner] exit(#{status_code}) called"
    caller.each { |line| VMLogger.info "[SafeRunner]   #{line}" }
    raise SystemExit.new(status_code, "exit intercepted by safe_runner")
  end
end

module Process
  class << self
    alias_method :original_exit!, :exit!
    def exit!(status = false)
      Kernel.exit!(status)
    end

    alias_method :original_exit, :exit
    def exit(status = true)
      Kernel.exit(status)
    end
  end
end

module SafeRunner
  # Run a block in a safe environment with logging and exception handling.
  #
  # @param log_path [String, nil] Path to the stdout log file
  # @param error_log_path [String, nil] Path to the error log file
  # @yield The script code to execute
  # @return [Integer] Exit code (0 = success)
  def self.run(log_path: nil, error_log_path: nil)
    VMLogger.setup_log_files(log_path, error_log_path)
    VMLogger.info "[SafeRunner] Starting (Ruby #{RUBY_VERSION}, #{RUBY_PLATFORM})"
    VMLogger.info "[SafeRunner] Working directory: #{Dir.pwd}"

    $__safe_runner_exit_code = nil

    begin
      yield
      VMLogger.info "[SafeRunner] Script finished normally"
      0

    rescue SystemExit => e
      VMLogger.info "[SafeRunner] SystemExit with status: #{e.status}"
      e.backtrace&.each { |line| VMLogger.info "[SafeRunner]   #{line}" }
      e.status

    rescue Exception => e
      VMLogger.error "[SafeRunner] #{e.class}: #{e.message}"
      VMLogger.error "[SafeRunner] Backtrace:"
      e.backtrace&.each { |line| VMLogger.error "[SafeRunner]   #{line}" }
      1

    ensure
      $stdout.flush rescue nil
      $stderr.flush rescue nil
      VMLogger.close
    end
  end
end
