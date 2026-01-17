const { SerialPort } = require('serialport');
const ffmpeg = require('fluent-ffmpeg');
const Speaker = require('speaker');
const fs = require('fs');
const path = require('path');
const minimist = require('minimist');

const args = minimist(process.argv.slice(2));
const mode = args.mode; // alice, bob, mallory
const portPath = args.port; // /dev/ttyACMx, COMx, /dev/tty.usbmodemxxxx
const baudRate = 1000000; // 1M baud for audio

console.log(`[INIT] Starting BISON Audio App in ${mode.toUpperCase()} mode`);
console.log(`[INIT] Serial port: ${portPath} @ ${baudRate} baud`);

const AUDIO_FORMAT = {
    channels: 1,
    bitDepth: 16,
    sampleRate: 8000
};

const BYTES_PER_SECOND = 8000 * 2; // 16000 bytes/sec
const CHUNK_SIZE = 160; // 10ms of audio
const CHUNK_INTERVAL_MS = 10; // Send every 10ms for real-time

// BOB MODE (RECEIVER)
if (mode === 'bob') {
    console.log('[BOB] Opening serial port...');
    const port = new SerialPort({ path: portPath, baudRate: baudRate });

    let speaker = null;
    let speakerReady = false;

    port.on('open', () => {
        console.log('[BOB] Serial port OPENED successfully');
        console.log('[BOB] Initializing speaker (this may take a moment on macOS)...');

        try {
            speaker = new Speaker(AUDIO_FORMAT);

            speaker.on('open', () => {
                console.log('[BOB] Speaker READY - listening for audio data...');
                speakerReady = true;
            });

            speaker.on('error', (err) => {
                console.error('[BOB] Speaker error:', err.message);
            });

            speaker.on('close', () => {
                console.log('[BOB] Speaker closed');
            });

            setTimeout(() => {
                if (!speakerReady) {
                    console.log('[BOB] Speaker assumed ready (open event not received)');
                    speakerReady = true;
                }
            }, 1000);

        } catch (err) {
            console.error('[BOB] Failed to create speaker:', err.message);
        }
    });

    let totalBytesReceived = 0;
    let lastLogTime = Date.now();

    const DEBUG_MARKERS = {
        0xAA: '[BOB] Firmware: Starting scan for Alice...',
        0xBB: '[BOB] Firmware: Found Alice!',
        0xCC: '[BOB] Firmware: PA Sync established',
        0xDD: '[BOB] Firmware: BIG Info received',
        0xEE: '[BOB] Firmware: ISO connected - audio starting!',
    };
    let lastMarker = 0;

    port.on('data', (chunk) => {
        totalBytesReceived += chunk.length;

        // Check for debug markers (4 identical bytes)
        if (chunk.length >= 4) {
            const first = chunk[0];
            if (DEBUG_MARKERS[first] &&
                chunk[1] === first && chunk[2] === first && chunk[3] === first) {
                if (first !== lastMarker) {
                    console.log(DEBUG_MARKERS[first]);
                    lastMarker = first;
                }
                // Skip marker bytes, process rest as audio
                chunk = chunk.slice(4);
                if (chunk.length === 0) return;
            }
        }

        // Log every second
        const now = Date.now();
        if (now - lastLogTime >= 1000) {
            const audioSecs = (totalBytesReceived / BYTES_PER_SECOND).toFixed(1);
            console.log(`[BOB] Received ${totalBytesReceived} bytes (~${audioSecs}s audio)`);
            lastLogTime = now;
        }

        // Write to speaker if available
        if (speaker && speakerReady) {
            try {
                speaker.write(chunk);
            } catch (err) {
                console.error('[BOB] Speaker write error:', err.message);
            }
        }
    });

    port.on('error', (err) => console.error('[BOB] Serial Error:', err.message));
    port.on('close', () => console.log('[BOB] Serial port CLOSED'));
}

// ALICE / MALLORY MODE (TRANSMITTER)
else if (mode === 'alice' || mode === 'mallory') {
    console.log(`[${mode.toUpperCase()}] Opening serial port...`);
    const port = new SerialPort({ path: portPath, baudRate: baudRate });

    const playlist = {
        alice: 'assets/default.mp3',
        mallory: 'assets/injection.mp3'
    };

    const track = (mode === 'alice') ? playlist.alice : playlist.mallory;
    const filePath = path.join(__dirname, track);

    if (!fs.existsSync(filePath)) {
        console.error(`[${mode.toUpperCase()}] Error: File not found: ${filePath}`);
        process.exit(1);
    }

    const stats = fs.statSync(filePath);
    console.log(`[${mode.toUpperCase()}] Track: ${track} (${(stats.size / 1024 / 1024).toFixed(2)} MB)`);

    port.on('open', () => {
        console.log(`[${mode.toUpperCase()}] Serial port OPENED - waiting for device...`);
        setTimeout(() => {
            console.log(`[${mode.toUpperCase()}] Starting REAL-TIME audio stream (${BYTES_PER_SECOND} bytes/sec)...`);
            playLoop();
        }, 2000);
    });

    port.on('error', (err) => console.error(`[${mode.toUpperCase()}] Serial Error:`, err.message));
    port.on('close', () => console.log(`[${mode.toUpperCase()}] Serial port CLOSED`));

    const DEBUG_MARKERS_TX = {
        0xA0: '[MALLORY] Firmware: Starting scan...',
        0xA1: '[MALLORY] Firmware: Found Alice!',
        0xA2: '[MALLORY] Firmware: PA Sync established',
        0xA3: '[MALLORY] Firmware: BIG Info received',
        0xA4: '[MALLORY] Firmware: Attack ACTIVE!',
        0xA5: '[MALLORY] Firmware: Audio buffer EMPTY (no UART data!)',
        0xA6: '[MALLORY] Firmware: Audio data RECEIVED from UART!'
    };
    let lastMarkerTx = 0;

    port.on('data', (chunk) => {
        // Check for debug markers (4 identical bytes)
        if (chunk.length >= 4) {
            const first = chunk[0];
            if (DEBUG_MARKERS_TX[first] &&
                chunk[1] === first && chunk[2] === first && chunk[3] === first) {
                if (first !== lastMarkerTx) {
                    console.log(DEBUG_MARKERS_TX[first]);
                    lastMarkerTx = first;
                }
            }
        }
    });

    let totalBytesSent = 0;
    let loopCount = 0;

    function playLoop() {
        loopCount++;
        console.log(`[${mode.toUpperCase()}] Starting loop #${loopCount}`);

        // Buffer to accumulate FFmpeg output
        let audioBuffer = Buffer.alloc(0);
        let bufferReadIndex = 0;
        let streamEnded = false;

        const command = ffmpeg(filePath)
            .format('s16le')
            .audioCodec('pcm_s16le')
            .audioChannels(1)
            .audioFrequency(8000)
            .on('start', () => {
                console.log(`[${mode.toUpperCase()}] FFmpeg started - buffering audio...`);
            })
            .on('error', (err, stdout, stderr) => {
                console.error(`[${mode.toUpperCase()}] FFmpeg Error:`, err.message);
            });

        const audioStream = command.pipe();

        // Accumulate all audio data into buffer
        audioStream.on('data', (chunk) => {
            audioBuffer = Buffer.concat([audioBuffer, chunk]);
        });

        audioStream.on('end', () => {
            streamEnded = true;
            console.log(`[${mode.toUpperCase()}] FFmpeg finished - ${audioBuffer.length} bytes buffered`);
        });

        audioStream.on('error', (err) => {
            console.error(`[${mode.toUpperCase()}] Stream Error:`, err.message);
        });

        // Real-time sending interval
        let loopBytesSent = 0;
        let lastLogTime = Date.now();
        const startTime = Date.now();

        const sendInterval = setInterval(() => {
            // Calculate how many bytes we should have sent by now
            const elapsed = Date.now() - startTime;
            const expectedBytes = Math.floor((elapsed / 1000) * BYTES_PER_SECOND);

            // Send chunks to catch up (but not more than available)
            while (loopBytesSent < expectedBytes && bufferReadIndex < audioBuffer.length) {
                const remaining = audioBuffer.length - bufferReadIndex;
                const toSend = Math.min(CHUNK_SIZE, remaining);
                const chunk = audioBuffer.slice(bufferReadIndex, bufferReadIndex + toSend);

                port.write(chunk);
                bufferReadIndex += toSend;
                loopBytesSent += toSend;
                totalBytesSent += toSend;
            }

            // Log every second
            const now = Date.now();
            if (now - lastLogTime >= 1000) {
                const audioSecs = (loopBytesSent / BYTES_PER_SECOND).toFixed(1);
                console.log(`[${mode.toUpperCase()}] Sent ${loopBytesSent} bytes (~${audioSecs}s audio) [real-time]`);
                lastLogTime = now;
            }

            // Check if we're done
            if (streamEnded && bufferReadIndex >= audioBuffer.length) {
                clearInterval(sendInterval);
                const duration = (loopBytesSent / BYTES_PER_SECOND).toFixed(1);
                console.log(`[${mode.toUpperCase()}] Loop #${loopCount} finished: ${loopBytesSent} bytes (~${duration}s audio)`);
                console.log(`[${mode.toUpperCase()}] Total sent: ${totalBytesSent} bytes`);
                playLoop();
            }
        }, CHUNK_INTERVAL_MS);
    }
} else {
    console.error("Unknown mode. Use --mode=[alice|bob|mallory]");
}
