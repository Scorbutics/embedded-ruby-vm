#ifndef EMBEDDED_RUBY_VM_REMOTE_OPTIONS_H
#define EMBEDDED_RUBY_VM_REMOTE_OPTIONS_H

/*
 * Public option structs for the embedded Ruby VM's remote listeners.
 *
 * These two POD structs are the input to `ruby_vm_enable_remote_debug` /
 * `ruby_vm_enable_remote_eval` (declared in ruby-vm.h) and to higher-level
 * wrappers like `ruby_interpreter_enable_remote_*` and
 * `ExecRubyScriptInlineWithRemote`. They are intentionally kept in their
 * own small header (free of pthread / atomic / RubyVM internals) so a
 * thin consumer — e.g. the NativeActivity wrapper in `kmp-publish/wrapper/
 * native_main.c` — can include them without dragging in the rest of the
 * VM's private types.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RubyVMRemoteDebugOptions RubyVMRemoteDebugOptions;
typedef struct RubyVMRemoteEvalOptions  RubyVMRemoteEvalOptions;

/**
 * Options for enabling the remote DAP (Debug Adapter Protocol) listener.
 * The listener is provided by Ruby 3.1's stdlib `debug` gem and accepts
 * connections from DAP clients (VS Code's rdbg extension, JetBrains,
 * `rdbg --attach`).
 *
 * Connect over the LAN with caution. The recommended deployment is to bind
 * to 127.0.0.1 and tunnel with `adb forward tcp:<port> tcp:<port>` (Android)
 * or `ssh -L <port>:127.0.0.1:<port> host` (remote desktop), so the
 * listener never leaves loopback.
 *
 * Ownership: callers fill this struct with their own strings and pass it to
 * ruby_vm_enable_remote_debug, which duplicates the strings into a private
 * heap copy stored on the RubyVM. Callers may free their strings immediately
 * after the call returns.
 */
struct RubyVMRemoteDebugOptions {
    const char* host;          /* bind address; NULL means "127.0.0.1"            */
    int         port;          /* TCP port; must be > 0                            */
    const char* token;         /* shared-secret cookie; MUST be non-NULL/non-empty */
    const char* session_name;  /* optional; surfaces in DAP messages, may be NULL  */
};

/**
 * Options for enabling the remote line-eval console listener (sibling of
 * ruby_vm_enable_remote_debug). The listener is implemented in remote_eval.rb
 * and serves a plain-text, nc-friendly protocol: cookie handshake, then a
 * REPL-style loop evaluating Ruby expressions against TOPLEVEL_BINDING (or a
 * registered scope via `RemoteEval.expose(:name, binding)`).
 *
 * Eval is more powerful than the debugger's REPL feature — anyone who can
 * connect runs arbitrary code in the VM. Bind to 127.0.0.1 unless you have
 * a specific reason; the token is mandatory regardless.
 *
 * Ownership: callers fill this struct with their own strings and pass it to
 * ruby_vm_enable_remote_eval, which duplicates the strings into a private
 * heap copy stored on the RubyVM. Callers may free their strings immediately
 * after the call returns.
 */
struct RubyVMRemoteEvalOptions {
    const char* host;          /* bind address; NULL means "127.0.0.1"             */
    int         port;          /* TCP port; must be > 0                             */
    const char* token;         /* shared-secret cookie; MUST be non-NULL/non-empty  */
    const char* session_name;  /* optional friendly label surfaced in the log       */
};

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDED_RUBY_VM_REMOTE_OPTIONS_H */
