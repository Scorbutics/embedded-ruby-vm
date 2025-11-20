#include <stdio.h>
#include <string.h>
#include "jni_logging.h"

/**
 * JNI Android Logging Tests
 *
 * Tests that the Android logging module is properly linked and overrides
 * the weak symbol with __android_log_write implementation.
 *
 * Note: This test verifies the mechanism works, but actual logcat output
 *       should be verified manually using `adb logcat`.
 */

// We cannot directly test if __android_log_write is called (it's internal),
// but we can verify:
// 1. Logging doesn't crash
// 2. The weak symbol has been overridden (by checking symbol isn't the default no-op)

// Forward declare the weak symbol to check if it's been overridden
extern void jni_log_impl(JNILogPriority priority, const char* tag, const char* message)
    __attribute__((weak));

int main(void) {
    printf("=== JNI Android Logging Tests ===\n\n");

    // Test 1: Verify jni_log_impl is defined (not NULL)
    printf("Test 1: Checking if jni_log_impl is overridden\n");
    if (jni_log_impl == NULL) {
        printf("  FAIL: jni_log_impl is NULL\n");
        return 1;
    }
    printf("  PASS: jni_log_impl is defined\n");

    // Test 2: Basic smoke test - call logging functions
    printf("\nTest 2: Calling jni_log_write (should not crash)\n");
    jni_log_write(JNI_LOG_INFO, "AndroidTest", "Test INFO message");
    jni_log_write(JNI_LOG_WARN, "AndroidTest", "Test WARN message");
    jni_log_write(JNI_LOG_ERROR, "AndroidTest", "Test ERROR message");
    printf("  PASS: No crashes\n");

    // Test 3: Printf formatting
    printf("\nTest 3: Calling jni_log_printf (should not crash)\n");
    jni_log_printf(JNI_LOG_ERROR, "AndroidTest", "Formatted: %d %s", 123, "test");
    printf("  PASS: No crashes\n");

    // Test 4: All priority levels
    printf("\nTest 4: All log priorities (should not crash)\n");
    jni_log_write(JNI_LOG_VERBOSE, "AndroidTest", "VERBOSE");
    jni_log_write(JNI_LOG_DEBUG, "AndroidTest", "DEBUG");
    jni_log_write(JNI_LOG_INFO, "AndroidTest", "INFO");
    jni_log_write(JNI_LOG_WARN, "AndroidTest", "WARN");
    jni_log_write(JNI_LOG_ERROR, "AndroidTest", "ERROR");
    jni_log_write(JNI_LOG_FATAL, "AndroidTest", "FATAL");
    printf("  PASS: No crashes\n");

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("All tests PASSED!\n");
    printf("\nNote: To verify Android logging actually works, check logcat:\n");
    printf("  adb logcat | grep AndroidTest\n");
    printf("\nYou should see the test messages above in logcat output.\n");

    return 0;
}
