include config.mk

.DEFAULT_GOAL := help

.PHONY: help
help:   ## Show command list
	@grep -E '^[[:alnum:]_/-]+:.*?## .*$$' Makefile | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'

.PHONY: install/deps
install/deps:	## Setup dependency
	@sh ./cmd/installer/go_installer.sh 
	@sudo apt update
	@sudo apt upgrade -y
	@sudo apt install -y bluetooth libbluetooth-dev
	@sudo apt install -y bluez bluez-tools bluez-firmware
	@sudo apt install -y python3 python3-pip python3-dev
	@sudo apt install -y python3-dbus python3-evdev python3-bluez python3-gi
	@sudo pip3 install pyudev
	@sudo pip3 install colorama

.PHONY: setup/device
setup/device:	## Setup Raspberry Pi env.
	@go build -o chg_bt_addr ./cmd/configer/main.go
	@./cmd/generators/config.mk_generator.sh "${TARGET}"
	@./cmd/generators/machine-info_generator.sh "${NAME}" \
	&& sudo mv ./machine-info /etc/
	@./chg_bt_addr -addr ${MAC}
	@rm -rf ./chg_bt_addr
	@sudo cp ./conf/dbus/org.trapedev.btkbservice.conf /etc/dbus-1/system.d
	@sudo cp ./conf/service/bluetooth.service /lib/systemd/bluetooth.service
	@sudo systemctl daemon-reload
	@sudo /etc/init.d/bluetooth start
	@sudo systemctl restart bluetooth.service
	@sleep 5
	@sudo hciconfig hci0 class 0x002540

.PHONY: rename/device
rename/device:	## Change bluetooth device name
	@./machine-info_generator.sh "${NAME}" \
	&& sudo mv ./machine-info /etc/

.PHONY: boot/kb/server
boot/kb/server:	## Boot DBus server for keyboard emulator
	@sudo hciconfig hci0 down \
	&& sudo systemctl daemon-reload \
	&& sudo /etc/init.d/bluetooth start \
	&& sudo hciconfig hci0 up \
	&& sudo hciconfig hci0 class 0x002540 \
	&& sudo hciconfig hci0 piscan \
	&& sudo ./cmd/attacker/boot_kb_server.py ${TARGET_MAC_ADDRESS}

.PHONY: boot/injector
boot/injector:	## Boot DBus client for keyboard emulator
	@sudo ./cmd/attacker/key_stroke_injector.py

.PHONY: breaktooth
breaktooth:	## Launching Breaktooth
	@sudo hciconfig hci0 class 0x002540
	@sudo ./cmd/attacker/breaktooth.py ${TARGET_MAC_ADDRESS}
