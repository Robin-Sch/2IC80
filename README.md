# 2IC80 2025/26 - Group 47

## Navigation
There are 4 branches:
- BISON Attack
    - [main](https://github.com/Robin-Sch/2IC80/tree/main) (Alice+Bob): modified code for  for Alice ([iso_broadcast](https://github.com/Robin-Sch/2IC80/tree/main/samples/bluetooth/iso_broadcast)) and Bob ([iso_receive](https://github.com/Robin-Sch/2IC80/tree/main/samples/bluetooth/iso_receive)) to perform audio streaming demo
    - [mallory](https://github.com/Robin-Sch/2IC80/tree/mallory): modified code for Mallory ([iso_receive](https://github.com/Robin-Sch/2IC80/tree/mallory/samples/bluetooth/iso_receive)) and low-level bluetooth driver changes ([1](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_adv_iso.c), [2](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_sync.c), [3](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_sync_iso.c), [4](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_sync_iso.c)) to perform BISON attack
    - [bison-audio-demo](https://github.com/Robin-Sch/2IC80/tree/bison-audio-demo): Small Node.js app for audio injection demo with 3 modes for Alice (sending audio over UART), Bob (receiving audio over UART and playing it) and Mallory (performaing audio injection)
- Breaktooth Attack
    - [breaktooth](https://github.com/Robin-Sch/2IC80/tree/breaktooth): Breaktooth code changed to allow for Link Key attack


## BISON Configuration

Assumed hardware: 3x ProMicro nRF52840 (alice, bob, mallory)

Below instructions are tested on Ubuntu 24.04 with Python 3.12 and Node.js v25.3.0

1. Install required packages: 
    ```bash
    sudo apt install --no-install-recommends git cmake ninja-build gperf ccache dfu-util device-tree-compiler wget python3-dev python3-pip python3-setuptools python3-tk python3-wheel xz-utils file make gcc gcc-multilib g++-multilib libsdl2-dev libmagic1 ffmpeg nodejs
    ```
2. Downalod and install JLink: https://www.segger.com/downloads/jlink/JLink_Linux_x86_64.deb
3. Clone this repository:
    ```bash
    export BISON_PATH=~/zephyr # or any other path you prefer
    mkdir -p $BISON_PATH
    cd $BISON_PATH
    git clone https://github.com/Robin-Sch/2IC80.git
    ```
4. Create python virtual environment, install `west` and required python packages:
    ```bash
    python3.12 -m venv $BISON_PATH/venv 
    source $BISON_PATH/venv/bin/activate
    pip install -U west
    pip install -r $BISON_PATH/zephyrproject/scripts/requirements.txt
    ```
5. Initialize Zephyr workspace and update modules:
    ```bash
    west init -l 2IC80
    cd 2IC80
    west update
    ```

6. Build and flash Alice and Bob:
    ```bash
    cd $BISON_PATH/2IC80/samples/bluetooth/iso_broadcast # alice
    west build -p -b promicro_nrf52840
    # Connect Alice ProMicro via USB and turn it into setup mode by shorting RST and GND pins twice quickly
    fdisk -l # check which /dev/sdX is the ProMicro
    mount /dev/sdX /mnt
    cp build/zephyr/zephyr.uf2 /mnt && sync # device will reboot automatically
    
    cd $BISON_PATH/2IC80/samples/bluetooth/iso_receive # bob
    # repat the steps for Bob
    ```

7. Build and flash Mallory:
    ```bash
    git checkout mallory # switch to Mallory branch (necessary because of BT driver changes)
    cd $BISON_PATH/2IC80/samples/bluetooth/iso_receive # Mallory is based on Bob's code
    # repeat build and flash steps for Mallory (from step 6)
    ```
8. Connect all three boards to the host machine (you can also use multiple machines)
    
    We recommend to plug in the boards one by one and monitor `dmesg` output to identify which /dev/ttyACMx corresponds to which board. For the following steps, we assume:
    - Alice: /dev/ttyACM0
    - Bob: /dev/ttyACM1
    - Mallory: /dev/ttyACM2  

    Note that on different opeating systems those paths will be different (e.g. COMx on Windows or /dev/tty.usbmodemxxxx on MacOS)
    
9. Start the demo application
    ```bash
    cd $BISON_PATH/2IC80
    git checkout bison-audio-demo
    npm install
    cd $BISON_PATH/2IC80/assets
    # Follow instructions in README.md file to prepare audio files
    cd $BISON_PATH/2IC80

    # In three separate terminal windows run:
    # Alice
    node index.js --mode=alice --port=/dev/ttyACM0
    # Bob
    node index.js --mode=bob --port=/dev/ttyACM1
    # Mallory
    node index.js --mode=mallory --port=/dev/ttyACM2
    ```
10. Notice that when Mallory starts, Bob's audio output changes to the injected audio stream.