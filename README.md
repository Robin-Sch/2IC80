Report can be found in the Overleaf.

There are 4 branches:
- BISON Attack
    - [main](https://github.com/Robin-Sch/2IC80/tree/main) (Alice+Bob): modified code for  for Alice ([iso_broadcast](https://github.com/Robin-Sch/2IC80/tree/main/samples/bluetooth/iso_broadcast)) and Bob ([iso_receive](https://github.com/Robin-Sch/2IC80/tree/main/samples/bluetooth/iso_receive)) to perform audio streaming demo
    - [mallory](https://github.com/Robin-Sch/2IC80/tree/mallory): modified code for Mallory ([iso_receive](https://github.com/Robin-Sch/2IC80/tree/mallory/samples/bluetooth/iso_receive)) and low-level bluetooth driver changes ([1](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_adv_iso.c), [2](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_sync.c), [3](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/ull_sync_iso.c), [4](https://github.com/Robin-Sch/2IC80/tree/mallory/subsys/bluetooth/controller/ll_sw/nordic/lll/lll_sync_iso.c)) to perform BISON attack
    - [bison-audio-demo](https://github.com/Robin-Sch/2IC80/tree/bison-audio-demo): Small Node.js app for audio injection demo with 3 modes for Alice (sending audio over UART), Bob (receiving audio over UART and playing it) and Mallory (performaing audio injection)
- Breaktooth Attack
    - [breaktooth](https://github.com/Robin-Sch/2IC80/tree/breaktooth): Breaktooth code changed to allow for Link Key attack