# hass-bg-device

An always-on ESP32-S3 voice satellite for
[`hass-bg-gateway`](https://github.com/kkazakov/hass-bg-gateway).

The device listens locally for **„окей дом“**, records the command until the
room goes quiet, uploads a 16 kHz mono WAV to the gateway's `POST /v1/voice`,
and plays the spoken reply. No cloud service, no Home Assistant voice pipeline,
no phone in the loop.

> **Status: scaffold / bring-up.** The firmware builds around ESPHome's
> `micro_wake_word` and a custom `bg_voice` component; `esphome config` and
> `esphome compile` pass with ESPHome 2026.9.1. The bring-up wake word is the
> stock **“Okay Nabu”** model; the custom Bulgarian model is a separate training
> task described in [`docs/WAKE_WORD.md`](docs/WAKE_WORD.md). Hardware has not
> been validated yet.

## Architecture

```
INMP441 ──I2S──► ESP32-S3 ──Wi-Fi──► gateway  POST /v1/voice  (multipart WAV)
                    │                    │
                    │                    └──► Home Assistant
                    │
                    └──I2S──► MAX98357A ──► speaker
                                  ▲
                    reply WAV ◄───┘  GET /v1/audio/<id>.wav
```

The local wake word runs on the ESP32-S3 through
[`microWakeWord`](https://github.com/kahrendt/microWakeWord). The custom
`bg_voice` ESPHome component owns the state machine, VAD, WAV upload/download
and half-duplex playback.

## Hardware (roughly $25–35)

| Part | Example | Notes |
|---|---|---|
| MCU | ESP32-S3-DevKitC-1 **N16R8** | 16 MB flash, 8 MB octal PSRAM |
| Mic | INMP441 | I2S MEMS, 3.3 V |
| Amp | MAX98357A | I2S class-D, 5 V |
| Speaker | 3 W, 4 Ω | small full-range |
| Button | momentary to GND | GPIO0 / boot button works |
| PSU | 5 V, 1 A | USB-C |

Full wiring and pin map: [`docs/HARDWARE.md`](docs/HARDWARE.md).

## Quick start

Requirements: ESPHome **2026.9.x**, Python 3.12+, an ESP32-S3 N16R8 board, and
a reachable gateway.

```bash
git clone https://github.com/kkazakov/hass-bg-device.git
cd hass-bg-device

make secrets          # copies firmware/secrets.yaml.example -> firmware/secrets.yaml
$EDITOR firmware/secrets.yaml
$EDITOR firmware/bg-voice.yaml   # gateway_url

make config           # validate the ESPHome config
make compile          # build the firmware
make upload           # flash over USB (first time)
make logs             # serial logs
```

For firmware-only development without a real gateway:

```bash
make mock             # starts tools/mock_gateway.py on :18125
make gateway-test     # POSTs a test WAV with curl
```

## Configuration

Static for now, in [`firmware/bg-voice.yaml`](firmware/bg-voice.yaml) and
[`firmware/secrets.yaml`](firmware/secrets.yaml.example):

| Setting | Where | Example |
|---|---|---|
| Wi-Fi SSID/password | `secrets.yaml` | `wifi_ssid`, `wifi_password` |
| Gateway base URL | `bg-voice.yaml` | `http://192.168.1.10:8125` |
| Gateway `X-API-Key` | `secrets.yaml` | `gateway_api_key` (empty if keyless) |
| API encryption key | `secrets.yaml` | `openssl rand -base64 32` |
| OTA password | `secrets.yaml` | any |
| Pins / timings | `bg-voice.yaml` | see [`docs/CONFIGURATION.md`](docs/CONFIGURATION.md) |

`secrets.yaml` is git-ignored. Never commit real Wi-Fi credentials, the gateway
key or the API encryption key.

## Repository layout

```
firmware/
  bg-voice.yaml              ESPHome device config (pins, Wi-Fi, gateway)
  secrets.yaml.example       copy to secrets.yaml
  components/bg_voice/       custom ESPHome component (state machine + HTTP)
  models/                    custom wake-word model goes here
docs/
  PLAN.md                    phases, decisions, risks, acceptance criteria
  ARCHITECTURE.md            state machine, buffers, tasks, timing
  GATEWAY_API.md             exact /v1/voice contract and examples
  CONFIGURATION.md           every setting, secrets, security notes
  HARDWARE.md                BOM, wiring, pin map, assembly
  WAKE_WORD.md               microWakeWord training and evaluation
  TESTING.md                 host, mock-gateway and hardware test plan
tools/
  mock_gateway.py            local fake gateway for firmware development
  make_test_wav.py           generate a 16 kHz test WAV
  test_gateway.sh            curl smoke test
  train_wakeword/            training notes and phrase list
```

## The gateway contract

The device calls the same API as the Android app:

- `POST {gateway}/v1/voice` — `multipart/form-data`, field `audio`, WAV
  16 kHz mono PCM16, optional `X-API-Key`.
- Response JSON: `kind`, `speak`, `text`, `transcript`, `audio_url`,
  `matched`, `timings`.
- `GET {gateway}{audio_url}` with the same `X-API-Key`; the device currently
  plays **WAV** replies. MP3 (gTTS) is not decoded in v1.

Details and examples: [`docs/GATEWAY_API.md`](docs/GATEWAY_API.md).

## Design constraints

- **Local wake word.** Audio does not leave the device until the wake word fires.
- **Half-duplex.** The mic is muted while the reply is playing; no AEC in v1.
- **Static config.** No captive portal or NVS settings yet; reflash to change.
- **LAN/VPN only.** Plain HTTP; do not expose the gateway to the internet.
- **One gateway = one Home Assistant**, as in the Android app.
- **WAV replies only.** Configure the gateway's TTS to Piper/WAV for this
  device; gTTS MP3 is a later feature.

## License

MIT. See [`LICENSE`](LICENSE). The stock `okay_nabu` wake-word model used for
bring-up is downloaded by ESPHome from
[`esphome/micro-wake-word-models`](https://github.com/esphome/micro-wake-word-models)
(Apache-2.0); a custom model trained for this project will carry its own
provenance in [`firmware/models/README.md`](firmware/models/README.md).
