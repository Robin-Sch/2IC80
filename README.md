![Demo Setup](assets/demo1.jpeg)

## Setup steps 

Assumed hardware: 3x ProMicro nRF52840 (alice, bob, mallory)

Below instructions are tested on Ubuntu 24.04 with Python 3.12

1. Install required packages: 
    ```bash
    sudo apt install --no-install-recommends git cmake ninja-build gperf   ccache dfu-util device-tree-compiler wget   python3-dev python3-pip python3-setuptools python3-tk python3-wheel xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1
    ```
2. Downalod and install JLink: https://www.segger.com/downloads/jlink/JLink_Linux_x86_64.deb
3. Initialize Zephyr workspace:
    ```bash
    mkdir ~/zephyr
    cd ~/zephyr
    git clone https://github.com/Robin-Sch/2IC80.git
    west init -l 2IC80
    cd 2IC80
    west update
    ```
4. Create python virtual environment, install `west` and required python packages:
    ```bash
    python3.12 -m venv ~/zephyr/venv 
    source ~/zephyr/venv/bin/activate
    pip install -U west
    pip install -r ~/zephyrproject/scripts/requirements.txt
5. Build and flash Alice and Bob:
    ```bash
    cd ~/zephyr/2IC80/samples/bluetooth/iso_broadcast # Alice
    west build -p -b promicro_nrf52840
    # Connect Alice ProMicro via USB and turn it into setup mode by shorting RST and GND pins twice quickly
    fdisk -l # check which /dev/sdX is the ProMicro
    mount /dev/sdX /mnt
    cp build/zephyr/zephyr.uf2 /mnt && sync # Device will reboot automatically
    
    cd ~/zephyr/2IC80/samples/bluetooth/iso_receive # Bob
    # repat the steps for Bob
    ```
6. Open Minicom to see the output from Bob:
    ```bash
    git checkout mallory # switch to Mallory branch (necessary because fof BT driver changes)
    cd ~/zephyr/2IC80/samples/bluetooth/iso_receive # Mallory is based on Bob's code
    # repeat build and flash steps for Mallory (from step 5)
    ```
7. Connect Alice and Bob via USB:
    ```bash
    # Connect Alice
    dmesg | grep ttyACM # check which /dev/ttyACMX is Alice
    minicom -D /dev/ttyACMX -b 115200
    # In another terminal, connect Bob
    dmesg | grep ttyACM # check which /dev/ttyACMY is Bob
    minicom -D /dev/ttyACMY -b 115200
    # You should see output on Bob's terminal showing received ISO packets and being in sync with Alice's output
    ```
8. Connect Mallory via USB and observe the output:
    ```bash
    # Connect Mallory
    dmesg | grep ttyACM # check which /dev/ttyACMZ is Mallory
    minicom -D /dev/ttyACMZ -b 115200
    ```
9. Bring Mallory slightly closer to Bob and move Alice away. Observe how Bob starts printing 666 indicating payload injection due to Mallory's attack.
