#define FIFO_LOGS_NAME "psdk_fifo_logs"
#define FIFO_COMMANDS_NAME "psdk_fifo_commands"


#define MAX_PATH_LENGTH 512
#define MAX_CONTENT_SIZE 65536

#define ENV_RUBY_VM_ADDITIONAL_PARAM "RUBY_VM_ADDITIONAL_PARAM"

#define FIFO_INTERPRETER_SCRIPT "fifo_interpreter.rb"

// Internal protocol sentinel sent by fifo_interpreter.rb via VMLogger
// after a script finishes and all output has been flushed.
// The logging system intercepts this on LOG_STREAM_VMLOGGER and signals
// completion — it never reaches user callbacks.
#define SCRIPT_COMPLETE_SENTINEL "<<<LOGS_FLUSHED>>>"

// Drain barrier marker written by logging_drain_async() (via the C side, not
// Ruby). Independent of SCRIPT_COMPLETE_SENTINEL so a test calling
// logging_drain() never collides with an in-flight ruby_vm_execute_sync()
// also waiting on the script sentinel.
#define LOGGING_DRAIN_BARRIER "<<<DRAIN_BARRIER>>>"

// VMLogger severity tags prepended by safe_runner.rb (VMLogger.debug/info/error)
// to every line written to the vmlogger pipe when @output_io is set. The C side
// strips the tag in write_full_log_line and surfaces the parsed severity as a
// log_level_t on the LogMessage callback — recovering the per-call severity
// that would otherwise be flattened by the single shared pipe.
//
// Format: <PREFIX><LEVEL><SUFFIX><message>\n
//   e.g. "<<<VMLOG:INFO>>> [Ruby VM] hello world"
// Tag levels must mirror VMLogger::TAG_* in safe_runner.rb.
#define VMLOGGER_LEVEL_TAG_PREFIX  "<<<VMLOG:"
#define VMLOGGER_LEVEL_TAG_SUFFIX  ">>> "
#define VMLOGGER_LEVEL_NAME_DEBUG  "DEBUG"
#define VMLOGGER_LEVEL_NAME_INFO   "INFO"
#define VMLOGGER_LEVEL_NAME_ERROR  "ERROR"

// Host event lines written by VMHost.emit (vm_host.rb) on the vmlogger
// pipe. write_full_log_line hands them to the interpreter's LogListener as
// LOG_STREAM_HOST_EVENT, never to the logs.
//
// Format: <TAG><interpreter tag><topic> <base64 payload>\n
//   e.g. "<<<VMHOST:EVENT>>> \x01[3]\x01overlay.edited eyJ4IjoxfQ=="
// Lines stay short (see VMHost::LINE_MAX), so a longer event comes as PART
// lines carrying the start of its base64, then an EVENT line with the
// rest; the C side joins them.
// Must mirror VMHost::EVENT_TAG / PART_TAG in vm_host.rb.
#define VMHOST_EVENT_TAG "<<<VMHOST:EVENT>>> "
#define VMHOST_PART_TAG  "<<<VMHOST:PART>>> "

// Ruby prelude run right before arming the `debug` gem's TCP listener
// (pre-boot in exec-main-vm.c, post-boot in ruby-interpreter.c). The gem
// binds its socket later, in its own session thread, which runs with
// abort_on_exception: a port already taken (e.g. by another app built
// from the same template, same fixed port) re-raises EADDRINUSE into the
// VM main thread and the dispatcher exits the whole VM. Binding once here,
// synchronously, makes a busy port raise in the caller's protected eval
// instead, so the debugger is skipped and the VM keeps running.
#define RUBY_VM_DEBUG_PORT_PROBE_SRC \
    "require 'socket'\n" \
    "begin\n" \
    "  TCPServer.new(ENV['RUBY_DEBUG_HOST'], Integer(ENV['RUBY_DEBUG_PORT'])).close\n" \
    "rescue SystemCallError => e\n" \
    "  raise \"remote debugger not started: #{e.message}\"\n" \
    "end\n"
