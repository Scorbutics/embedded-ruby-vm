#ifndef RUBY_COMM_CHANNEL_H
#define RUBY_COMM_CHANNEL_H

#ifdef __cplusplus
extern "C" {
#endif

// Communication channel structure for cross-platform compatibility
typedef struct {
    union {
        int read_fd;
        int main_fd;
    };
    union {
        int write_fd;
        int second_fd;
    };
} CommChannel;


// Helper functions for communication channels
int create_comm_channel(CommChannel* channel);
void close_comm_channel(CommChannel* channel);

#ifdef __cplusplus
}
#endif

#endif //RUBY_COMM_CHANNEL_H
