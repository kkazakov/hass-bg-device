# Wake-word training notes

This directory is for the custom **„окей дом“** microWakeWord model. The
firmware repo does not vendor a training pipeline; use one of:

- [microWakeWord](https://github.com/kahrendt/microWakeWord) — upstream
  training notebooks and model export.
- [customWakeWord](https://github.com/0thernes/customWakeWord) — end-to-end
  synthesis/train/eval/export pipeline producing an ESPHome-compatible INT8
  `.tflite`.

Read [`../../docs/WAKE_WORD.md`](../../docs/WAKE_WORD.md) first. The short
version:

## 1. Phrase list

Edit [`phrases.yaml`](phrases.yaml). Keep the Bulgarian phrase as the primary
target and add English “okay dom” / “okay dawm” variants for speaker diversity.
Do **not** train only on “okay dome”: the English diphthong `/oʊ/` does not
match Bulgarian `/ɔ/`.

## 2. Positives

```bash
# illustrative — use the trainer's actual CLI
python scripts/synth_positives.py \
  --phrases phrases.yaml \
  --out data/okay_dom/synth/positives \
  --count 8000
```

- Bulgarian Piper voice: `bg_BG-dimitar` (medium).
- English Piper voices: several en_US/en_GB voices, speaking “okay dom”,
  “okay dawm”, “okay dome”.
- Add 30–50 real recordings per household speaker for validation (and training
  if the pipeline supports custom positives).

## 3. Negatives

- MUSAN / DEMAND / Common Voice ambient audio.
- **Bulgarian TV, radio, podcasts, music** — the real false-trigger source.
- Hard negatives from `phrases.yaml`.
- Recordings of the device's own TTS replies.

## 4. Train and export

Use the trainer's GPU/Colab flow. The output must be:

- streaming INT8 `.tflite`;
- a v2 manifest (`okay_dom.json`) with `feature_step_size`,
  `tensor_arena_size`, `probability_cutoff`, `sliding_window_size`,
  `trained_languages`, `minimum_esphome_version`;
- ideally < 150 KB.

## 5. Evaluate

- FRR ≤ 10% on real speakers at 1 m and 3 m, quiet and with TV.
- FAR ≤ 1/hour over 2–3 hours of Bulgarian audio.
- Test in the actual enclosure, not on a laptop.

## 6. Install

```bash
cp okay_dom.tflite okay_dom.json ../../firmware/models/
# edit ../../firmware/bg-voice.yaml:
#   model:
#     type: local
#     path: models/okay_dom.json
make compile
make upload
```

Record provenance and evaluation numbers in
[`../../firmware/models/README.md`](../../firmware/models/README.md).
