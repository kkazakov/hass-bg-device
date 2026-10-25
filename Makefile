ESPHOME ?= esphome
CONFIG ?= firmware/bg-voice.yaml
PYTHON ?= python3

.PHONY: help secrets config compile upload logs clean mock test-wav gateway-test

help:
	@echo "Targets:"
	@echo "  secrets       copy firmware/secrets.yaml.example to firmware/secrets.yaml"
	@echo "  config        validate the ESPHome configuration"
	@echo "  compile       build the firmware"
	@echo "  upload        build and flash over USB (first flash)"
	@echo "  logs          follow serial logs"
	@echo "  mock          run the local fake gateway on :18125"
	@echo "  test-wav      generate /tmp/bg-voice-test.wav"
	@echo "  gateway-test  POST the test WAV to the mock gateway"
	@echo "  clean         remove ESPHome build output"

secrets:
	@if [ ! -f firmware/secrets.yaml ]; then \
		cp firmware/secrets.yaml.example firmware/secrets.yaml; \
		echo "Created firmware/secrets.yaml — edit it before building."; \
	else \
		echo "firmware/secrets.yaml already exists."; \
	fi

config: secrets
	$(ESPHOME) config $(CONFIG)

compile: secrets
	$(ESPHOME) compile $(CONFIG)

upload: secrets
	$(ESPHOME) run $(CONFIG)

logs:
	$(ESPHOME) logs $(CONFIG)

mock:
	$(PYTHON) tools/mock_gateway.py --host 0.0.0.0 --port 18125

test-wav:
	$(PYTHON) tools/make_test_wav.py -o /tmp/bg-voice-test.wav

gateway-test: test-wav
	./tools/test_gateway.sh /tmp/bg-voice-test.wav

clean:
	rm -rf firmware/.esphome build
