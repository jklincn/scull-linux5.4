#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

#define BUFFER_SIZE 1024

int main() {
    int fd;
    char write_buf[BUFFER_SIZE] =
        "Hello, this is a test data for scull device read and write test.";
    char read_buf[BUFFER_SIZE] = {0};
    // 打开设备文件
    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return errno;
    }
    // 写数据到 scull 设备
    int bytes_written = write(fd, write_buf, strlen(write_buf));
    if (bytes_written < 0) {
        perror("Failed to write to the device");
        return errno;
    }

    // 移动文件指针到开头
    lseek(fd, 0, SEEK_SET);

    // 从 scull 设备读数据
    int bytes_read = read(fd, read_buf, sizeof(read_buf));
    if (bytes_read < 0) {
        perror("Failed to read from the device");
        return errno;
    }
    // 关闭设备
    close(fd);

    SCULL_ASSERT(!strcmp(write_buf, read_buf));

    return 0;
}
