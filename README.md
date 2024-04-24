# Scull-Linux5.4

Scull (Simple Character Utility for Loading Localities) is a char driver that acts on a memory area as though it were a device. It's an example driver in LDD3 (Linux Device Drivers, Third Edition). 

This project has implemented Scull on Linux 5.4 (Ubuntu 20.04) and written some functional testing programs (see [test](https://github.com/jklincn/scull-linux5.4/tree/master/test)).

## Quick Start

```
git clone https://github.com/jklincn/scull-linux5.4.git
cd scull-linux5.4
make install
```

It will compile the scull module and insert it into the kernel, and then automatically run the test program.

For details see [Makefile](Makefile).

