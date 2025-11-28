#ifndef EXEC_MAIN_VM_H
#define EXEC_MAIN_VM_H

#ifdef __cplusplus
extern "C" {
#endif

int ExecMainRubyVM(const char* scriptContent, int commandsFd,
                   const char* rubyDirectoryPath, const char* nativeLibsDirLocation);

#ifdef __cplusplus
}
#endif

#endif //EXEC_MAIN_VM_H
