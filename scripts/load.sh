#!/bin/bash

module="scull"
device="scull"

set -e

# 检查模块是否加载
lsmod | grep -q scull && exit 1

# 加载模块
sudo /sbin/insmod ./$module.ko $* || exit 1

# 获取主编号
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)

# 创建scull
sudo rm -f /dev/${device}[0-3]
sudo mknod /dev/${device}0 c $major 0
sudo mknod /dev/${device}1 c $major 1
sudo mknod /dev/${device}2 c $major 2
sudo mknod /dev/${device}3 c $major 3

# 修改所有者
sudo -E chown $USER:$USER /dev/${device}[0-3]

# 创建scull pipe
sudo rm -f /dev/${device}pipe[0-3]
sudo mknod /dev/${device}pipe0 c $major 4
sudo mknod /dev/${device}pipe1 c $major 5
sudo mknod /dev/${device}pipe2 c $major 6
sudo mknod /dev/${device}pipe3 c $major 7

sudo -E chown $USER:$USER /dev/${device}pipe[0-3]



