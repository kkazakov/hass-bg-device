# `bg_voice` ESPHome component

Owns the satellite behaviour after the wake word fires:

1. keep a short pre-roll of microphone audio;
2. record until VAD silence, max duration, or buffer full;
3. build a 16 kHz mono PCM16 WAV;
4. `POST` it as `multipart/form-data` to the gateway's `/v1/voice`;
5. parse the JSON reply and, if `speak: true`, download `audio_url` and play it;
6. mute the microphone while the reply plays (half-duplex).

## YAML

```yaml
bg_voice:
  id: bg_voice_component
  microphone: mic
  speaker: spk
  state_sensor: voice_state          # optional text sensor
  gateway_url: "http://192.168.1.10:8125"
  gateway_api_key: !secret gateway_api_key
  vad_threshold: 500                 # peak int16 amplitude
  silence_ms: 800ms
  max_command_ms: 12000ms
  pre_roll_ms: 300ms
  no_speech_timeout_ms: 3000ms
  http_timeout_ms: 30000ms
```

The microphone must be 16 kHz, 16-bit, mono. The component fails setup with a
clear log message if it is not.

## Triggers

```yaml
micro_wake_word:
  on_wake_word_detected:
    - lambda: |-
        id(bg_voice_component).on_wake_word();

binary_sensor:
  - platform: gpio
    pin: GPIO0
    on_press:
      - lambda: |-
          id(bg_voice_component).start_listening();
```

## Behaviour notes

- **Pre-roll** is prepended to the command so the first syllable after the wake
  word is not lost. It may include the tail of the wake word; the gateway's
  matcher should tolerate a leading wake phrase. Reduce `pre_roll_ms` if needed.
- **VAD** is a simple peak threshold, not a neural VAD. Tune `vad_threshold` on
  the real enclosure/mic. `silence_ms` is the hangover after the last loud
  frame; `max_command_ms` caps a monologue.
- **No speech** within `no_speech_timeout_ms` discards the command.
- **Half-duplex:** the mic is muted during playback. This avoids the device
  hearing its own TTS. There is no AEC in v1.
- **Reply audio** must be PCM16 mono WAV, 16–48 kHz. The speaker is reconfigured
  to the WAV's sample rate. MP3 is rejected with a log message.
- **API key** is sent on both the upload and the audio download, matching the
  Android app's behaviour. Empty key means no header.
- **Memory:** command buffer is `max_command_ms * 32` bytes in PSRAM; the reply
  buffer is up to 2 MB in PSRAM. N16R8 (8 MB) is required.

## Not implemented (yet)

- AEC / barge-in
- MP3 decoding
- TLS certificate validation
- dynamic config / captive portal
- multi-room identity header
- retries/queueing when the gateway is down
