TARGET_MODULE:=slabs-pwm-led
SRC := $(shell pwd)
$(TARGET_MODULE)-objs := led_driver.o
obj-m := $(TARGET_MODULE).o

KERNEL_DIR=/home/patlas/Repositories/yocto_image/kernel/linux-4.9.11
KERNEL_INC=$(KERNEL_DIR)/include

all :
	$(MAKE) -C $(KERNEL_DIR) M=$(SRC) modules
modules_install:
	$(MAKE) -C $(KERNEL_DIR) M=$(SRC) modules_install
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(SRC) clean