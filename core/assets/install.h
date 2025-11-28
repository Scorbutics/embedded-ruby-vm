#ifndef INSTALL_H
#define INSTALL_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Install all embedded files to the specified directory.
 *
 * This function will:
 * 1. Extract the Ruby standard library ZIP to <install_dir>/ruby-stdlib/
 * 2. Write fifo_interpreter.rb to <install_dir>/fifo_interpreter.rb
 *
 * @param install_dir The directory where files should be installed
 * @return 0 on success, -1 on error
 */
int install_embedded_files(const char *install_dir);

/**
 * Get a default installation directory.
 *
 * Returns a platform-appropriate directory for installing files:
 * - Uses $XDG_CACHE_HOME/your_app if available
 * - Falls back to $HOME/.cache/your_app
 * - Falls back to /tmp/your_app_install
 *
 * @return Pointer to static string containing default install directory
 */
const char* get_default_install_dir(void);

/**
 * Check if installation is needed.
 *
 * Checks if the key files already exist in the install directory.
 *
 * @param install_dir Directory to check
 * @return 1 if installation is needed, 0 if files already exist, -1 on error
 */
int installation_needed(const char *install_dir);

#ifdef __cplusplus
}
#endif

#endif // INSTALL_H
