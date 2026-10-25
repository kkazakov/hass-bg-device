# Architecture

## Block diagram

```
┌──────────────┐   I2S (16 kHz, 16-bit, mono)   ┌──────────────────────────┐
│ INMP441 mic  ├───────────────────────────────►│ ESP32-S3 N16R8            │
└──────────────┘                                │                          │
                                                │  micro_wake_word task     │
                                                │  bg_voice (main loop)     │
                                                │  bg_voice upload task     │
                                                └───────┬──────────┬───────┘
                                                        │          │
                                      Wi-Fi HTTP        │          │ I2S
                                                        ▼          ▼
                                             ┌────────────────┐  ┌────────────┐
                                             │ hass-bg-gateway│  │ MAX98357A  │
                                             │ POST /v1/voice │  │ + speaker  │
                                             │ GET  /v1/audio │  └────────────┘
                                             └───────┬────────┘
                                                     ▼
                                              Home Assistant
```

The ESP32 is a satellite. It does not know the command catalogue, does not
match intents and does not talk to Home Assistant directly.

## Why ESPHome

ESPHome already provides:

- Wi-Fi station + fallback AP + captive portal;
- OTA and encrypted API;
- I2S microphone and speaker abstractions with format conversion;
- the `micro_wake_word` component, including model download/compile;
- logging and a state text sensor.

The custom `bg_voice` component only implements the gateway-specific part:
pre-roll, VAD, WAV, multipart HTTP, reply download and half-duplex playback.
Porting to raw ESP-IDF later is possible; the state machine and gateway
contract would not change.

## State machine

```
                 wake word / button
        ┌───────────────────────────────┐
        │                               ▼
     ┌──────┐                       ┌──────────┐
     │ IDLE │                       │ LISTENING│
     └──────┘                       └────┬─────┘
        ▲                               │ silence / max duration / buffer full
        │                               ▼
        │                          ┌──────────┐
        │        no speech         │ THINKING │
        ├──────────────────────────└────┬─────┘
        │                               │ HTTP 200 + speak
        │                               ▼
        │                          ┌──────────┐
        │        playback done     │ SPEAKING │
        └──────────────────────────└──────────┘
                                        │
                              error     ▼
                                   ┌─────────┐
                                   │  ERROR  │──► IDLE after 3 s
                                   └─────────┘
```

| State | Entry | Exit | Mic | Speaker |
|---|---|---|---|---|
| `idle` | boot, command done, error timeout | wake/button | running for wake word | stopped |
| `listening` | wake/button | silence, max duration, no speech | running | stopped |
| `thinking` | recording finished | gateway reply/error | running (wake word continues) | stopped |
| `speaking` | `speak: true` + audio downloaded | playback done | **muted** | playing |
| `error` | HTTP/JSON/memory failure | 3 s timeout | running | stopped |

The `Voice state` text sensor publishes these strings. It is optional; if no
`state_sensor` is configured the component only logs.

## Audio path

### Uplink

```
I2S mic → Microphone callback (16-bit mono 16 kHz)
        → pre-roll history (300 ms, 9,600 B)
        → command buffer (12 s max, 384,000 B)
        → WAV header (44 B)
        → multipart POST /v1/voice
```

- The microphone must be 16 kHz, 16-bit, mono. `bg_voice` fails setup with a
  clear message otherwise.
- `micro_wake_word` starts the microphone and keeps it running because
  `stop_after_detection: false`.
- `bg_voice` registers a second callback on the same `Microphone`. ESPHome's
  `CallbackManager` supports multiple listeners.
- Pre-roll is a linear history buffer. On wake, it is copied to the start of
  the command buffer; the current mic chunk is appended after it.
- VAD is a peak threshold per callback, with a silence hangover. It is not a
  neural VAD; tune `vad_threshold` for the enclosure.

### Downlink

```
JSON { speak: true, audio_url: "/v1/audio/x.wav" }
  → GET gateway + audio_url (X-API-Key)
  → PSRAM buffer (≤ 2 MB)
  → RIFF/WAVE parse
  → require PCM16 mono, 16–48 kHz
  → speaker.set_audio_stream_info(rate)
  → I2S speaker → MAX98357A
```

- The gateway's `audio_url` is relative; `bg_voice` joins it to the configured
  base URL and sends `X-API-Key`.
- MP3 (gTTS) is rejected with a log message in v1. Configure the gateway to
  use Piper/WAV for this device.
- The I2S speaker is configured for 16 kHz by default, but `set_audio_stream_info`
  reconfigures it to the reply's sample rate (primary I2S mode supports
  16–48 kHz). Verify pitch on hardware.

## Memory budget

| Buffer | Size | Location | Lifetime |
|---|---:|---|---|
| pre-roll (300 ms) | 9,600 B | PSRAM | boot |
| command (12 s + header) | 384,044 B | PSRAM | boot |
| reply audio | up to 2,097,152 B | PSRAM | per reply |
| HTTP response JSON | ≤ 16 KB | heap | per request |
| microWakeWord model | ~50–120 KB | flash/PSRAM | boot |
| microWakeWord ring buffer | 120–1000 ms | heap/PSRAM | detection |

N16R8 (8 MB PSRAM) is required. The command buffer is allocated once; the
reply buffer is allocated on demand and freed after playback. If allocation
fails, the state goes `error` and the component logs the requested size.

## Tasks and concurrency

| Context | Work |
|---|---|
| ESPHome main loop | `loop()`: state timers, start upload task, publish state |
| micro_wake_word inference task | frontend features + TFLite inference |
| I2S mic task/callback | delivers PCM chunks to all microphone callbacks |
| `bg_voice_upload` task (8 KB stack) | HTTP POST, JSON parse, audio GET, playback |

Synchronisation:

- `buffer_mutex_` guards the pre-roll/command buffers and VAD timestamps.
- `state_`, `wake_requested_`, `recording_done_`, `upload_task_started_`,
  `speech_seen_`, `listen_start_ms_`, `last_voice_ms_` are atomics.
- The upload task only starts after the state is `thinking`; the audio callback
  stops appending because it only appends in `listening`.
- A new wake is rejected while an upload task is still running, so the command
  buffer cannot be overwritten under HTTP.

## Timing

| Parameter | Default | Notes |
|---|---|---|
| Wake-word latency | ~0.3–0.8 s | model + sliding window |
| Pre-roll | 300 ms | avoids clipping the first syllable |
| No-speech timeout | 3 s | discard if nobody speaks |
| Silence hangover | 800 ms | end of command |
| Max command | 12 s | hard cap |
| HTTP timeout | 30 s | gateway ASR can be slow |
| Playback drain | ≤ 10 s | wait for I2S buffer |
| Error reset | 3 s | back to idle |

## Failure handling

| Failure | Behaviour |
|---|---|
| Wi-Fi down | HTTP fails; state `error`; retry on next wake |
| Gateway unreachable | POST fails; state `error`; log |
| HTTP 401 | log body; state `error`; check `gateway_api_key` |
| HTTP 4xx/5xx | log body; state `error` |
| Invalid JSON | state `error` |
| Reply download fails | log; command is still considered done; state `idle` |
| Reply is MP3/stereo/unsupported | log; skip playback; state `idle` |
| PSRAM allocation fails | state `error`; log size |
| Brownout | hardware issue; add bulk capacitor / stronger PSU |
| Mic stopped (OTA/error) | no audio; no-speech timeout discards command |

## Security and privacy

- Audio is only captured locally; it leaves the device after the wake word.
- The gateway key is stored in the firmware image (`secrets.yaml` → compiled
  config). It is not encrypted at rest; treat the flash image as sensitive.
- HTTP is plaintext. Use LAN/VPN (Tailscale/WireGuard), not the open internet.
- OTA is password-protected; the ESPHome API is encrypted.
- A physical mute switch is a Phase 4 item; v1 has no hardware mute.

## Future architecture options

- **AEC/barge-in:** ESP-SR AEC on the S3, or hardware XMOS (Voice PE/BOX-3).
- **MP3:** `esp-audio-libs` MP3 decoder or a gateway WAV-only policy.
- **TLS:** `esp_http_client` with a pinned CA; requires SNTP and certificate
  management. Prefer a VPN until then.
- **Dynamic config:** captive portal or HA text/number entities writing NVS.
- **Multi-room:** add `X-Device-Id` and let the gateway route/announce.
