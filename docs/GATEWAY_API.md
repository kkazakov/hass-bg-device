# Gateway API

The device is a client of [`hass-bg-gateway`](https://github.com/kkazakov/hass-bg-gateway).
It uses the same `POST /v1/voice` contract as the Android app, plus a keyed
download of the reply audio.

- Base URL: `http://<gateway-host>:8125` (default port **8125**).
- One gateway maps to one Home Assistant; there is no `instance` parameter.
- Commands live in the gateway's catalogue (`commands.yaml` or
  `BGV_COMMANDS_FILE`), not in the device.
- Optional auth: `X-API-Key: <BGV_API_KEY>`. If the gateway has no key, omit the
  header. Never send an empty key.

## Upload

```http
POST /v1/voice HTTP/1.1
Host: 192.168.1.10:8125
Content-Type: multipart/form-data; boundary=----bgvoice...
X-API-Key: <optional>
Accept: application/json
```

Body:

```
------bgvoice...
Content-Disposition: form-data; name="audio"; filename="command.wav"
Content-Type: audio/wav

<RIFF/WAVE bytes>
------bgvoice...--
```

Requirements:

| Property | Value |
|---|---|
| Container | WAV / RIFF |
| Encoding | PCM, 16-bit signed little-endian |
| Channels | 1 (mono) |
| Sample rate | 16,000 Hz |
| Duration | 0.3–12 s (device cap) |
| Field name | `audio` |
| Filename | `command.wav` (informational) |

The device prepends ~300 ms of pre-roll, so the WAV may begin with the tail of
the wake phrase (“…ей дом”). The gateway's ASR/matcher should tolerate a
leading wake phrase; if it does not, reduce `pre_roll_ms` or add a gateway-side
trim.

### curl

```bash
curl -sS -X POST "http://192.168.1.10:8125/v1/voice" \
  -H "X-API-Key: $BGV_API_KEY" \
  -F "audio=@/tmp/bg-voice-test.wav;type=audio/wav" \
  | python3 -m json.tool
```

Omit the `-H` line for a keyless gateway.

## Response

`200 OK`, `application/json`:

```json
{
  "kind": "query",
  "speak": true,
  "text": "Сега е 15 часа и 42 минути.",
  "transcript": "окей дом колко е часът",
  "audio_url": "/v1/audio/8f3c1a.wav",
  "matched": {
    "intent": "time",
    "score": 0.94
  },
  "timings": {
    "total_ms": 812
  },
  "reason": null
}
```

Fields used by the device:

| Field | Type | Use |
|---|---|---|
| `kind` | string | logged: `action`, `query`, `no_match` |
| `speak` | bool | if true and `audio_url` is set, download/play |
| `text` | string | logged |
| `transcript` | string | logged |
| `audio_url` | string | relative URL of the spoken reply |
| `matched.intent` | string | optional log |
| `matched.score` | number | optional log |
| `timings.total_ms` | number | optional log |
| `reason` | string/null | optional log |

The device does not require any other field. Unknown fields are ignored.

## Reply audio

```http
GET /v1/audio/8f3c1a.wav HTTP/1.1
Host: 192.168.1.10:8125
X-API-Key: <same key, if configured>
```

- `audio_url` is **relative**; the device joins it with the configured base URL.
- The gateway applies `X-API-Key` to **every** `/v1` route, including
  `/v1/audio/...`. The device sends the key on the audio GET as well.
- MP3 (gTTS) and WAV (Piper) are both valid gateway outputs. **This device only
  plays PCM16 mono WAV** in v1. Configure Piper/WAV for the satellite, or add
  MP3 decoding later.
- The device reconfigures the I2S speaker to the WAV's sample rate (16–48 kHz).
  Mono is required; stereo is rejected.

## Errors

| Status | Meaning | Device behaviour |
|---|---|---|
| `200` | success | parse JSON, maybe play |
| `400` | bad request / unreadable audio | log body, state `error` |
| `401` | missing/wrong `X-API-Key` | log body, state `error` |
| `404` | wrong path / gateway not mounted there | log body, state `error` |
| `5xx` | gateway/ASR failure | log body, state `error` |
| timeout / connect failure | gateway down / wrong URL / Wi-Fi | log, state `error` |

The device does not retry a command automatically in v1. It returns to `idle`
after 3 s; the user can repeat the wake word. Network retry/backoff is a Phase 4
item.

## Mock gateway

For firmware development without a real gateway:

```bash
make mock
# in another terminal:
make gateway-test
```

`tools/mock_gateway.py`:

- accepts `POST /v1/voice` and saves the uploaded WAV to
  `/tmp/bg-voice-last-upload.wav`;
- returns the JSON above with a generated 16 kHz WAV at `/v1/audio/test.wav`;
- optionally enforces `X-API-Key` (`--api-key`);
- can simulate errors (`--fail 500`) and `speak: false` (`--no-speak`).

Point the device at your workstation:

```yaml
substitutions:
  gateway_url: "http://192.168.1.50:18125"
```

## Notes

- The gateway default port is **8125**.
- Use `--host 0.0.0.0` (or the Docker equivalent) so the ESP32 can reach it.
- The gateway's HA token stays on the gateway; the device only holds the
  optional gateway key.
- For remote access, use LAN/Tailscale/WireGuard. Do not expose the gateway to
  the public internet; the device speaks plain HTTP.
