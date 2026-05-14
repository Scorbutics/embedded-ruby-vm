#ifndef EXEC_MAIN_VM_H
#define EXEC_MAIN_VM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "embedded-ruby-vm/ruby-vm.h"

int ExecMainRubyVM(RubyVM* vm, const char* rubyDirectoryPath, const char* nativeLibsDirLocation);

/**
 * Execute a Ruby script file directly on the calling thread.
 * No VM thread is spawned; Ruby initialization and execution happen inline.
 *
 * This is intended for use cases where the calling thread must be
 * the thread that runs Ruby (e.g., Android NativeActivity where the
 * SFML thread has an ALooper prepared).
 *
 * @param rubyDirectoryPath     Path to Ruby standard library
 * @param nativeLibsDirLocation Path to native extension libraries
 * @param scriptFilePath        Path to the Ruby script file to execute
 * @return Ruby exit code (0 on success)
 */
int ExecRubyScriptInline(const char* rubyDirectoryPath,
                         const char* nativeLibsDirLocation,
                         const char* scriptFilePath);

/**
 * Same as ExecRubyScriptInline but additionally arms the remote DAP and/or
 * the remote line-eval listener at VM boot, mirroring the JNI path's
 * enableRemoteDebug/enableRemoteEval. Either or both opts pointers can be
 * NULL to skip that listener.
 *
 * Intended for Android NativeActivity wrappers that read listener
 * configuration from environment variables before the VM boots.
 */
int ExecRubyScriptInlineWithRemote(const char* rubyDirectoryPath,
                                   const char* nativeLibsDirLocation,
                                   const char* scriptFilePath,
                                   const RubyVMRemoteDebugOptions* remote_debug,
                                   const RubyVMRemoteEvalOptions* remote_eval);

#ifdef __cplusplus
}
#endif

#endif //EXEC_MAIN_VM_H
