#include <unistd.h>
#include <stdlib.h>

#include "constants.h"

int env_update_locations(const char* execution_location, const char* archive_location) {
    int chdir_result = chdir(execution_location);
    int set_env_result = setenv(ENV_RUBY_VM_ADDITIONAL_PARAM, archive_location == NULL ? "" : archive_location, 1);
    return (chdir_result != 0) + (set_env_result != 0);
}
