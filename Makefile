obj-m += taas_driver.o
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CC := gcc
# Se eliminan flags de 32 bits (-mfpu, -mfloat-abi) incompatibles con aarch64
CFLAGS := -O3 -Wall -march=armv8-a+crc+crypto
LIBS := -lssl -lcrypto
NODE_BIN := taas_node

all: driver node

driver:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

node: taas_node.c
	$(CC) $(CFLAGS) taas_node.c -o $(NODE_BIN) $(LIBS)

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(NODE_BIN)

install:
	@echo "[+] Instalando Driver..."
	sudo mkdir -p /lib/modules/$(shell uname -r)/extra/
	sudo cp taas_driver.ko /lib/modules/$(shell uname -r)/extra/
	sudo depmod -a
	@echo "[+] Configurando reglas UDEV..."
	echo 'KERNEL=="taas_timer", MODE="0666"' | sudo tee /etc/udev/rules.d/99-taas.rules
	sudo udevadm control --reload-rules && sudo udevadm trigger
