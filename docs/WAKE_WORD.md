# Wake word

## Target

Local, on-device detection of **„окей дом“**. Audio must not leave the device
until the phrase fires.

The checked-in firmware uses the stock **Okay Nabu** model for bring-up. This
document is the plan for replacing it with a custom model.

## Engine choice

| Option | Verdict |
|---|---|
| **microWakeWord** (ESPHome) | **Use this.** INT8 TFLite, streaming, a few ms per inference on ESP32-S3, ~50–120 KB model. |
| openWakeWord | Great for a laptop/Pi prototype, too slow on ESP32-S3 (seconds per frame). Use it to validate the phrase, not to ship. |
| ESP-SR / WakeNet | Built-in fixed wake words only. Custom WakeNet models are a proprietary `.bin` with no public converter. Not a DIY path. |
| Vosk / Whisper on-device | Too large/slow for an S3. Server-side only. |
| Porcupine | Commercial custom keywords; check Bulgarian support and pricing before relying on it. |

## The phrase problem

Bulgarian **окей дом** is roughly `/oˈkɛj ˈdɔm/`.
English **okay dome** is `/oʊˈkeɪ ˈdoʊm/`.

- “okay” is close enough.
- “дом” is a monophthong `/ɔ/`; “dome” is the diphthong `/oʊ/`. Training only on
  “dome” teaches the wrong vowel.
- The final `/m/` and the `/d/` are fine.

Practical consequence:

- Prefer English TTS spellings **“okay dom”** (BrE `/dɒm/`) and **“okay dawm”**
  (`/dɔːm/`), not “okay dome”.
- Better: train on the actual Bulgarian phrase as well.
- “окей дом” is short and “дом” is common. Expect false accepts from TV/radio.
  If it is too noisy, consider “окей, домакине” or “здравей, дом”, or a
  two-stage confirmation.

## Model format

microWakeWord exports a streaming INT8 `.tflite` plus a JSON manifest. ESPHome
reads the manifest, embeds the model in the firmware and runs it against the
16 kHz microphone.

Manifest shape (v2):

```json
{
  "type": "micro",
  "wake_word": "окей дом",
  "author": "kkazakov",
  "website": "https://github.com/kkazakov/hass-bg-device",
  "model": "okay_dom.tflite",
  "version": 2,
  "trained_languages": ["bg", "en"],
  "micro": {
    "feature_step_size": 10,
    "tensor_arena_size": 45672,
    "probability_cutoff": 0.5,
    "sliding_window_size": 10,
    "minimum_esphome_version": "2026.9.0"
  }
}
```

`tensor_arena_size`, `feature_step_size` and `sliding_window_size` must come
from the trainer; the values above are illustrative.

## Data

### Positives

Aim for 5,000–10,000 clips per training run, with speaker/accent/rate/noise
variation.

- **Bulgarian:** Piper voice `bg_BG-dimitar` (medium). It is one voice, so add
  speed/pitch jitter and real recordings.
- **English mimic:** “okay dom”, “okay dawm”, “okay dome” across several
  en_US/en_GB Piper voices. This adds speaker diversity and covers callers who
  pronounce it the English way.
- **Real recordings:** 30–50 utterances per speaker from 3–5 household voices,
  at 1 m and 3 m, with and without TV noise. These are for validation first;
  add them to training only if the pipeline supports custom positives.

Do not train only on “okay dome”. The vowel mismatch is the main risk.

### Negatives

- 100+ hours of ambient audio if possible: MUSAN, DEMAND, Common Voice.
- **Bulgarian TV, radio, podcasts and music** — the actual false-trigger source
  in the room. The default microWakeWord negatives are English-heavy.
- Hard negatives:
  - “окей”, “дом”, “окей добре”, “окей доктор”, “окей домат”
  - “okay”, “dome”, “dom”, “dawm”
  - phrases where “окей” is followed by a different word
- The device's own TTS replies (record them and add as negatives; half-duplex
  already mutes the mic, but this hardens the model).

### Augmentation

Room impulse responses, additive noise, gain, speed/pitch jitter, codec
degradation. This matters more than adding another TTS voice.

## Training

Two workable routes:

1. **[microWakeWord](https://github.com/kahrendt/microWakeWord)** — the upstream
   training notebooks. The README is explicit: training a usable model is for
   advanced users and “still very difficult”; budget iterations, not one
   afternoon.
2. **[customWakeWord](https://github.com/0thernes/customWakeWord)** — a pipeline
   that synthesises positives/hard negatives, trains microWakeWord, evaluates
   and exports an ESPHome-ready `.tflite`. Useful as a starting point; review
   its output and licences.

A GPU rental (e.g. RunPod A40) is a few dollars per run; the model is small.
The real cost is data curation and evaluation.

### Suggested loop

1. Generate a small positive set (2–3k) and a negative set.
2. Train a baseline; export.
3. Evaluate FRR on real household recordings and FAR/hour on 2–3 hours of
   Bulgarian TV/radio.
4. Inspect false accepts; add them as hard negatives; retrain.
5. Tune `probability_cutoff` and `sliding_window_size` on the device.
6. Only then replace the stock model.

## Evaluation

| Metric | Target | How |
|---|---|---|
| False-reject rate (FRR) | ≤ 10% | 30–50 real utterances per speaker, 1 m / 3 m, quiet / TV |
| False accepts per hour (FAR/h) | ≤ 1/h | 2–3 h Bulgarian TV/radio/podcasts, music, conversation |
| Wake latency | < 1 s after phrase | serial log timestamps |
| Model size | < 150 KB | `.tflite` |
| Inference | real-time on S3 | `micro_wake_word` logs / no ring-buffer warnings |

Test with the actual enclosure and microphone. A model that works on a laptop
with a USB mic can fail behind a 3 mm hole.

## Integration

1. Put `okay_dom.tflite` + `okay_dom.json` in `firmware/models/`.
2. Point `micro_wake_word` at the local manifest:

   ```yaml
   micro_wake_word:
     stop_after_detection: false
     models:
       - model:
           type: local
           path: models/okay_dom.json
         probability_cutoff: 0.5
         sliding_window_size: 10
   ```

3. `make compile && make upload`.
4. Say the phrase at 1 m, 3 m, with TV on; check the `Voice state` sensor.
5. Run the FAR test in the real room for at least an hour.

## Licensing and provenance

Record in `firmware/models/README.md`:

- TTS engines/voices and their licences;
- negative datasets and licences;
- training code/version and hyperparameters;
- evaluation numbers;
- the `.tflite` hash.

Do not commit a model trained on data you cannot redistribute. Piper voices
and the microWakeWord code are permissively licensed; check each dataset.
