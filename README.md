| Supported Targets | ESP32 |
| ----------------- | ----- |

# REI-BT - Smart ESP32 A2DP Sink

A Bluetooth A2DP audio receiver using the ESP32 and PCM5102 DAC, designed for clean audio playback with user-friendly features:

- Silent pairing: No audio on Bluetooth pairing.
- Logarithmic volume control: Natural volume steps.
- Auto-reconnect: Automatically reconnects to the last paired device on boot.
- I2S output to PCM5102 DAC (supports 44.1kHz, 16-bit stereo).
- No buttons, no LEDs. Suitable for embedding.
- Custom Bluetooth name.

## Hardware Requirements

- ESP32 development board
- PCM5102 DAC module

### Wiring (I2S to PCM5102)

| ESP32 Pin | PCM5102 Signal |
|----------|----------------|
| GPIO16   | LRCK (LRC)     |
| GPIO17   | DIN (DATA)     |
| GPIO21   | BCLK (SCK)     |

PCM5102 FMT is I2S.

All pins are in a row.

## Build & Flash

```bash
idf.py set-target esp32
idf.py menuconfig
idf.py flash monitor
```

ESP-IDF v5.5.2

## Configuration

All settings are in `menuconfig`.


## License

MIT License - see LICENSE

## Acknowledgements

- ESP-IDF Bluetooth A2DP Sink example.
- PCM5102 datasheet.
- Qwen IA.

