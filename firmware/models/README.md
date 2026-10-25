# Wake-word models

## Bring-up model

`bg-voice.yaml` currently uses the stock **Okay Nabu** model through ESPHome's
model-name shorthand. ESPHome downloads it from
[`esphome/micro-wake-word-models`](https://github.com/esphome/micro-wake-word-models)
at build time. That repository is Apache-2.0; the model is used here only to
bring up the audio path.

This is not the final wake word. It exists so the device can be flashed and
tested before the Bulgarian model is trained.

## Custom „окей дом“ model

Put two files here:

```
models/
  okay_dom.json      # microWakeWord manifest
  okay_dom.tflite    # INT8 streaming model
```

Example manifest (the trainer should emit the real values; this is only the
shape):

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

Then point the YAML at it:

```yaml
micro_wake_word:
  models:
    - model:
        type: local
        path: models/okay_dom.json
      probability_cutoff: 0.5
      sliding_window_size: 10
```

Training, data and evaluation: [`../../docs/WAKE_WORD.md`](../../docs/WAKE_WORD.md).

## Licensing

Do not commit a model whose training data/licence you cannot redistribute.
Record the source, TTS voices, datasets and evaluation numbers in this file
when the final model is added.
