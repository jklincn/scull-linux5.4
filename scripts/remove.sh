#!/bin/bash

module="scull"
device="scull"

lsmod | grep -q $module || exit 0

sudo /sbin/rmmod $module $* || exit 1

sudo rm -f /dev/${device} /dev/${device}[0-3] 

sudo rm -f /dev/${device} /dev/${device}pipe[0-3] 





