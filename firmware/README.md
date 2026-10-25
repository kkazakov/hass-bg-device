# Firmware

ESPHome 2026.9.x firmware for the ESP32-S3 satellite.

## Build and flash

```bash
cd ..
make secrets
$EDITOR firmware/secrets.yaml
$EDITOR firmware/bg-voice.yaml   # gateway_url

make config
make compile
make upload                      # first flash over USB
make logs
```

`make upload` uses `esphome run`, which builds, flashes over USB and attaches to
the logs. Later updates can use OTA from the ESPHome dashboard or
`esphome upload firmware/bg-voice.yaml --device <ip>`.

## Files

| File | Purpose |
|---|---|
| `bg-voice.yaml` | device config: board, pins, Wi-Fi, gateway, wake word |
| `secrets.yaml.example` | template for Wi-Fi, API key, OTA/AP passwords |
| `components/bg_voice/` | custom component: VAD, WAV, HTTP, playback |
| `models/` | custom wake-word model (not committed yet) |

## Pin map

| Signal | GPIO | Notes |
|---|---|---|
| I2S mic BCLK | 4 | INMP441 SCK |
| I2S mic LRCLK | 5 | INMP441 WS |
| I2S mic DIN | 6 | INMP441 SD |
| I2S amp BCLK | 15 | MAX98357A BCLK |
| I2S amp LRCLK | 16 | MAX98357A LRC |
| I2S amp DOUT | 17 | MAX98357A DIN |
| Button | 0 | boot button, to GND |
| Status LED | 48 | optional, not configured |

INMP441 `L/R` goes to GND for the left channel (`channel: left`). MAX98357A
`VIN` is 5 V; the mic is 3.3 V. Common ground is required.

## Wake word

The checked-in config uses ESPHome's stock **`okay_nabu`** model for bring-up.
It proves the microphone, inference, gateway and playback path without waiting
for a custom model.

To use a custom **„окей дом“** model:

1. Train/export it as described in [`../docs/WAKE_WORD.md`](../docs/WAKE_WORD.md).
2. Put `okay_dom.tflite` and `okay_dom.json` in `models/`.
3. Replace the `models:` entry in `bg-voice.yaml`:

   ```yaml
   models:
     - model:
         type: local
         path: models/okay_dom.json
       probability_cutoff: 0.5
       sliding_window_size: 10
   ```

4. `make compile && make upload`.

Keep `stop_after_detection: false`; otherwise `micro_wake_word` stops the
microphone after the first detection and the command recording gets no audio.

## State machine

`bg_voice` publishes a `Voice state` text sensor:

| State | Meaning |
|---|---|
| `idle` | waiting for the wake word or button |
| `listening` | recording; waiting for speech / end of speech |
| `thinking` | WAV uploaded; waiting for the gateway |
| `speaking` | playing the reply WAV; mic muted |
| `error` | HTTP/JSON/memory error; returns to idle after 3 s |

## Troubleshooting

- **No wake word:** check `micro_wake_word` logs, mic wiring and `channel`.
  The stock model expects English “Okay Nabu”; say it clearly at 1–3 m.
- **No audio / wrong pitch:** the reply must be a PCM16 **mono** WAV. The
  component reconfigures the I2S speaker to the WAV's sample rate (16–48 kHz).
  MP3 replies are not decoded in v1.
- **HTTP 401:** `gateway_api_key` does not match the gateway's `BGV_API_KEY`.
- **HTTP connection failed:** check `gateway_url`, that the gateway listens on
  `0.0.0.0`, and that the ESP32 and gateway are on the same LAN/VPN.
- **Brownout / reboot when speaking:** use a 5 V supply that can handle the
  amplifier's peaks; add a 470 µF capacitor near the MAX98357A.
- **`Not enough memory for audio buffers`:** the board must have 8 MB PSRAM and
  `psram: mode: octal`; N16R8 is required.
