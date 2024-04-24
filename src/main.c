#include <linux/fs.h>  // 包含了绝大部分函数
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/slab.h>  // 用于 kmalloc 函数
#include <linux/uaccess.h>  // 用于 copy_*_user 函数，原代码是 #include <asm/uaccess.h>

#include "scull.h"

int scull_major = SCULL_MAJOR;      // 设备主编号
int scull_minor = 0;                // 设备次编号
int scull_nr_devs = SCULL_NR_DEVS;  // 分配的设备数量
int scull_quantum = SCULL_QUANTUM;  // 每个 quantum 的字节数
int scull_qset = SCULL_QSET;        // 每个 qset 的数组长度

// 模块加载时可手动设置参数
module_param(scull_major, int, S_IRUGO);
module_param(scull_minor, int, S_IRUGO);
module_param(scull_nr_devs, int, S_IRUGO);
module_param(scull_quantum, int, S_IRUGO);
module_param(scull_qset, int, S_IRUGO);

struct scull_dev *scull_devices;

struct file_operations scull_fops = {
    .owner = THIS_MODULE,
    .llseek = scull_llseek,
    .read = scull_read,
    .write = scull_write,
    .unlocked_ioctl = scull_ioctl,
    .open = scull_open,
    .release = scull_release,
};

MODULE_LICENSE("Dual BSD/GPL");

// 设置字符设备
static void scull_setup_cdev(struct scull_dev *dev, int index) {
    int err, devno = MKDEV(scull_major, scull_minor + index);

    // 初始化字符设备
    cdev_init(&dev->cdev, &scull_fops);
    // 设置所有者和操作
    dev->cdev.owner = THIS_MODULE;
    // dev->cdev.ops = &scull_fops;  // 这行代码是多余的，cdev_init做了这个事情
    // 把字符设备加入到系统中
    err = cdev_add(&dev->cdev, devno, 1);

    if (err) printk(KERN_NOTICE "Error %d adding scull%d", err, index);
}

// 释放整个数据区
int scull_trim(struct scull_dev *dev) {
    struct scull_qset *next, *dptr;
    int qset = dev->qset;
    int i;

    // 遍历 qset 链表
    for (dptr = dev->data; dptr; dptr = next) {
        // 释放 qset 保存的数据
        if (dptr->data) {
            // 注意这里是一个二维数组的释放
            for (i = 0; i < qset; i++) kfree(dptr->data[i]);
            kfree(dptr->data);
            dptr->data = NULL;
        }
        next = dptr->next;
        // 释放 qset
        kfree(dptr);
    }
    dev->size = 0;
    dev->data = NULL;
    return 0;
}

int scull_open(struct inode *inode, struct file *filp) {
    struct scull_dev *dev;

    // 使用宏来找到cdev对应的dev结构体
    dev = container_of(inode->i_cdev, struct scull_dev, cdev);
    // 存储指针，方便以后存取
    filp->private_data = dev;

    // 如果以写入方式打开，则将设备的数据长度截取为0，即清空设备数据。
    if ((filp->f_flags & O_ACCMODE) == O_WRONLY) {
        if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
        scull_trim(dev);
        up(&dev->sem);
    }
    return 0;
}

// 文件释放时使用这个函数，一般用于关闭硬件，由于scull没有硬件，因此直接返回0
int scull_release(struct inode *inode, struct file *filp) { return 0; }

// 定位到指定的量子集合
struct scull_qset *scull_follow(struct scull_dev *dev, int n) {
    struct scull_qset *qs = dev->data;

    // 如果当前设备还没有量子集合，则先分配一个
    if (!qs) {
        qs = dev->data = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
        if (qs == NULL) return NULL;
        memset(qs, 0, sizeof(struct scull_qset));
    }

    // 寻找下一个量子集合，如果不存在也分配一个
    while (n--) {
        if (!qs->next) {
            qs->next = kmalloc(sizeof(struct scull_qset), GFP_KERNEL);
            if (qs->next == NULL) return NULL;
            memset(qs->next, 0, sizeof(struct scull_qset));
        }
        qs = qs->next;
        continue;
    }
    return qs;
}

// 从scull的内存区域中读取数据
ssize_t scull_read(struct file *filp, char __user *buf, size_t count,
                   loff_t *f_pos) {
    // 从 private_data 中得到 scull_dev 结构体
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    // 获得两个大小常量
    int quantum = dev->quantum, qset = dev->qset;
    // 计算每个量子集合可以保存的数据大小
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = 0;

    // 获取信号量，可中断
    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;
    // 如果偏移量大于当前设备的数据长度，则错误。
    // 比如总数据量只有 100 字节，但读了第 120 个字节
    if (*f_pos >= dev->size) goto out;
    // 如果读取的长度超过了当前设备的数据长度，则截断。
    // 比如总数据量是100，当前偏移量是90，但要读的长度是20，那么110超过了总长度，因此把要读的长度修改为10
    if (*f_pos + count > dev->size) count = dev->size - *f_pos;

    // 计算要读取的数据的位置
    // 假设f_pos=4100200，itemsize=4000000，qset=1000
    // 则item=1，表示第2个量子集合。
    // rest=100200，表示第2个量子集合内的偏移量
    // s_pos=100，表示在第2个量子集合的第100个量子内
    // q_pos=200，表示在第2个量子集合的第100个量子的第200字节位置
    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;

    // 根据上文假设，此处是找到第2个量子集合
    dptr = scull_follow(dev, item);

    // 三个出错情况：
    // 1. 找不到指定量子集合，这一般是kmalloc的错误
    // 2. 该量子集合没有数据保存，这一般是数据长度有误
    // 3. 该量子集合的对应量子没有数据，这一般也是数据长度有误
    if (dptr == NULL || !dptr->data || !dptr->data[s_pos]) goto out;

    // 只处理单个量子的数据，即不跨量子读取数据
    // 比如单个量子最多保存4000字节数据，当前偏移量处于3900，读的长度是200，则把要读的长度修改为100
    if (count > quantum - q_pos) count = quantum - q_pos;

    // 把内核空间以dptr->data[s_pos] + q_pos为起始地址，复制count字节到用户空间的buf中
    // copy_to_user 会进行地址空间的转换
    if (copy_to_user(buf, dptr->data[s_pos] + q_pos, count)) {
        retval = -EFAULT;
        goto out;
    }
    // 修改当前偏移量
    *f_pos += count;
    retval = count;

out:
    // 释放信号量
    up(&dev->sem);
    return retval;
}

// 往scull的内存区域中写入数据，除了在没有数据区域时要创建之外，其他过程和read基本一致，注释见read
ssize_t scull_write(struct file *filp, const char __user *buf, size_t count,
                    loff_t *f_pos) {
    struct scull_dev *dev = filp->private_data;
    struct scull_qset *dptr;
    int quantum = dev->quantum, qset = dev->qset;
    int itemsize = quantum * qset;
    int item, s_pos, q_pos, rest;
    ssize_t retval = -ENOMEM;  // 默认返回值

    if (down_interruptible(&dev->sem)) return -ERESTARTSYS;

    item = (long)*f_pos / itemsize;
    rest = (long)*f_pos % itemsize;
    s_pos = rest / quantum;
    q_pos = rest % quantum;
    dptr = scull_follow(dev, item);
    if (dptr == NULL) goto out;
    // 创建一个量子集合的数据区域
    if (!dptr->data) {
        dptr->data = kmalloc(qset * sizeof(char *), GFP_KERNEL);
        if (!dptr->data) goto out;
        memset(dptr->data, 0, qset * sizeof(char *));
    }
    // 创建一个量子的数据区域
    if (!dptr->data[s_pos]) {
        dptr->data[s_pos] = kmalloc(quantum, GFP_KERNEL);
        if (!dptr->data[s_pos]) goto out;
        // 这里相较于原代码补充了一个memset
        memset(dptr->data[s_pos], 0, quantum);
    }

    if (count > quantum - q_pos) count = quantum - q_pos;

    if (copy_from_user(dptr->data[s_pos] + q_pos, buf, count)) {
        retval = -EFAULT;
        goto out;
    }
    *f_pos += count;
    retval = count;

    // 更新设备保存的数据大小
    if (dev->size < *f_pos) dev->size = *f_pos;

out:
    up(&dev->sem);
    return retval;
}

long scull_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {
    int err = 0, tmp;
    int retval = 0;

    // 提取类型和顺序号，如果有错误则返回
    if (_IOC_TYPE(cmd) != SCULL_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > SCULL_IOC_MAXNR) return -ENOTTY;

    // 新版access_ok只需要两个参数，移除了对读写的判断
    err = !access_ok((void __user *)arg, _IOC_SIZE(cmd));
    if (err) return -EFAULT;

    switch (cmd) {
        case SCULL_IOCRESET:
            scull_quantum = SCULL_QUANTUM;
            scull_qset = SCULL_QSET;
            break;

        case SCULL_IOCSQUANTUM:
            // 检查权限，下同
            // 可以把Makefile中运行测试程序的sudo去掉，以此来测试权限管理
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            retval = __get_user(scull_quantum, (int __user *)arg);
            break;

        case SCULL_IOCTQUANTUM:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            scull_quantum = arg;
            break;

        case SCULL_IOCGQUANTUM:
            retval = __put_user(scull_quantum, (int __user *)arg);
            break;

        case SCULL_IOCQQUANTUM:
            return scull_quantum;

        case SCULL_IOCXQUANTUM:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            tmp = scull_quantum;
            retval = __get_user(scull_quantum, (int __user *)arg);
            if (retval == 0) retval = __put_user(tmp, (int __user *)arg);
            break;

        case SCULL_IOCHQUANTUM:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            tmp = scull_quantum;
            scull_quantum = arg;
            return tmp;

        case SCULL_IOCSQSET:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            retval = __get_user(scull_qset, (int __user *)arg);
            break;

        case SCULL_IOCTQSET:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            scull_qset = arg;
            break;

        case SCULL_IOCGQSET:
            retval = __put_user(scull_qset, (int __user *)arg);
            break;

        case SCULL_IOCQQSET:
            return scull_qset;

        case SCULL_IOCXQSET:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            tmp = scull_qset;
            retval = __get_user(scull_qset, (int __user *)arg);
            if (retval == 0) retval = put_user(tmp, (int __user *)arg);
            break;

        case SCULL_IOCHQSET:
            if (!capable(CAP_SYS_ADMIN)) return -EPERM;
            tmp = scull_qset;
            scull_qset = arg;
            return tmp;

        case SCULL_P_IOCTSIZE:
            scull_p_buffer = arg;
            break;

        case SCULL_P_IOCQSIZE:
            return scull_p_buffer;

        default:  // 多余的，因为检查了 _IOC_NR(cmd)
            return -ENOTTY;
    }
    return retval;
}

loff_t scull_llseek(struct file *filp, loff_t off, int whence) {
    struct scull_dev *dev = filp->private_data;
    loff_t newpos;

    switch (whence) {
        case 0:  // SEEK_SET
            newpos = off;
            break;

        case 1:  // SEEK_CUR
            newpos = filp->f_pos + off;
            break;

        case 2:  // SEEK_END
            newpos = dev->size + off;
            break;

        default:  // can't happen
            return -EINVAL;
    }
    if (newpos < 0) return -EINVAL;
    filp->f_pos = newpos;
    return newpos;
}

static void scull_cleanup_module(void) {
    int i;
    dev_t devno = MKDEV(scull_major, scull_minor);

    if (scull_devices) {
        for (i = 0; i < scull_nr_devs; i++) {
            scull_trim(scull_devices + i);
            cdev_del(&scull_devices[i].cdev);
        }
        kfree(scull_devices);
    }

    // 注销字符设备
    unregister_chrdev_region(devno, scull_nr_devs);

    // 清理其他关联设备
    scull_p_cleanup();
    // scull_access_cleanup();

    printk(KERN_ALERT "[scull] Goodbye, cruel world\n");
}

static int scull_init_module(void) {
    int result, i;
    dev_t dev = 0;

    if (scull_major) {
        // 已手动指定设备编号
        dev = MKDEV(scull_major, scull_minor);
        result = register_chrdev_region(dev, scull_nr_devs, "scull");
    } else {
        // 动态分配设备编号(推荐)
        result = alloc_chrdev_region(&dev, scull_minor, scull_nr_devs, "scull");
        scull_major = MAJOR(dev);
    }

    if (result < 0) {
        printk(KERN_WARNING "scull: can't get major %d\n", scull_major);
        return result;
    }

    // 分配 dev 结构体的内存空间
    scull_devices =
        kmalloc(scull_nr_devs * sizeof(struct scull_dev), GFP_KERNEL);
    if (!scull_devices) {
        result = -ENOMEM;
        goto fail;
    }
    memset(scull_devices, 0, scull_nr_devs * sizeof(struct scull_dev));

    // 初始化 dev 结构体
    for (i = 0; i < scull_nr_devs; i++) {
        // 设置两个和大小相关的常量
        scull_devices[i].quantum = scull_quantum;
        scull_devices[i].qset = scull_qset;
        // 初始化互斥锁，原代码是init_MUTEX(&scull_devices[i].sem);
        sema_init(&scull_devices[i].sem, 1);
        scull_setup_cdev(&scull_devices[i], i);
    }

    // 初始化其他关联设备
    dev = MKDEV(scull_major, scull_minor + scull_nr_devs);
    dev += scull_p_init(dev);
    // dev += scull_access_init(dev);

    printk(KERN_ALERT "[scull] Hello, world\n");
    return 0;

fail:
    scull_cleanup_module();
    return result;
}

module_init(scull_init_module);
module_exit(scull_cleanup_module);
