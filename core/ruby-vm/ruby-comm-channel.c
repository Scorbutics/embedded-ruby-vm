#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ruby-comm-channel.h"

// Internal buffer size for efficient 'read()' calls
#define BUF_SIZE 4096

int create_comm_channel(CommChannel* channel) {
    if (!channel) return -1;

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
        return -1;
    }
    channel->read_fd = sv[0];
    channel->write_fd = sv[1];

    return 0;
}

void close_comm_channel(CommChannel* channel) {
    if (!channel) return;

    if (channel->read_fd >= 0) {
        close(channel->read_fd);
        channel->read_fd = -1;
    }
    if (channel->write_fd >= 0) {
        close(channel->write_fd);
        channel->write_fd = -1;
    }
}
