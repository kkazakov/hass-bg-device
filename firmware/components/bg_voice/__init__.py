import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import json, microphone, speaker, text_sensor
from esphome.const import CONF_ID

CODEOWNERS = ["@kkazakov"]
AUTO_LOAD = ["json"]
DEPENDENCIES = ["microphone", "speaker", "text_sensor"]

bg_voice_ns = cg.esphome_ns.namespace("bg_voice")
BgVoice = bg_voice_ns.class_("BgVoice", cg.Component)

CONF_GATEWAY_URL = "gateway_url"
CONF_GATEWAY_API_KEY = "gateway_api_key"
CONF_MICROPHONE = "microphone"
CONF_SPEAKER = "speaker"
CONF_STATE_SENSOR = "state_sensor"
CONF_VAD_THRESHOLD = "vad_threshold"
CONF_SILENCE_MS = "silence_ms"
CONF_MAX_COMMAND_MS = "max_command_ms"
CONF_PRE_ROLL_MS = "pre_roll_ms"
CONF_NO_SPEECH_TIMEOUT_MS = "no_speech_timeout_ms"
CONF_HTTP_TIMEOUT_MS = "http_timeout_ms"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BgVoice),
        cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Required(CONF_SPEAKER): cv.use_id(speaker.Speaker),
        cv.Required(CONF_GATEWAY_URL): cv.url,
        cv.Optional(CONF_GATEWAY_API_KEY, default=""): cv.string,
        cv.Optional(CONF_STATE_SENSOR): cv.use_id(text_sensor.TextSensor),
        cv.Optional(CONF_VAD_THRESHOLD, default=500): cv.int_range(min=1, max=32767),
        cv.Optional(CONF_SILENCE_MS, default="800ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_MAX_COMMAND_MS, default="12000ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PRE_ROLL_MS, default="300ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_NO_SPEECH_TIMEOUT_MS, default="3000ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_HTTP_TIMEOUT_MS, default="30000ms"): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_microphone(await cg.get_variable(config[CONF_MICROPHONE])))
    cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
    cg.add(var.set_gateway_url(config[CONF_GATEWAY_URL]))
    cg.add(var.set_api_key(config[CONF_GATEWAY_API_KEY]))

    if CONF_STATE_SENSOR in config:
        cg.add(var.set_state_sensor(await cg.get_variable(config[CONF_STATE_SENSOR])))

    cg.add(var.set_vad_threshold(config[CONF_VAD_THRESHOLD]))
    cg.add(var.set_silence_ms(config[CONF_SILENCE_MS].total_milliseconds))
    cg.add(var.set_max_command_ms(config[CONF_MAX_COMMAND_MS].total_milliseconds))
    cg.add(var.set_pre_roll_ms(config[CONF_PRE_ROLL_MS].total_milliseconds))
    cg.add(var.set_no_speech_timeout_ms(config[CONF_NO_SPEECH_TIMEOUT_MS].total_milliseconds))
    cg.add(var.set_http_timeout_ms(config[CONF_HTTP_TIMEOUT_MS].total_milliseconds))

    cg.add_global(bg_voice_ns.using)
