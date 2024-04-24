#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include "scull.h"

static int scull_p_nr_devs = SCULL_P_NR_DEVS;  // 管道设备的数量
int scull_p_buffer = SCULL_P_BUFFER;           // 缓冲区大小
dev_t scull_p_devno;

module_param(scull_p_nr_devs, int, 0);
module_param(scull_p_buffer, int, 0);

static struct scull_pipe *scull_p_devices;

struct file_operations scull_pipe_fops = {
    .owner = THIS_MODULE,
    .llseek = no_llseek,
    .read = scull_p_read,
    .write = scull_p_write,
    .poll = scull_p_poll,
    .unlocked_ioctl = scull_ioctl,
    .open = scull_p_open,
    .release = scull_p_release,
    .fasync = scull_p_fasync,
};

int scull_p_open(struct inode *inode, struct file *filp) {
    struct scull_pipe *dev;

    dev = container_of(inode->i_cdev, struct scull_pipe, cdev);
    filp->private_data = dev;

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    if (!dev->buffer) {
        // 分配缓冲区
        dev->buffer = kmalloc(scull_p_buffer, GFP_KERNEL);
        if (!dev->buffer) {
            up(&dev->sem);
            return -ENOMEM;
        }
    }

    // 初始化缓冲区大小、起始末尾位置、读写位置
    dev->buffersize = scull_p_buffer;
    dev->end = dev->buffer + dev->buffersize;
    dev->rp = dev->wp = dev->buffer;
    // 更新读者、写者计数
    if (filp->f_mode & FMODE_READ) dev->nreaders++;
    if (filp->f_mode & FMODE_WRITE) dev->nwriters++;
    up(&dev->sem);

    // 调用 nonseekable_open 函数，标记文件为不支持寻址操作，即不支持随机访问
    return nonseekable_open(inode, filp);
}

int scull_p_release(struct inode *inode, struct file *filp) {
    struct scull_pipe *dev = filp->private_data;

    // 当设备关闭时，需要从异步队列中删除
    scull_p_fasync(-1, filp, 0);
    down(&dev->sem);
    if (filp->f_mode & FMODE_READ) dev->nreaders--;
    if (filp->f_mode & FMODE_WRITE) dev->nwriters--;
    // 当读者和写者数量均为0时，释放缓冲区
    if (dev->nreaders + dev->nwriters == 0) {
        kfree(dev->buffer);
        dev->buffer = NULL;  // 以便打开时确认是否分配缓冲区
    }
    up(&dev->sem);
    return 0;
}

// 管道数据读取
ssize_t scull_p_read(struct file *filp, char __user *buf, size_t count,
                     loff_t *f_pos) {
    struct scull_pipe *dev = filp->private_data;

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    while (dev->rp == dev->wp) {  // 缓冲区为空（没有可读取的数据）
        up(&dev->sem);
        // 如果是非阻塞则返回错误
        if (filp->f_flags & O_NONBLOCK) return -EAGAIN;
        // 等待缓冲区有数据
        printk(KERN_INFO "[scull] pipe reader waiting...");
        if (wait_event_interruptible(dev->inq, (dev->rp != dev->wp)))
            // 如果等待中被信号打断，则交给上层VFS来处理
            return -ERESTARTSYS;
        // 重新获得锁，循环
        if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    }

    // 已拿到互斥锁，并且缓冲区中有数据

    // 计算可以读取的数据量。如果写指针在读指针后面，直接读取这段数据
    // 如果写指针已经绕回（缓冲区是循环使用的），则读取到缓冲区末尾的数据。
    if (dev->wp > dev->rp)
        count = min(count, (size_t)(dev->wp - dev->rp));
    else
        count = min(count, (size_t)(dev->end - dev->rp));
    if (copy_to_user(buf, dev->rp, count)) {
        up(&dev->sem);
        return -EFAULT;
    }
    dev->rp += count;
    // 更新读指针，如果读到了缓冲区的末尾，则将读指针重置到缓冲区的开始
    if (dev->rp == dev->end) dev->rp = dev->buffer;
    up(&dev->sem);
    // 唤醒写进程
    wake_up_interruptible(&dev->outq);
    return count;
}

// 计算剩余空间
static int spacefree(struct scull_pipe *dev) {
    // 如果读指针和写指针相等，表示缓冲区为空。因此，可用空间为缓冲区的总大小减去1。
    // 这里减1是为了避免写指针追上读指针，这种情况通常被用来区分“满”和“空”的状态。
    if (dev->rp == dev->wp) return dev->buffersize - 1;
    return ((dev->rp + dev->buffersize - dev->wp) % dev->buffersize) - 1;
}

// 等待有剩余空间可以写入，调用者必须持有互斥锁。在发生错误返回前，互斥锁会先被释放
static int scull_getwritespace(struct scull_pipe *dev, struct file *filp) {
    while (spacefree(dev) == 0) {  // 检查缓冲区空间
        // 定义一个等待队列
        DEFINE_WAIT(wait);
        up(&dev->sem);
        // 如果是非阻塞，并且缓冲区已满，直接返回错误
        if (filp->f_flags & O_NONBLOCK) return -EAGAIN;
        // 准备等待
        // 1. 将当前进程添加到设备的等待队列 dev->outq 中
        // 2. 设置进程状态为 TASK_INTERRUPTIBLE
        prepare_to_wait(&dev->outq, &wait, TASK_INTERRUPTIBLE);
        // 放弃执行，重新调度，开始睡眠

        if (spacefree(dev) == 0) {
            printk(KERN_INFO "[scull] pipe writer waiting...");
            schedule();
        }
        // 从等待队列中移除当前进程，恢复正常的进程状态
        finish_wait(&dev->outq, &wait);
        // 如果在等待过程中有信号发送到当前进程，则返回-ERESTARTSYS以通知文件系统层需要处理这个信号
        if (signal_pending(current)) return -ERESTARTSYS;
        if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    }
    return 0;
}

// 管道数据写入
ssize_t scull_p_write(struct file *filp, const char __user *buf, size_t count,
                      loff_t *f_pos) {
    struct scull_pipe *dev = filp->private_data;
    int result;

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    // 确保有空间可以写入
    result = scull_getwritespace(dev, filp);
    if (result) return result;  // scull_getwritespace中已经调用了up(&dev->sem);

    // 计算实际写入量
    count = min(count, (size_t)spacefree(dev));
    if (dev->wp >= dev->rp)
        // 最多写到缓冲区末尾
        count = min(count, (size_t)(dev->end - dev->wp));
    else
        // 环形缓冲区处理
        count = min(count, (size_t)(dev->rp - dev->wp - 1));

    // 实际的数据写入
    if (copy_from_user(dev->wp, buf, count)) {
        up(&dev->sem);
        return -EFAULT;
    }
    // 更新写指针
    dev->wp += count;
    if (dev->wp == dev->end) dev->wp = dev->buffer;
    up(&dev->sem);

    // 唤醒读进程
    wake_up_interruptible(&dev->inq);

    // 如果有注册异步通知的进程，通知它们现在可以进行读操作
    if (dev->async_queue) kill_fasync(&dev->async_queue, SIGIO, POLL_IN);
    return count;
}

unsigned int scull_p_poll(struct file *filp, poll_table *wait) {
    struct scull_pipe *dev = filp->private_data;
    // 表明设备的当前状态
    unsigned int mask = 0;

    down(&dev->sem);
    // 注册等待队列
    poll_wait(filp, &dev->inq, wait);
    poll_wait(filp, &dev->outq, wait);
    // 检查是否可读
    if (dev->rp != dev->wp) mask |= POLLIN | POLLRDNORM;
    // 检查是否可写
    if (spacefree(dev)) mask |= POLLOUT | POLLWRNORM;
    up(&dev->sem);
    return mask;

    // 缺少了文件尾(end-of-file)的支持和处理
}

// 管理异步通知队列
int scull_p_fasync(int fd, struct file *filp, int mode) {
    struct scull_pipe *dev = filp->private_data;
    // mode!=0时，将进程加入队列；mode=0时，从队列中删除进程
    return fasync_helper(fd, filp, mode, &dev->async_queue);
}

static void scull_p_setup_cdev(struct scull_pipe *dev, int index) {
    int err, devno = scull_p_devno + index;

    cdev_init(&dev->cdev, &scull_pipe_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err) printk(KERN_NOTICE "Error %d adding scullpipe%d", err, index);
}

// 初始化管道设备，返回管道设备数量
int scull_p_init(dev_t firstdev) {
    int i, result;

    // 分配设备编号
    result = register_chrdev_region(firstdev, scull_p_nr_devs, "scullp");
    if (result < 0) {
        printk(KERN_NOTICE "Unable to get scullp region, error %d\n", result);
        return 0;
    }

    scull_p_devno = firstdev;

    // 分配结构体空间
    scull_p_devices =
        kmalloc(scull_p_nr_devs * sizeof(struct scull_pipe), GFP_KERNEL);
    if (scull_p_devices == NULL) {
        unregister_chrdev_region(firstdev, scull_p_nr_devs);
        return 0;
    }
    memset(scull_p_devices, 0, scull_p_nr_devs * sizeof(struct scull_pipe));

    // 初始化结构体
    for (i = 0; i < scull_p_nr_devs; i++) {
        init_waitqueue_head(&(scull_p_devices[i].inq));
        init_waitqueue_head(&(scull_p_devices[i].outq));
        sema_init(&scull_p_devices[i].sem, 1);
        scull_p_setup_cdev(scull_p_devices + i, i);
    }

    return scull_p_nr_devs;
}

// 管道设备清理函数
void scull_p_cleanup(void) {
    int i;

    if (!scull_p_devices) return;

    for (i = 0; i < scull_p_nr_devs; i++) {
        cdev_del(&scull_p_devices[i].cdev);
        kfree(scull_p_devices[i].buffer);
    }
    kfree(scull_p_devices);
    unregister_chrdev_region(scull_p_devno, scull_p_nr_devs);
    scull_p_devices = NULL;  // 好习惯
}
