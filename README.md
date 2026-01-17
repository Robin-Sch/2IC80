# BISON Attack Audio Injection Demo App

This is a small demo application written in Node.js that demonstrates the BISON attack for audio injection. It connects to Alice, Bob and Mallory devices via serial ports. The app has 3 working modes:
- `--mode=alice` - connects to Alice device and plays the default audio stream
- `--mode=bob` - connects to Bob device and plays the audio received from Alice on speakers
- `--mode=mallory` - connects to Mallory device and injects audio into

## Setup
1. Clone this repository
2. Install [Node.js](https://nodejs.org/en) and [ffmpeg](https://www.ffmpeg.org/) (used for audio format conversion)
3. Put the audio files in the `assets/` folder (see `assets/README.md` for details)
4. `npm run install` to install dependencies
5. Connect the devices to your computer via USB
6. Run `node index.js --mode=alice|bob|mallory --port=x` (replace `x` with the actual serial port of the device, it can be e.g. `/dev/ttyACM0` on Linux, `COM3` on Windows or `/dev/tty.usbmodem2101` on macOS)