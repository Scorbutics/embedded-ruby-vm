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
