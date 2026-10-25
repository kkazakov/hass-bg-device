# Handoff — hass-bg-device

**Date:** 2026-10-25  
**Repository:** https://github.com/kkazakov/hass-bg-device (public, `main`)  
**Local checkout used during development:** `~/Projects/hass-bg-device`  
**Status:** Phase 0 scaffold is complete and verified. Phase 1 (hardware
bring-up) has not started. The custom Bulgarian wake-word model has not been
trained.

This document is a starting point for an LLM/agent continuing the project on
another machine. Read the linked repo docs for detail; this file is the map and
the context that is easy to lose.

## What this project is

An always-on **ESP32-S3 voice satellite** for
[`hass-bg-gateway`](https://github.com/kkazakov/hass-bg-gateway):

1. listens locally for **„окей дом“**;
2. records the command until the room is quiet;
3. uploads a 16 kHz mono WAV to the gateway's `POST /v1/voice`;
4. plays the gateway's spoken reply.

It is a separate device, not the Flutter phone app
[`hass-bulgarian-voice`](https://github.com/kkazakov/hass-bulgarian-voice).
The phone app remains the manual push-to-talk client. No cloud service, no
Home Assistant Assist pipeline, no phone in the loop.

## Current state

- Initial scaffold: commit `ea5f0a4`.
- Documentation clarification: commit `06b4403`.
- GitHub Actions is green on both commits:
  - `esphome config` (27 s);
  - cached full `esphome compile` (7m30s cold, 1m26s with cache).
- Local verification:
  - `esphome config firmware/bg-voice.yaml` → valid;
  - `esphome compile firmware/bg-voice.yaml` → success;
  - image ~1.2 MB, RAM 32.9%, flash 15.3%;
  - mock gateway smoke test returned the expected JSON and saved a valid
    16 kHz mono WAV.
- **No hardware has been flashed or tested.**
- The bring-up wake word is the stock **“Okay Nabu”** model. The final
  **„окей дом“** model is a separate ML task.

## Key decisions

| Area | Decision |
|---|---|
| Firmware | **ESPHome 2026.9.1** + custom external component `bg_voice`. Raw ESP-IDF is a later option. |
| MCU | **ESP32-S3-DevKitC-1 N16R8** — 16 MB flash, 8 MB octal PSRAM. PSRAM is required. |
| Wake word | **microWakeWord**, on-device. ESP-SR/WakeNet custom models are a proprietary dead end. |
| Mic / amp | **INMP441** I2S mic + **MAX98357A** I2S amp + 3 W 4 Ω speaker. |
| Uplink | 16 kHz mono PCM16 WAV, multipart field `audio`, optional `X-API-Key`. |
| Downlink | PCM16 **mono** WAV, 16–48 kHz. MP3 is not decoded in v1. |
| Duplex | Half-duplex: mute mic while the reply plays. No AEC in v1. |
| Config | Static YAML + `secrets.yaml`; no captive portal/NVS settings yet. |
| Bring-up model | Stock `okay_nabu`; final model is custom „окей дом“. |

## Read these first

- [README.md](https://github.com/kkazakov/hass-bg-device/blob/main/README.md) — overview, BOM, quick start.
- [docs/PLAN.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/PLAN.md) — phases, risks, acceptance criteria, current status.
- [docs/ARCHITECTURE.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/ARCHITECTURE.md) — state machine, buffers, timing, failure handling.
- [docs/GATEWAY_API.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/GATEWAY_API.md) — exact API contract and examples.
- [docs/CONFIGURATION.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/CONFIGURATION.md) — every setting, secrets, security.
- [docs/HARDWARE.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/HARDWARE.md) — BOM, wiring, pin map.
- [docs/WAKE_WORD.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/WAKE_WORD.md) — training/evaluation plan, including the “okay dome” phonetic issue.
- [docs/TESTING.md](https://github.com/kkazakov/hass-bg-device/blob/main/docs/TESTING.md) — host, mock-gateway and hardware test plan.
- [firmware/bg-voice.yaml](https://github.com/kkazakov/hass-bg-device/blob/main/firmware/bg-voice.yaml) — device config.
- [firmware/components/bg_voice](https://github.com/kkazakov/hass-bg-device/tree/main/firmware/components/bg_voice) — custom component.
- [tools/mock_gateway.py](https://github.com/kkazakov/hass-bg-device/blob/main/tools/mock_gateway.py) — local fake gateway.

## Gateway contract in one paragraph

`POST {gateway}/v1/voice`, `multipart/form-data`, field `audio`, WAV 16 kHz mono
PCM16. Optional `X-API-Key: <BGV_API_KEY>`; never send an empty key. Response
JSON uses `kind`, `speak`, `text`, `transcript`, `audio_url`, `matched`,
`timings`, `reason`. `audio_url` is relative; fetch it with the same
`X-API-Key`. The device only plays PCM16 mono WAV in v1. Gateway default port is
**8125**. Full examples: `docs/GATEWAY_API.md`.

## Configuration in one paragraph

- `firmware/bg-voice.yaml`: board, pins, gateway URL, wake word, VAD/timing.
- `firmware/secrets.yaml` (git-ignored): Wi-Fi SSID/password, ESPHome API
  encryption key, OTA/AP passwords, `gateway_api_key`.
- Start with `cp firmware/secrets.yaml.example firmware/secrets.yaml`.
- The example values are placeholders. Never commit real credentials.
- Gateway URL placeholder: `http://192.168.1.10:8125`; replace it.
- Mock gateway defaults to **18125** so it does not collide with the real
  gateway on 8125.

## Critical gotchas

- **Component ID is `bg_voice_component`**, not `bg_voice`: ESPHome rejects an
  ID equal to the integration name. YAML lambdas use
  `id(bg_voice_component).on_wake_word()`.
- **`micro_wake_word.stop_after_detection` must stay `false`.** Otherwise the
  mic stops after the first detection and `bg_voice` records nothing.
- The mic must be **16 kHz, 16-bit, mono**; `bg_voice` fails setup with a clear
  message otherwise. INMP441 `L/R` goes to GND for `channel: left`.
- Reply audio must be **PCM16 mono WAV**. MP3/gTTS is rejected with a log
  message; configure the gateway's TTS to Piper/WAV.
- The command WAV starts with ~300 ms of pre-roll and may contain the tail of
  the wake phrase. The gateway matcher should tolerate it; reduce
  `pre_roll_ms` if not.
- PSRAM is mandatory. `Not enough memory for audio buffers` means a non-N16R8
  board or `psram: mode: octal` missing.
- GPIO0 is used for the button; the strapping-pin warning is expected. The
  onboard boot button works after boot.
- The `bg_voice` component uses `AUTO_LOAD = ["json"]` and depends on
  `microphone`, `speaker`, `text_sensor`. Do not turn `json` into a
  `DEPENDENCIES` entry without a `json:` block.
- CI compiles with placeholder secrets; its binary must **not** be flashed.
- ESPHome 2026.9.1 is pinned. Re-test on upgrade; the external component uses
  current `microphone`/`speaker` APIs.

## Wake-word status

- Bring-up: stock `okay_nabu` model, downloaded by ESPHome; no model binary is
  committed.
- Final: custom „окей дом“ microWakeWord model, not trained yet.
- Phonetic trap: Bulgarian „дом“ is `/dɔm/`; English “dome” is `/doʊm/`.
  Train English variants **“okay dom” / “okay dawm”**, not “okay dome”, and mix
  in Bulgarian samples (Piper `bg_BG-dimitar`) plus real household recordings.
- Add Bulgarian TV/radio/podcasts as negatives; the default microWakeWord
  negatives are English-heavy.
- Targets: FRR ≤ 10%, FAR ≤ 1/hour, tested in the real enclosure.
- Full plan: `docs/WAKE_WORD.md`; phrase list:
  `tools/train_wakeword/phrases.yaml`.

## Next work

Phase 1 — hardware bring-up:

1. Assemble per `docs/HARDWARE.md`; check 3.3 V/5 V rails.
2. `make secrets`; edit Wi-Fi and gateway URL.
3. `make config && make compile && make upload`.
4. Verify “Okay Nabu” detection and reply playback via `make mock`.
5. Tune `vad_threshold`, `silence_ms`, `pre_roll_ms` on the real mic/enclosure.
6. Run the test matrix in `docs/TESTING.md`.

Phase 2 — real gateway: point at the real `hass-bg-gateway`, set
`gateway_api_key`, verify action/query/no_match, 401/timeout/JSON failures.

Phase 3 — custom wake word: train/export `okay_dom.tflite` + manifest, put them
in `firmware/models/`, point `micro_wake_word.models` at the local manifest,
evaluate on-device, record provenance.

## Open questions

See `docs/PLAN.md` §8. The important ones:

- Does the gateway return Piper WAV at 22.05 kHz for this device, or gTTS MP3?
- Does the gateway matcher tolerate a leading wake phrase in the WAV?
- Is „окей дом“ distinctive enough in Bulgarian TV/radio, or should it become
  „окей, домакине“ / „здравей, дом“?
- Bare INMP441 vs a 2-mic array for 3–5 m?
- Which TTS voices/datasets can be redistributed with the final model?

## Suggested skills for the next agent

Use these if they are available in the next environment:

- **`diagnose`** — for hardware/audio/HTTP bring-up bugs. Follow
  reproduce → minimise → hypothesise → instrument → fix.
- **`deep-web-research`** — for microWakeWord/ESP32-S3/ESPHome version research;
  cite sources and verify current APIs.
- **`tdd`** — if extracting WAV/multipart/JSON helpers into host-testable code.
- **`create-agents-md`** — if the next step is adding repo-level agent
  instructions (the repo currently has no `AGENTS.md`).
- **`document-repository`** — if expanding the existing docs set.

## First actions on a new machine

```bash
git clone https://github.com/kkazakov/hass-bg-device.git
cd hass-bg-device
make secrets
$EDITOR firmware/secrets.yaml
$EDITOR firmware/bg-voice.yaml   # gateway_url
make config
make compile
```

Then read `docs/PLAN.md` and `docs/TESTING.md` before touching hardware. Do not
copy a local `firmware/secrets.yaml` between machines; create a fresh one from
the example.
