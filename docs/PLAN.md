# Plan — ESP32-S3 local wake-word satellite

## 1. Goal

A small, always-on device that:

1. listens locally for **„окей дом“**;
2. records the command until the room is quiet;
3. uploads a 16 kHz mono WAV to `hass-bg-gateway`'s `POST /v1/voice`;
4. plays the gateway's spoken reply;
5. runs on a ~$30 ESP32-S3 bill of materials;
6. keeps audio off the network until the wake word fires.

The phone app (`hass-bulgarian-voice`) remains the manual push-to-talk client.
This device is a separate satellite, not a replacement for the app.

## Status (2026-10-25)

| Phase | Status |
|---|---|
| 0 — Scaffold | done: config + compile pass, mock gateway smoke test passes |
| 1 — Hardware bring-up | next |
| 2 — Real gateway | pending |
| 3 — Custom wake word | pending |
| 4 — UX / hardening | pending |
| 5 — Optional | pending |

## 2. Non-goals for v1

- Cloud wake word or cloud ASR.
- Home Assistant Assist / Wyoming pipeline in the middle.
- Acoustic echo cancellation / barge-in.
- MP3 reply decoding.
- Multi-room arbitration or device identity.
- Dynamic Wi-Fi/gateway configuration (captive portal, HA entities, NVS).
- TLS certificate validation.
- Battery operation.

## 3. Decisions

| Decision | Choice | Why |
|---|---|---|
| Firmware framework | **ESPHome 2026.9.x** + external `bg_voice` component | First-class `micro_wake_word`, I2S mic/speaker, Wi-Fi, OTA, logs. Raw ESP-IDF would reimplement all of that before the first command works. |
| MCU | **ESP32-S3-DevKitC-1 N16R8** | 16 MB flash, 8 MB octal PSRAM, dual core, vector instructions. PSRAM is required for the command/reply buffers. |
| Wake word | **microWakeWord**, on-device | Runs on S3 in a few ms; open model format; no cloud. ESP-SR/WakeNet custom models are a proprietary dead end. |
| Mic | **INMP441** I2S MEMS | Cheap, 3.3 V, good enough at 1–3 m; 16 kHz mono. |
| Amp | **MAX98357A** + 3 W 4 Ω speaker | Cheap I2S class-D; no DAC wiring. |
| Uplink | 16 kHz mono PCM16 WAV, multipart field `audio` | Exactly what the Android app sends and what the gateway already accepts. |
| Downlink | PCM16 mono WAV, 16–48 kHz | Avoids an MP3 decoder in v1. The speaker is reconfigured to the WAV rate. |
| Duplex | Half-duplex: mute mic during playback | No AEC on a bare dev board; prevents the device hearing its own TTS. |
| Configuration | Static YAML + `secrets.yaml` | Fastest path; reflash to change. Dynamic config is a later phase. |
| Bring-up model | Stock `okay_nabu` | Proves mic → inference → gateway → speaker before the Bulgarian model exists. |
| Final model | Custom `окей дом` | Separate ML task; see [`WAKE_WORD.md`](WAKE_WORD.md). |

## 4. Architecture

```
INMP441 ──I2S──► ESP32-S3 ──Wi-Fi──► gateway  POST /v1/voice
                    │                    │
                    │                    └──► Home Assistant
                    │
                    └──I2S──► MAX98357A ──► speaker
                                  ▲
                    reply WAV ◄───┘  GET /v1/audio/<id>.wav
```

Detailed state machine, buffers and timing: [`ARCHITECTURE.md`](ARCHITECTURE.md).

## 5. Phases

### Phase 0 — Scaffold (this commit)

**Deliverables**

- Repository, README, MIT licence, `.gitignore`.
- ESPHome config with static Wi-Fi, gateway URL and API key.
- `bg_voice` external component skeleton: state machine, VAD, WAV, multipart
  HTTP, reply download, half-duplex playback.
- Exact gateway contract in [`GATEWAY_API.md`](GATEWAY_API.md).
- Mock gateway, test WAV generator, curl smoke test.
- Plan, architecture, hardware, wake-word and testing docs.
- CI config validation and cached compile.

**Done when**

- `make config` validates.
- `make compile` builds (or a documented blocker is recorded).
- `make mock` + `make gateway-test` return the expected JSON.
- No secrets are committed.

### Phase 1 — Hardware bring-up

**Tasks**

- Assemble the board: INMP441, MAX98357A, speaker, button, 5 V supply.
- Flash the stock `okay_nabu` model.
- Verify mic data: `micro_wake_word` detects “Okay Nabu” at 1–3 m.
- Verify speaker: play a test WAV through the `speaker` component.
- Point `gateway_url` at `tools/mock_gateway.py`.
- Wake → speak → observe `POST /v1/voice` and reply playback.
- Tune `vad_threshold`, `silence_ms`, `pre_roll_ms`.

**Done when**

- A wake word produces one upload and one spoken reply.
- No reboot/brownout during playback.
- The state sensor goes `idle → listening → thinking → speaking → idle`.

### Phase 2 — Real gateway

**Tasks**

- Point at the real `hass-bg-gateway` on the LAN/VPN.
- Set `gateway_api_key` to the gateway's `BGV_API_KEY` (or empty).
- Verify a command from the gateway catalogue executes and the reply plays.
- Verify a `speak: false` command does not try to play audio.
- Verify 401, timeout, unreachable gateway and invalid JSON produce `error`
  and recover to `idle`.
- Confirm the gateway accepts the WAV (sample rate, duration, leading pre-roll).

**Done when**

- Three different commands work end-to-end.
- A keyed gateway works; a keyless gateway still works.
- Failure cases are visible in logs and on the state sensor.

### Phase 3 — Custom „окей дом“ wake word

**Tasks**

- Build the phrase/data plan in [`WAKE_WORD.md`](WAKE_WORD.md).
- Generate positives (Bulgarian Piper `bg_BG-dimitar`, English `okay dom` /
  `okay dawm` variants), hard negatives and ambient negatives.
- Train microWakeWord; export INT8 `.tflite` + manifest.
- Evaluate FRR on real household voices and FAR/hour on Bulgarian
  TV/radio/podcasts.
- Swap the model in `bg-voice.yaml`; reflash.
- Tune `probability_cutoff` and `sliding_window_size` on the device.

**Done when**

- FRR ≤ 10% at 1–3 m for the intended speakers.
- FAR ≤ 1/hour on a 2–3 hour Bulgarian audio sample.
- No false trigger from the device's own reply (half-duplex holds).
- The model and its provenance are recorded in `firmware/models/README.md`.

### Phase 4 — UX and hardening

**Tasks**

- LED feedback: idle / listening / thinking / speaking / error.
- Optional chime on wake and error tone.
- Physical mute switch or long-press mute.
- Watchdog, Wi-Fi reconnect, HTTP retry/backoff.
- 24-hour soak test; memory/heap logging.
- OTA update test.
- Enclosure with mic port and speaker grille.

**Done when**

- 24 h without a reboot or leaked buffer.
- OTA works.
- A non-technical user can use it without logs.

### Phase 5 — Optional

- AEC / barge-in (ESP-SR AEC or XMOS hardware).
- MP3 reply decoding.
- HTTPS with a pinned CA or Tailscale.
- Dynamic config (captive portal / HA text entities / NVS).
- Multi-room device ID header.
- Local “stop” wake word.
- Battery/solar variant.

## 6. Acceptance criteria (v1)

- One wake word per command; no double uploads.
- End-of-speech latency ≤ 1.2 s after the user stops talking.
- Upload + gateway + first audio ≤ 3 s on a LAN for a short query.
- No audio leaves the device before the wake word.
- No secrets in git.
- `make config` and `make compile` pass from a clean checkout.
- The device recovers from gateway/Wi-Fi failure without a power cycle.

## 7. Risks

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Custom microWakeWord model is hard to train / generalise | High | High | Start with stock model; prototype phrase with openWakeWord; mixed Bulgarian/English data; real recordings; evaluate before swapping. |
| English “okay dome” model misses Bulgarian `/dɔm/` | Medium | Medium | Train on `okay dom`/`okay dawm`, not `dome`; include Bulgarian samples; test with real speakers. |
| ESPHome microphone callbacks conflict with `micro_wake_word` | Low | High | Both use the same `Microphone`; `stop_after_detection: false`; test early. Fall back to a passive `MicrophoneSource` or fork. |
| Reply WAV is 22.05 kHz / stereo / MP3 | Medium | Medium | Speaker is reconfigured to the WAV rate; require mono PCM16; document Piper/WAV; MP3 later. |
| PSRAM allocation fails / fragmentation | Medium | High | N16R8; allocate command once; reply buffer 2 MB on demand; log free heap; reboot on repeated failure. |
| Acoustic feedback / self-trigger | Medium | Medium | Half-duplex mute; no AEC in v1; test with speaker at normal volume. |
| Wi-Fi power save adds latency | Medium | Low | `power_save_mode: none`; DHCP reservation. |
| ESPHome API changes break the component | Medium | Medium | Pin ESPHome 2026.9.x in docs/CI; keep component small; test on upgrade. |
| Brownout during playback | Medium | High | 5 V 1 A supply; bulk capacitor; short speaker leads; measure rail. |
| Gateway ASR/matcher sees the wake word in the pre-roll | Medium | Low | Keep pre-roll short (300 ms); gateway should tolerate leading wake phrase; add trim option if needed. |

## 8. Open questions

1. **Gateway TTS format.** Does the gateway return Piper WAV at 22.05 kHz for
   this device, or gTTS MP3? v1 needs mono PCM16 WAV.
2. **Leading wake word.** Does the gateway's matcher tolerate “окей дом …” in
   the transcript, or should the satellite trim more aggressively?
3. **Final phrase.** Is “окей дом” distinctive enough in Bulgarian TV/radio,
   or should it become “окей, домакине” / “здравей, дом”?
4. **Enclosure/mic.** Bare INMP441 vs a 2-mic array (ReSpeaker Lite / BOX-3)
   for 3–5 m.
5. **Licence of the final model.** Which TTS voices/datasets can be
   redistributed with the `.tflite`?

## 9. Backlog

- [ ] Validate `make compile` on a clean machine.
- [ ] Add LED status ring.
- [ ] Add mute switch.
- [ ] Add HTTP retry with one backoff.
- [ ] Add `X-Device-Id` header for multi-room logs.
- [ ] Add WAV reply resampling fallback for stereo.
- [ ] Add MP3 decoder.
- [ ] Add AEC.
- [ ] Add dynamic config.
- [ ] Record a demo video/GIF for the README.
