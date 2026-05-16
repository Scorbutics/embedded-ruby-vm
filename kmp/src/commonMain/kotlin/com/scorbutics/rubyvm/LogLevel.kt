package com.scorbutics.rubyvm

/**
 * Severity of a log message.
 *
 * For LOG_STREAM_VMLOGGER lines, the level reflects the VMLogger method that
 * produced the line (VMLogger.debug -> DEBUG, .info -> INFO, .error -> ERROR);
 * parsing happens in the C logging system from an in-band tag emitted by
 * safe_runner.rb so consumers see the level directly.
 *
 * For other sources, the level is derived from the stream identity:
 * RUBY_STDERR / NATIVE_STDERR -> ERROR; the remaining stdout streams -> INFO.
 *
 * Ordinals must stay aligned with `log_level_t` in include/public/embedded-ruby-vm/log-listener.h.
 */
enum class LogLevel {
    DEBUG,
    INFO,
    ERROR
}
