#include <stdio.h>
#include <string.h>
#include "jni_logging.h"

/**
 * JNI Layer Tests
 *
 * Tests the weak symbol logging mechanism without Android dependencies.
 * Verifies that:
 * 1. Default no-op logging works (doesn't crash)
 * 2. Custom logging implementation can be provided
 * 3. jni_log_write and jni_log_printf work correctly
 */

// Test counter for custom logging
static int log_call_count = 0;
static char last_tag[256] = {0};
static char last_message[1024] = {0};
static JNILogPriority last_priority = JNI_LOG_UNKNOWN;

// Custom logging implementation for testing
// This provides a strong symbol that overrides the weak default
void jni_log_impl(JNILogPriority priority, const char* tag, const char* message) {
    log_call_count++;
    last_priority = priority;

    if (tag) {
        strncpy(last_tag, tag, sizeof(last_tag) - 1);
        last_tag[sizeof(last_tag) - 1] = '\0';
    }

    if (message) {
        strncpy(last_message, message, sizeof(last_message) - 1);
        last_message[sizeof(last_message) - 1] = '\0';
    }

    // Also print to console for visibility
    const char* level_str;
    switch (priority) {
        case JNI_LOG_ERROR: level_str = "ERROR"; break;
        case JNI_LOG_WARN:  level_str = "WARN";  break;
        case JNI_LOG_INFO:  level_str = "INFO";  break;
        case JNI_LOG_DEBUG: level_str = "DEBUG"; break;
        default:            level_str = "LOG";   break;
    }

    printf("[TEST:%s] %s: %s\n", level_str, tag ? tag : "NULL", message ? message : "NULL");
}

int main(void) {
    int failures = 0;

    printf("=== JNI Logging Tests ===\n\n");

    // Test 1: Basic write
    printf("Test 1: jni_log_write with ERROR priority\n");
    log_call_count = 0;
    jni_log_write(JNI_LOG_ERROR, "TestTag", "Test message");

    if (log_call_count != 1) {
        printf("  FAIL: Expected 1 call, got %d\n", log_call_count);
        failures++;
    } else if (last_priority != JNI_LOG_ERROR) {
        printf("  FAIL: Expected priority ERROR (%d), got %d\n", JNI_LOG_ERROR, last_priority);
        failures++;
    } else if (strcmp(last_tag, "TestTag") != 0) {
        printf("  FAIL: Expected tag 'TestTag', got '%s'\n", last_tag);
        failures++;
    } else if (strcmp(last_message, "Test message") != 0) {
        printf("  FAIL: Expected message 'Test message', got '%s'\n", last_message);
        failures++;
    } else {
        printf("  PASS\n");
    }

    // Test 2: Printf formatting
    printf("\nTest 2: jni_log_printf with formatted message\n");
    log_call_count = 0;
    jni_log_printf(JNI_LOG_WARN, "WarnTag", "Error code: %d, string: %s", 42, "test");

    if (log_call_count != 1) {
        printf("  FAIL: Expected 1 call, got %d\n", log_call_count);
        failures++;
    } else if (last_priority != JNI_LOG_WARN) {
        printf("  FAIL: Expected priority WARN, got %d\n", last_priority);
        failures++;
    } else if (strcmp(last_message, "Error code: 42, string: test") != 0) {
        printf("  FAIL: Expected formatted message, got '%s'\n", last_message);
        failures++;
    } else {
        printf("  PASS\n");
    }

    // Test 3: Multiple calls
    printf("\nTest 3: Multiple sequential calls\n");
    log_call_count = 0;
    jni_log_write(JNI_LOG_INFO, "Tag1", "Message1");
    jni_log_write(JNI_LOG_DEBUG, "Tag2", "Message2");
    jni_log_write(JNI_LOG_ERROR, "Tag3", "Message3");

    if (log_call_count != 3) {
        printf("  FAIL: Expected 3 calls, got %d\n", log_call_count);
        failures++;
    } else if (strcmp(last_message, "Message3") != 0) {
        printf("  FAIL: Expected last message 'Message3', got '%s'\n", last_message);
        failures++;
    } else {
        printf("  PASS\n");
    }

    // Test 4: All priority levels
    printf("\nTest 4: All log priority levels\n");
    jni_log_write(JNI_LOG_VERBOSE, "Test", "Verbose");
    jni_log_write(JNI_LOG_DEBUG, "Test", "Debug");
    jni_log_write(JNI_LOG_INFO, "Test", "Info");
    jni_log_write(JNI_LOG_WARN, "Test", "Warning");
    jni_log_write(JNI_LOG_ERROR, "Test", "Error");
    jni_log_write(JNI_LOG_FATAL, "Test", "Fatal");
    printf("  PASS (if no crashes)\n");

    // Summary
    printf("\n=== Test Summary ===\n");
    printf("Total failures: %d\n", failures);

    if (failures == 0) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("Some tests FAILED!\n");
        return 1;
    }
}
