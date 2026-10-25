# Hardware

## Bill of materials (roughly $25–35)

| Part | Example | Qty | Approx. | Notes |
|---|---|---:|---:|---|
| MCU board | ESP32-S3-DevKitC-1 **N16R8** | 1 | $10–13 | 16 MB flash, 8 MB octal PSRAM |
| I2S mic | INMP441 breakout | 1 | $2–4 | 3.3 V, left channel |
| I2S amp | MAX98357A breakout | 1 | $3–5 | 5 V, mono |
| Speaker | 3 W, 4 Ω, 40–50 mm | 1 | $2–4 | full range |
| Button | 6×6 mm momentary | 1 | $0.2 | optional; boot button works |
| PSU | USB-C 5 V, ≥ 1 A | 1 | $4–6 | quality supply; amp peaks |
| Passives | 470 µF + 100 nF | 1 set | $0.5 | near MAX98357A VIN |
| Case | 3D print / project box | 1 | $2–5 | mic port + speaker grille |

The ESP32-S3 **N16R8** is required: the command buffer (12 s ≈ 384 KB) and the
reply buffer (up to 2 MB) live in PSRAM. A no-PSRAM S3 SuperMini will fail at
runtime.

## Wiring

### INMP441 → ESP32-S3

| INMP441 | ESP32-S3 | Note |
|---|---|---|
| VDD | 3V3 | do not use 5 V |
| GND | GND | common ground |
| SCK | GPIO4 | I2S bit clock |
| WS | GPIO5 | I2S word select / LRCLK |
| SD | GPIO6 | I2S data out |
| L/R | GND | selects left channel |

`channel: left` in `bg-voice.yaml` matches `L/R → GND`. If you tie `L/R` to
3V3, change the YAML to `channel: right`.

### MAX98357A → ESP32-S3

| MAX98357A | ESP32-S3 | Note |
|---|---|---|
| VIN | 5V | 2.5–5.5 V; 5 V recommended |
| GND | GND | common ground |
| BCLK | GPIO15 | I2S bit clock |
| LRC | GPIO16 | I2S word select / LRCLK |
| DIN | GPIO17 | I2S data in |
| SD | 3V3 / module default | shutdown/enable; keep enabled |
| GAIN | module default | leave floating unless you need more gain |
| + / − | speaker | 4 Ω, 3 W |

Add a **470 µF** electrolytic across VIN/GND at the amplifier and a 100 nF
ceramic close to the chip. Speaker current peaks cause brownouts on weak USB
supplies.

### Button and LED

| Signal | ESP32-S3 | Note |
|---|---|---|
| Button | GPIO0 → GND | `INPUT_PULLUP`, `inverted: true`; boot button works after boot |
| Status LED | GPIO48 | optional; many DevKitC-1 boards have a WS2812 there |

Do not hold GPIO0 low while resetting unless you want download mode.

## Pin map (matches `bg-voice.yaml`)

| Function | GPIO |
|---|---:|
| I2S mic BCLK | 4 |
| I2S mic LRCLK | 5 |
| I2S mic DIN | 6 |
| I2S amp BCLK | 15 |
| I2S amp LRCLK | 16 |
| I2S amp DOUT | 17 |
| Button | 0 |
| Optional LED | 48 |

## Power

- 5 V USB-C, at least 1 A. The S3 + mic + Wi-Fi is ~0.5 W; the amplifier adds
  peaks.
- Keep the mic and speaker grounds star-connected at the board's GND.
- Keep I2S wires short (< 10 cm) and away from the speaker wires.
- If the board reboots when the reply starts, add bulk capacitance and try a
  better supply before changing software.

## Mechanical

- Put the INMP441 behind a small hole or a fabric-covered opening; do not seal
  it in a airtight box.
- Keep the mic ≥ 3 cm from the speaker and, if possible, on a different face.
- Add a speaker grille and a little air volume; a bare speaker in a small box
  sounds thin.
- Strain-relieve the USB cable.
- Label the button and, if added, the mute switch.

## Bring-up order

1. Power the board alone; check 3.3 V and 5 V rails.
2. Flash the stock firmware; check serial logs.
3. Verify the mic: say “Okay Nabu”; `micro_wake_word` should log a detection.
4. Verify the speaker: use the mock gateway and a test WAV.
5. Verify the button: press it; state goes `listening`.
6. Assemble the case; repeat the wake test at 1 m and 3 m.
7. Run a 1-hour soak with periodic commands; check free heap/PSRAM.

## Alternatives

| Board | Pros | Cons |
|---|---|---|
| **ESP32-S3-BOX-3** | dual mic, speaker, case, display | stock firmware is HA-centric; reflash and change pins |
| **ReSpeaker Lite** (XIAO ESP32-S3 + 2-mic array) | small, better far-field, case | pin map differs; check I2S wiring |
| **Home Assistant Voice PE** | XMOS AEC, dual mic, mute switch, finished case | HA-first firmware; not this ESPHome config out of the box |
| **ESP32-S3-Korvo-2** | dual mic, codec, speaker | bigger, more expensive |

The firmware is pin-agnostic; update the `i2s_audio` buses and the
`microphone`/`speaker` entries when changing boards.

## Known hardware limits

- INMP441 is a single mic; 3–5 m in a noisy room will be marginal. A 2-mic
  array or the BOX-3/Voice PE is better for far-field.
- No hardware AEC. The firmware uses half-duplex (mic muted during playback).
- No physical mute switch in v1.
- The DevKitC-1 has no onboard speaker; the MAX98357A is required for replies.
  If you only need commands, omit the amp/speaker and let HA announce
  elsewhere.
