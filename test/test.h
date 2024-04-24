#ifndef TEST_H
#define TEST_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define DEVICE "/dev/scull0"
#define PIPE_DEVICE "/dev/scullpipe0"
#define TIMEOUT_SECONDS 5

#define SCULL_ASSERT(expr) ScullAssert(__FILE__, __LINE__, (expr), #expr)

void ScullAssert(const char* file, int line, bool test, const char* expr);

void ScullAssert(const char* file, int line, bool test, const char* expr) {
    if (!test) {
        fprintf(stderr,
                "=====================================\n"
                "Assert Fault!\n"
                "Location: test/%s:%d\n"
                "=====================================\n",
                file, line);
        exit(1);
    }
}

#define SCULL_IOC_MAGIC 'k'
#define SCULL_IOCRESET _IO(SCULL_IOC_MAGIC, 0)
#define SCULL_IOCSQUANTUM _IOW(SCULL_IOC_MAGIC, 1, int)
#define SCULL_IOCSQSET _IOW(SCULL_IOC_MAGIC, 2, int)
#define SCULL_IOCTQUANTUM _IO(SCULL_IOC_MAGIC, 3)
#define SCULL_IOCTQSET _IO(SCULL_IOC_MAGIC, 4)
#define SCULL_IOCGQUANTUM _IOR(SCULL_IOC_MAGIC, 5, int)
#define SCULL_IOCGQSET _IOR(SCULL_IOC_MAGIC, 6, int)
#define SCULL_IOCQQUANTUM _IO(SCULL_IOC_MAGIC, 7)
#define SCULL_IOCQQSET _IO(SCULL_IOC_MAGIC, 8)
#define SCULL_IOCXQUANTUM _IOWR(SCULL_IOC_MAGIC, 9, int)
#define SCULL_IOCXQSET _IOWR(SCULL_IOC_MAGIC, 10, int)
#define SCULL_IOCHQUANTUM _IO(SCULL_IOC_MAGIC, 11)
#define SCULL_IOCHQSET _IO(SCULL_IOC_MAGIC, 12)
#define SCULL_P_IOCTSIZE _IO(SCULL_IOC_MAGIC, 13)
#define SCULL_P_IOCQSIZE _IO(SCULL_IOC_MAGIC, 14)

#define SCULL_IOC_MAXNR 14

#ifndef SCULL_P_BUFFER
#define SCULL_P_BUFFER 4096
#endif

#endif