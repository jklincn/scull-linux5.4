#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "test.h"

#define NEW_PIPE_BUFFER_SIZE 256
#define FULL_DATA_BUFFER_SIZE 300
#define READER_BUFFER_SIZE 1024

// #define DEBUG

int fd;

void *reader_thread(void *arg) {
    char buffer[READER_BUFFER_SIZE];
    int ret;
#ifdef DEBUG
    printf("Reader started, waiting for data...\n");
#endif
    ret = read(fd, buffer, sizeof(buffer));
    if (ret < 0) {
        perror("Read failed");
        return NULL;
    }
#ifdef DEBUG
    printf("Reader read %d bytes: %.*s\n", ret, ret, buffer);
#endif
    return NULL;
}

void *writer_thread(void *arg) {
    int ret;
    char *data = (char *)arg;
#ifdef DEBUG
    printf("Writer is writing: %s\n", data);
#endif
    ret = write(fd, data, strlen(data));
    if (ret < 0) {
        perror("Write failed");
        return NULL;
    }
#ifdef DEBUG
    printf("Writer wrote %d bytes\n", ret);
#endif
    return NULL;
}

void signal_handler(int sig) {
    printf("Pipe Timed out.\n");
    exit(1);
}

int main() {
    int ret;
    pthread_t thr1, thr2, thr3, thr4;
    char *write_data = "Hello, pipe!";

    // 可以把缓冲区填满的数据
    char full_data[FULL_DATA_BUFFER_SIZE];
    memset(full_data, 'A', FULL_DATA_BUFFER_SIZE - 1);
    full_data[FULL_DATA_BUFFER_SIZE - 1] = '\0';

    // 设置一个超时
    signal(SIGALRM, signal_handler);
    alarm(TIMEOUT_SECONDS);

    // 调整管道缓冲区大小（顺便测试scull pipe的ioctl）
    int new_buffer_size = NEW_PIPE_BUFFER_SIZE;
    fd = open(PIPE_DEVICE, O_RDWR);
    ret = ioctl(fd, SCULL_P_IOCTSIZE, new_buffer_size);
    SCULL_ASSERT(ret == 0);
    ret = ioctl(fd, SCULL_P_IOCQSIZE, new_buffer_size);
    SCULL_ASSERT(ret == new_buffer_size);
    close(fd);

    fd = open(PIPE_DEVICE, O_RDWR);

    // 创建第一个读者线程（会阻塞）
    pthread_create(&thr1, NULL, reader_thread, NULL);
    sleep(1);
    // 启动一个写者线程
    pthread_create(&thr2, NULL, writer_thread, write_data);
    // 等待这两个线程完成，结果应该是写者先完成
    pthread_join(thr1, NULL);
    pthread_join(thr2, NULL);

    // 创建两个写者线程来填满缓冲区
    // 第一个是填满剩余的缓冲区，此时写指针重新回到缓冲区起始位置
    // 因为写指针回到了起始位置，因此这个时候写指针比读指针小，即还有空间可以写入。
    // 所以第二个是填满写指针到读指针的区域
    // 这边也是scull pipe设计的问题，因为它一次最大只能到缓冲区末尾或追上读指针
    // 而不是到缓冲区末尾，写指针重置后再去追读指针。
    pthread_create(&thr3, NULL, writer_thread, full_data);
    pthread_join(thr3, NULL);
    pthread_create(&thr3, NULL, writer_thread, full_data);
    pthread_join(thr3, NULL);

    // 这时缓冲区已满
    // 创建一个额外的写者线程（会阻塞）
    pthread_create(&thr4, NULL, writer_thread, "This should block until read.");
    sleep(1);
    // 启动一个读者线程来解阻
    pthread_create(&thr1, NULL, reader_thread, NULL);
    // 等待这两个线程完成，结果应该是读者先完成
    pthread_join(thr4, NULL);
    pthread_join(thr1, NULL);

    close(fd);
    return 0;
}
