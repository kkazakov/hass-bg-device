# Testing

## Host checks (no hardware)

```bash
make config          # ESPHome schema validation
make compile         # full ESP-IDF build
python3 -m py_compile tools/mock_gateway.py tools/make_test_wav.py
```

`make config` creates `firmware/secrets.yaml` from the example if it does not
exist. It does not need a real gateway.

## Mock gateway

```bash
# terminal 1
make mock

# terminal 2
make gateway-test
```

Expected:

- the mock logs `POST /v1/voice`, the content type and the uploaded byte count;
- it saves `/tmp/bg-voice-last-upload.wav`;
- the response is JSON with `speak: true` and `audio_url`;
- `tools/test_gateway.sh` prints the JSON.

Options:

```bash
python3 tools/mock_gateway.py --help
python3 tools/mock_gateway.py --api-key secret
python3 tools/mock_gateway.py --fail 500
python3 tools/mock_gateway.py --no-speak
```

If the device is configured with `gateway_api_key: secret`, run the mock with
`--api-key secret`; otherwise it returns 401.

## Firmware on hardware

### Flash

```bash
make upload
make logs
```

First flash over USB. Later:

```bash
esphome upload firmware/bg-voice.yaml --device <device-ip>
```

### Smoke test

1. Boot; wait for `Ready` and Wi-Fi.
2. Say “Okay Nabu” (stock model) at 1 m.
3. Expect:
   - `Listening (wake word)`
   - `Recorded … bytes`
   - `POST http://…/v1/voice`
   - `Gateway: kind=… speak=…`
   - reply audio, if `speak: true`.
4. `Voice state` in HA/logs: `idle → listening → thinking → speaking → idle`.
5. Press the button; same flow without the wake word.

### Test matrix

| Test | Steps | Pass |
|---|---|---|
| Wake at 1 m | say phrase 10× | ≥ 9 detections |
| Wake at 3 m | say phrase 10× | ≥ 7 detections |
| TV noise | TV on, say phrase 10× | ≥ 7 detections, no self-trigger |
| False accepts | 1 h TV/radio | ≤ 1 activation |
| Short command | “окей дом, часът” | upload < 3 s, reply |
| Long command | 8–10 s utterance | upload, no truncation before max |
| Silence after wake | wake, say nothing | discard after 3 s, no upload |
| Keyed gateway | set key, correct/incorrect | 200 / 401 in logs |
| Keyless gateway | empty key | no `X-API-Key`, 200 |
| Gateway down | stop mock, wake | `error`, returns to `idle` in 3 s |
| Bad JSON | mock `--fail 200` with junk | `error`, no crash |
| MP3 reply | point at gTTS audio | log “unsupported”, no crash |
| 22.05 kHz WAV | Piper reply | plays at correct pitch |
| Brownout | max volume reply | no reboot |
| 24 h soak | periodic commands | no reboot/leak |

### Logs to watch

```
[bg_voice]: Ready (gateway=…, api key set)
[bg_voice]: Listening (wake word)
[bg_voice]: Recorded 18432 bytes (0.6 s)
[bg_voice]: POST http://…/v1/voice (18476 bytes WAV)
[bg_voice]: Gateway: kind=query speak=true text=…
```

Warnings that matter:

- `Not enough memory for audio buffers` → PSRAM not enabled / wrong board.
- `Microphone must be 16000 Hz, 16-bit, 1 channel(s)` → YAML audio config.
- `Gateway returned HTTP 401` → key mismatch.
- `Unsupported reply WAV` → gateway returned MP3/stereo/other.
- `Not enough free bytes in ring buffer` from `micro_wake_word` → CPU/PSRAM
  pressure; reduce other work or move the task core.

## Metrics

For the custom wake word, record:

- FRR per speaker/distance/noise;
- FAR/hour over at least 2–3 hours of Bulgarian audio;
- wake latency (phrase end → `Listening`);
- end-of-speech latency (last speech → `Recorded`);
- upload + gateway + first audio latency;
- free heap/PSRAM after 1 h and 24 h.

## CI

`.github/workflows/firmware.yml` runs `esphome config` and a cached
`esphome compile` on every push/PR. It creates a throwaway `secrets.yaml` from
the example, so no real secrets are needed. The resulting binary is a compile
check only — it contains placeholder Wi-Fi credentials and must not be flashed.

## Troubleshooting quick reference

| Symptom | Likely cause |
|---|---|
| No serial logs | wrong USB port/cable; use the USB-UART/JTAG port |
| No Wi-Fi | 5 GHz SSID, wrong password, weak signal |
| No wake word | mic wiring, `channel`, wrong model, speak louder |
| Wake word fires constantly | threshold too low, TV/radio, model needs negatives |
| Upload 401 | `gateway_api_key` mismatch |
| Upload timeout | gateway not on `0.0.0.0`, wrong IP, firewall, VPN |
| No reply audio | `speak: false`, MP3 reply, speaker wiring, muted volume |
| Wrong pitch | non-16 kHz WAV without speaker reconfig; check logs |
| Reboot on playback | weak 5 V supply; add 470 µF; shorter speaker leads |
| `Not enough memory` | no PSRAM; use N16R8 and `psram: mode: octal` |
