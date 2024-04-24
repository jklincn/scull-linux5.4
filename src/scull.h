#ifndef SCULL_H
#define SCULL_H

#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/semaphore.h>

#ifndef SCULL_MAJOR
#define SCULL_MAJOR 0  // 默认动态分配
#endif

#ifndef SCULL_NR_DEVS
#define SCULL_NR_DEVS 4  // scull0 到 scull3
#endif

// 数据结构，按照默认值来解释
// scull_qset 是一个量子集合，最多可以保存 1024 个量子，每个量子最多可以保存 4096 字节的数据
// 因此每个量子集合最多可以保存 1024 * 4096 = 4194304 字节数据，即 4MB
// scull_dev 可以有多个量子集合，每个量子集合使用链表连接。

// 每个 quantum 可包含的字节数
#ifndef SCULL_QUANTUM
#define SCULL_QUANTUM 4096
#endif

// 每个 qset 的数组长度
// 即每个 qset 可保存 SCULL_QUANTUM * SCULL_QSET 字节的数据。
#ifndef SCULL_QSET
#define SCULL_QSET 1024
#endif

struct scull_qset {
    void **data;              // 数据实际保存位置
    struct scull_qset *next;  // 指向下一个 qset
};

// 每个内存区域称为 quantum
// 由 quantum 组成的数组称为 qset
struct scull_dev {
    struct scull_qset *data;  // 保存数据的头结点
    int quantum;              // 每个量子中可以存储的数据字节数
    int qset;                 // 当前保存的量子数量
    unsigned long size;       // 当前设备存储的数据总量
    unsigned int access_key;  // 用于访问控制
    struct semaphore sem;     // 互斥锁
    // scull 的做法是把 cdev 结构嵌入到 scull 设备结构体中
    struct cdev cdev;  // 字符设备结构体
};

struct scull_qset *scull_follow(struct scull_dev *dev, int n);
int scull_trim(struct scull_dev *dev);

// 文件操作集

loff_t scull_llseek(struct file *filp, loff_t off, int whence);
ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos);
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos);
long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int scull_open(struct inode *inode, struct file *filp);
int scull_release(struct inode *inode, struct file *filp);

// ioctl 定义

// 使用 k 作为魔数
#define SCULL_IOC_MAGIC 'k'

// 命名的小提示
// S(Set)   表示通过指针设置
// T(Tell)  表示通过直接变量设置
// G(Get)   表示通过指针获得
// Q(Query) 表示通过返回值获得
// X(eXchange) 表示通过指针设置，并通过指针返回旧值
// H(sHift) 表示通过直接变量设置，并通过返回值返回旧值

// ioctl命令号需要4个位段：数据传送方向，类型（魔数），顺序号，用户数据大小

// 将quantum和qset常量重置成默认值
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
// 设置新的quantum值（通过指针）
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int)
// 设置新的qset值（通过指针）
#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 2, int)
// 设置新的quantum值（通过直接变量）
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)
// 设置新的qset值（通过直接变量）
#define SCULL_IOCTQSET _IO(SCULL_IOC_MAGIC, 4)
// 获得当前的quantum值（通过指针）
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)
// 获得当前的qset值（通过指针）
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 6, int)
// 获得当前的quantum值（通过返回值）
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7)
// 获得当前的qset值（通过返回值）
#define SCULL_IOCQQSET _IO(SCULL_IOC_MAGIC, 8)
// 设置新的quantum值（通过指针）并返回旧的quantum值（通过指针）
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
// 设置新的qset值（通过指针）并返回旧的qset值（通过指针）
#define SCULL_IOCXQSET _IOWR(SCULL_IOC_MAGIC, 10, int)
// 设置新的quantum值（通过直接变量）并返回旧的qset值（通过返回值）
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11)
// 设置新的qset值（通过直接变量）并返回旧的qset值（通过返回值）
#define SCULL_IOCHQSET _IO(SCULL_IOC_MAGIC, 12)

// scullp相关的两个ioctl命令
// 因为代码比较简单，所以放在scull_ioctl函数一起统一处理，否则又要写一个ioctl处理函数
// 设置新的SCULL_P_BUFFER值（通过直接变量）
#define SCULL_P_IOCTSIZE _IO(SCULL_IOC_MAGIC, 13)
// 获得当前的SCULL_P_BUFFER值（通过返回值）
#define SCULL_P_IOCQSIZE _IO(SCULL_IOC_MAGIC, 14)

#define SCULL_IOC_MAXNR 14

#ifndef SCULL_P_NR_DEVS
#define SCULL_P_NR_DEVS 4
#endif

#ifndef SCULL_P_BUFFER
#define SCULL_P_BUFFER 4096
#endif

struct scull_pipe {
    wait_queue_head_t inq, outq;        // 读者和写者的等待队列头
    char *buffer, *end;                 // 缓冲区的起始和终止位置
    int buffersize;                     // 缓冲区大小，用于指针计算
    char *rp, *wp;                      // 缓冲区当前读写位置
    int nreaders, nwriters;             // 读者和写者的数量
    struct fasync_struct *async_queue;  // 异步队列
    struct semaphore sem;               // 互斥锁
    struct cdev cdev;                   // 字符设备
};

extern int scull_p_buffer;

int scull_p_init(dev_t dev);
void scull_p_cleanup(void);

int scull_p_open(struct inode *inode, struct file *filp);
int scull_p_release(struct inode *inode, struct file *filp);
ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count,
                     loff_t *f_pos);
ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count,
                      loff_t *f_pos);
unsigned int scull_p_poll(struct file *filp, poll_table *wait);
int scull_p_fasync(int fd, struct file *filp, int mode);
#endif