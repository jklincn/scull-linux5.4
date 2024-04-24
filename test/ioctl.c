#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "test.h"

int main() {
    int fd, ret;
    int quantum = 1024, qset = 32;
    int old_quantum, old_qset;

    // 打开设备文件
    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("Failed to open the device");
        return errno;
    }

    // 测试重置命令
    ret = ioctl(fd, SCULL_IOCRESET);
    SCULL_ASSERT(ret == 0);

    // 设置 quantum 值
    ret = ioctl(fd, SCULL_IOCSQUANTUM, &quantum);
    SCULL_ASSERT(ret == 0);
    // 获取 quantum 值
    ret = ioctl(fd, SCULL_IOCGQUANTUM, &old_quantum);
    SCULL_ASSERT(ret == 0);
    SCULL_ASSERT(old_quantum == quantum);

    quantum = 2048;
    // 设置 quantum 值
    ret = ioctl(fd, SCULL_IOCTQUANTUM, quantum);
    SCULL_ASSERT(ret == 0);
    // 获取 quantum 值
    ret = ioctl(fd, SCULL_IOCQQUANTUM);
    SCULL_ASSERT(ret == quantum);

    // 设置 qset 值
    ret = ioctl(fd, SCULL_IOCSQSET, &qset);
    SCULL_ASSERT(ret == 0);
    // 获取 qset 值
    ret = ioctl(fd, SCULL_IOCGQSET, &old_qset);
    SCULL_ASSERT(ret == 0);
    SCULL_ASSERT(old_qset == qset);

    qset = 64;
    // 设置 qset 值
    ret = ioctl(fd, SCULL_IOCTQSET, qset);
    SCULL_ASSERT(ret == 0);
    // 获取 qset 值
    ret = ioctl(fd, SCULL_IOCQQSET);
    SCULL_ASSERT(ret == qset);

    // 测试 SCULL_IOCXQUANTUM
    // 此时设备 quantum=2048
    old_quantum = 1024;
    ret = ioctl(fd, SCULL_IOCXQUANTUM, &old_quantum);
    SCULL_ASSERT(ret == 0);
    SCULL_ASSERT(old_quantum == quantum);
    ret = ioctl(fd, SCULL_IOCQQUANTUM);
    SCULL_ASSERT(ret == 1024);

    // 测试 SCULL_IOCXQSET
    // 此时设备 qset=64
    old_qset = 128;
    ret = ioctl(fd, SCULL_IOCXQSET, &old_qset);
    SCULL_ASSERT(ret == 0);
    SCULL_ASSERT(old_qset == qset);
    ret = ioctl(fd, SCULL_IOCQQSET);
    SCULL_ASSERT(ret == 128);

    // 测试 SCULL_IOCHQUANTUM
    // 此时设备 quantum=1024
    ret = ioctl(fd, SCULL_IOCHQUANTUM, quantum);
    SCULL_ASSERT(ret == 1024);
    ret = ioctl(fd, SCULL_IOCQQUANTUM);
    SCULL_ASSERT(ret == quantum);

    // 测试 SCULL_IOCHQSET
    // 此时设备 qset=128
    ret = ioctl(fd, SCULL_IOCHQSET, qset);
    SCULL_ASSERT(ret == 128);
    ret = ioctl(fd, SCULL_IOCQQSET);
    SCULL_ASSERT(ret == qset);

    // 关闭设备文件
    close(fd);
    return 0;
}
