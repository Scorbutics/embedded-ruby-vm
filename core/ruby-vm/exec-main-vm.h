#ifndef EXEC_MAIN_VM_H
#define EXEC_MAIN_VM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "embedded-ruby-vm/ruby-vm.h"

int ExecMainRubyVM(RubyVM* vm, const char* rubyDirectoryPath, const char* nativeLibsDirLocation);

#ifdef __cplusplus
}
#endif

#endif //EXEC_MAIN_VM_H
