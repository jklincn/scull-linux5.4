obj-m	:= scull.o

scull-objs := src/main.o src/pipe.o

CFLAGS=-Wall -std=c11

modules:
	make -j $(nproc) -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

load:
	scripts/load.sh

remove:
	scripts/remove.sh

test:
	scripts/test.sh

install: clean modules remove load
	@scripts/test.sh \
	&& echo "\nScull installation successful!\n" \
	|| echo "\nScull installation failed!\n"
	
clean:
	make -j $(nproc) -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	make -C test clean

.PHONY: modules load remove test install clean