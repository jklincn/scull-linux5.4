#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>

#include "test.h"

int fd;
int poll_timeout = 0;  // 跟踪poll是否超时

void *write_thread(void *arg) {
    // 等待1秒后开始写入，确保poll已经在等待
    sleep(1);
    const char *msg = "Test data";
    ssize_t bytes_written = write(fd, msg, strlen(msg));
    if (bytes_written < 0) {
        perror("Write failed");
    }
    return NULL;
}

void *poll_thread(void *arg) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, TIMEOUT_SECONDS * 1000);
    if (ret < 0) {
        perror("Poll failed");
    } else if (ret == 0) {
        // 设置超时标志
        poll_timeout = 1;
    } else if (pfd.revents & POLLIN) {
        return NULL;
    }
    return NULL;
}

int main() {
    fd = open(PIPE_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    pthread_t t1, t2;
    pthread_create(&t1, NULL, poll_thread, NULL);
    pthread_create(&t2, NULL, write_thread, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    close(fd);
    return poll_timeout ? 1 : 0;  // 根据poll超时状态返回
}
