# BreakTooth Description

## Overview

BreakTooth is an attack that exploits Bluetooth's power-saving mode, which causes connections to be temporarily disconnected or dropped, to overwrite Bluetooth BR/EDR device connections and hijack the operation of the devices to which they are connected.

## Experimental Devices

### Mallory

| Device Name  | Generation | Model |       OS        | System |                                         Debian version                                          | Kernel version |
| :----------: | :--------: | :---: | :-------------: | :----: | :---------------------------------------------------------------------------------------------: | :------------: |
| Raspberry Pi |    3rd     |  B+   | Raspberry Pi OS | 32bit  | 11 ([bullseye](https://www.raspberrypi.com/software/operating-systems/#raspberry-pi-os-legacy)) |      6.1       |
| Raspberry Pi |    4th     |   B   | Raspberry Pi OS | 32bit  | 11 ([bullseye](https://www.raspberrypi.com/software/operating-systems/#raspberry-pi-os-legacy)) |      6.1       |

### Alice

| Manufacturer  |         Device Name          | Device Type |        OS        |             BT Driver             |    BT Driver Version     |          BT Version          |     BT Name     |    BT Address     | Attack |
| :-----------: | :--------------------------: | :---------: | :--------------: | :-------------------------------: | :----------------------: | :--------------------------: | :-------------: | :---------------: | :----: |
|   Microsoft   |       Surface Laptop 4       |   Laptop    | Windows 11 Home  |  Intel(R) Wireless Bluetooth(R)   | HCI:11.8336/LMP:11.8336  |             5.0              | LAPTOP-VLKPUTIN | 8C:1D:96:10:77:D1 |   ✓    |
|   Microsoft   |       Surface Laptop 3       |   Laptop    | Windows 11 Home  |  Intel(R) Wireless Bluetooth(R)   | HCI:11.8207/LMP:11.8207  |             5.0              | DESKTOP-AH9N13U | C8:34:8E:08:A8:6D |   ✓    |
|  HP/TP-Link   | HP Pro SSF 400 G9 Desktop PC |   Desktop   |  Windows 11 Pro  | TP-Link Bluetooth 5.0 USB Adapter | HCI:9.55449/LMP: 9.25805 |             5.0              | DESKTOP-5C27L30 | E8:48:B8:C8:20:00 |   ✓    |
|     Sony      |    Sony Xperia 5 (901SO)     |    Phone    |  Android OS 10   |                 -                 |            -             |             5.0              |    Xperia 5     | 3C:01:EF:2F:6D:52 |   ✓    |
|    Google     |        Google Pixel 2        |    Phone    |  Android OS 10   |                 -                 |            -             |             5.0              |     Pixel 2     | 40:4E:36:D7:76:13 |   ✓    |
|    Samsung    |      Galaxy S8 (SVV36)       |    Phone    |   Android OS 9   |                 -                 |            -             |             5.0              |    Galaxy S8    | 94:7B:E7:F5:6B:16 |   ✓    |
| Google/HUAWEI |           Nexus 6P           |    Phone    | Android OS 8.1.0 |                 -                 |            -             |             4.0              |    Nexus 6P     | 50:68:0A:09:30:B2 |   ✓    |
|   Google/LG   |           Nexus 5            |    Phone    | Android OS 6.0.1 |                 -                 |            -             |             4.0              |     Nexus 5     | 34:FC:EF:11:D7:8E |   ✓    |
|    HUAWEI     |   HUAWEI MediaPad T2 8 Pro   |   Tablet    | Android OS 6.0.1 |                 -                 |            -             | 4.1 (3.0,2.1+EDR compatible) |     JDN-W09     | D0:65:CA:AC:9A:AA |   ✓    |
|     Apple     |          iPhone 11           |    Phone    |     iOS 16.2     |                 -                 |            -             |             5.0              |     iPhone      | FC:E2:6C:C2:AC:64 |   △    |

### Bob

| Manufacturer |  Device Model  | Device Type | BT Category |           BT Name            | BT Version | BT Class | Time to being into Power-Saving Mode | Attack |
| :----------: | :------------: | :---------: | :---------: | :--------------------------: | :--------: | :------: | :----------------------------------: | :----: |
|     Ewin     |    EW-B009     |  Keyboard   |   BR/EDR    |     Ewin BT5.1 Keyboard      |    5.1     |    -     |               10[min.]               |   ✓    |
|     Ewin     |   EW-ZR050B    |  Keyboard   |   BR/EDR    |     Ewin BT5.1 Keyboard      |    5.1     |    -     |               10[min.]               |   ✓    |
|     Ewin     |    EW-RB023    |  Keyboard   |   BR/EDR    |     BT 5.1 keyboard-1503     |    5.1     |    -     |               10[min.]               |   ✓    |
|    Earto     |   JP-B087-BK   |  Keyboard   |   BR/EDR    |    Bluetooth 5.1 Keyboard    |    5.1     |    -     |               10[min.]               |   ✓    |
|    ELECOM    |  TK-FMP101WH   |  Keyboard   |   BR/EDR    |       ELECOM TK-FBP101       |    3.0     | Class 2  |               30[min.]               |   ✓    |
|    ELECOM    | TK-FMP102SV/EC |  Keyboard   |   BR/EDR    |       ELECOM TK-FBP102       |    3.0     | Class 2  |               30[min.]               |   ✓    |
|   Buffalo    |   BSKBB315BK   |  Keyboard   |   BR/EDR    |     BUFFALO BT Keyboard      |    3.0     | Class 2  |               10[min.]               |   ✓    |
| SANWA SUPPLY |   400-SKB062   |  Keyboard   |   BR/EDR    |      SANWA BT KEYBOARD       |    3.0     | Class 2  |               10[min.]               |   ✓    |
|   Logicool   |     K380BK     |  Keyboard   |   BR/EDR    |        Keyboard K380         |    3.0     |    -     |     Unknown(more than 60[min.])      |   ×    |
|    Ajazz     |      308i      |  Keyboard   |   BR/EDR    | Ajazz Bluetooth 3.0 Keyboard |    3.0     |    -     |               15[min.]               |   ✓    |
|  YIXUANKEJI  |  KAWS510-10F   |  Keyboard   |   BR/EDR    |      Bluetooth Keyboard      |  Unknown   |    -     |               15[min.]               |   ✓    |
