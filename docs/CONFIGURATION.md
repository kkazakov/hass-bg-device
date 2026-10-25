# Configuration

Everything is static in the firmware image for now. To change a value: edit the
YAML, `make compile`, `make upload` (USB or OTA). There is no captive portal or
NVS settings editor yet.

## Files

| File | Committed? | Purpose |
|---|---|---|
| `firmware/bg-voice.yaml` | yes | device name, pins, gateway URL, timings, wake word |
| `firmware/secrets.yaml.example` | yes | template |
| `firmware/secrets.yaml` | **no** (git-ignored) | real Wi-Fi, API key, OTA/AP passwords |

```bash
make secrets
$EDITOR firmware/secrets.yaml
```

## Required settings

### `firmware/secrets.yaml`

| Key | Example | Notes |
|---|---|---|
| `wifi_ssid` | `"HomeNet"` | 2.4 GHz; the ESP32-S3 has no 5 GHz |
| `wifi_password` | `"..."` | WPA2 |
| `api_encryption_key` | base64, 32 bytes | `openssl rand -base64 32` |
| `ota_password` | `"..."` | ESPHome OTA |
| `ap_password` | `"fallback123"` | fallback AP, ≥ 8 chars |
| `gateway_api_key` | `""` or `"..."` | same as gateway `BGV_API_KEY`; empty = no header |

`api_encryption_key` is the ESPHome native API encryption key, not the gateway
key. Generate a fresh one per device.

### `firmware/bg-voice.yaml`

| Key | Default | Notes |
|---|---|---|
| `substitutions.gateway_url` | `http://192.168.1.10:8125` | base URL, no trailing slash; LAN/VPN |
| `substitutions.device_name` | `bg-voice-satellite` | hostname |
| `substitutions.friendly_name` | `BG Voice Satellite` | display name |
| `micro_wake_word.models[].model` | `okay_nabu` | stock bring-up model |
| `micro_wake_word.stop_after_detection` | `false` | **must stay false** so the mic keeps running |
| `bg_voice.gateway_api_key` | `!secret gateway_api_key` | optional |
| `bg_voice.vad_threshold` | `500` | peak int16; tune on hardware |
| `bg_voice.silence_ms` | `800ms` | end-of-speech hangover |
| `bg_voice.max_command_ms` | `12000ms` | hard cap |
| `bg_voice.pre_roll_ms` | `300ms` | audio kept before wake |
| `bg_voice.no_speech_timeout_ms` | `3000ms` | discard if nobody speaks |
| `bg_voice.http_timeout_ms` | `30000ms` | gateway ASR can be slow |

## Pin map

| Signal | GPIO | Component |
|---|---|---|
| I2S mic BCLK | 4 | INMP441 SCK |
| I2S mic LRCLK | 5 | INMP441 WS |
| I2S mic DIN | 6 | INMP441 SD |
| I2S amp BCLK | 15 | MAX98357A BCLK |
| I2S amp LRCLK | 16 | MAX98357A LRC |
| I2S amp DOUT | 17 | MAX98357A DIN |
| Button | 0 | to GND, `INPUT_PULLUP` |
| Status LED | 48 | optional; not configured |

If you change pins, update both `i2s_audio` buses and the `microphone`/`speaker`
platform entries. The button is configured as a `binary_sensor` with
`inverted: true`.

## Audio format

- Microphone: **16 kHz, 16-bit, mono**. The component refuses to start
  otherwise.
- Uplink WAV: 16 kHz mono PCM16, field `audio`.
- Reply WAV: PCM16 **mono**, 16–48 kHz. The speaker is reconfigured to the
  reply's sample rate. MP3 is not decoded in v1.
- `pre_roll_ms` is included at the start of the command. It may contain the
  wake-word tail.

## Gateway URL and key

`gateway_url` is the base URL only:

```
http://192.168.1.10:8125
http://gateway.local:8125
http://100.101.102.21:8125        # Tailscale
```

The device appends `/v1/voice` and the returned `audio_url`. Do not include a
path or trailing slash.

`gateway_api_key` must equal the gateway's `BGV_API_KEY`. If the gateway has no
key, leave it empty; the device omits the `X-API-Key` header entirely. The same
key is sent on the reply-audio GET.

## Security

- `firmware/secrets.yaml` is git-ignored. Never commit real values.
- The gateway key is compiled into the firmware image; treat a flash dump as
  sensitive.
- HTTP is plaintext. Use a trusted LAN or a VPN (Tailscale/WireGuard). Do not
  port-forward the gateway.
- OTA is password-protected; the ESPHome API is encrypted.
- v1 has no physical mute switch. Add one before placing the device in a
  bedroom/private space if that matters.
- The device sends audio only after the local wake word fires; the stock
  `micro_wake_word` runs fully on-device.

## Changing configuration

```bash
$EDITOR firmware/bg-voice.yaml
make config
make upload          # USB first time; later OTA: esphome upload ...
```

If you change the wake-word model, also update `firmware/models/README.md` with
its provenance.

## Future configuration work

- Captive portal / Improv Wi-Fi provisioning.
- Gateway URL/key as HA `text` entities or a small web form, stored in NVS.
- Per-device name/ID for multi-room.
- Volume and VAD threshold as HA `number` entities.
- TLS with a pinned CA.
