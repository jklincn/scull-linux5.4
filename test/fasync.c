#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

volatile int timeout = 1;  // 超时标志，假设超时发生
int fd;

static void sigio_handler(int signo) {
    if (signo == SIGIO) {
        timeout = 0;  // 收到信号，更新超时标志
    }
}

static void sigalrm_handler(int signo) {
    if (signo == SIGALRM) {
        exit(1);  // 超时发生，直接退出程序
    }
}

void *writer_thread(void *arg) {
    // 简单等待，确保主线程已设置好异步通知
    sleep(1);
    const char *data = "Hello, SCULL!";
    write(fd, data, strlen(data));  // 向设备写入数据
    return NULL;
}

int main() {
    pthread_t writer;

    fd = open(PIPE_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return EXIT_FAILURE;
    }

    // 设置信号处理函数
    signal(SIGIO, sigio_handler);
    signal(SIGALRM, sigalrm_handler);

    // 允许接收 SIGIO 信号
    fcntl(fd, F_SETOWN, getpid());
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | FASYNC);

    // 设置5秒超时
    alarm(TIMEOUT_SECONDS);

    pthread_create(&writer, NULL, writer_thread, NULL);

    // 等待信号
    pause();  // 挂起进程，直到收到信号

    pthread_join(writer, NULL);
    close(fd);
    return timeout;
}
