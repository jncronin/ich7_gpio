obj-m += ich7_gpio.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

modules:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

modules_install:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules_install

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


