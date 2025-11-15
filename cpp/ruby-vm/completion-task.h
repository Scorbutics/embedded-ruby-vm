#ifndef COMPLETION_TASK_H
#define COMPLETION_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Completion callback function signature.
 * @param user_data Context data provided when the task was created
 * @param result Completion result code (0 = success, non-zero = error)
 */
typedef void (*RubyCompletionCallback)(void* user_data, int result);

/**
 * Completion task structure containing callback and context.
 * This encapsulates both the function pointer and its associated data.
 */
typedef struct {
    RubyCompletionCallback callback;  // Function to call on completion
    void* user_data;                  // Context data to pass to callback
} RubyCompletionTask;

/**
 * Helper to create a completion task.
 * @param callback Function to call on completion (can be NULL)
 * @param user_data Context data to pass to callback (can be NULL)
 * @return Initialized RubyCompletionTask
 */
static inline RubyCompletionTask ruby_completion_task_create(
        RubyCompletionCallback callback,
        void* user_data
) {
    RubyCompletionTask task = {
            .callback = callback,
            .user_data = user_data
    };
    return task;
}

/**
 * Helper to invoke a completion task.
 * Safe to call even if callback is NULL.
 * @param task The task to invoke
 * @param result The completion result code
 */
static inline void ruby_completion_task_invoke(RubyCompletionTask* task, int result) {
    if (task && task->callback) {
        task->callback(task->user_data, result);
    }
}

#ifdef __cplusplus
}
#endif

#endif //COMPLETION_TASK_H
