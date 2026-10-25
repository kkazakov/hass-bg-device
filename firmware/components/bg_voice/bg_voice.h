#pragma once

#include "esphome/core/component.h"
#include "esphome/components/microphone/microphone.h"
#include "esphome/components/speaker/speaker.h"
#include "esphome/components/text_sensor/text_sensor.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace esphome {
namespace bg_voice {

class BgVoice : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_microphone(microphone::Microphone *mic) { this->mic_ = mic; }
  void set_speaker(speaker::Speaker *spk) { this->speaker_ = spk; }
  void set_state_sensor(text_sensor::TextSensor *sensor) { this->state_sensor_ = sensor; }
  void set_gateway_url(const std::string &url) { this->gateway_url_ = url; }
  void set_api_key(const std::string &key) { this->api_key_ = key; }
  void set_vad_threshold(uint32_t threshold) { this->vad_threshold_ = threshold; }
  void set_silence_ms(uint32_t ms) { this->silence_ms_ = ms; }
  void set_max_command_ms(uint32_t ms) { this->max_command_ms_ = ms; }
  void set_pre_roll_ms(uint32_t ms) { this->pre_roll_ms_ = ms; }
  void set_no_speech_timeout_ms(uint32_t ms) { this->no_speech_timeout_ms_ = ms; }
  void set_http_timeout_ms(uint32_t ms) { this->http_timeout_ms_ = ms; }

  /// Called by the micro_wake_word trigger.
  void on_wake_word();
  /// Called by the optional push button.
  void start_listening();

 protected:
  enum class State : uint8_t { IDLE, LISTENING, THINKING, SPEAKING, ERROR };

  static const char *state_to_string(State state);

  void begin_listening_(const char *reason);
  void on_audio_(const std::vector<uint8_t> &data);
  void append_pre_roll_(const uint8_t *data, size_t len);
  size_t copy_pre_roll_to_command_();
  void append_command_(const uint8_t *data, size_t len);
  void finish_recording_();
  void abort_recording_();
  void set_error_(const char *message);
  void start_upload_task_();
  static void upload_task_(void *arg);
  void process_command_();

  bool http_post_multipart_(const std::string &url, const uint8_t *wav, size_t wav_len,
                            const std::string &boundary, const std::string &preamble,
                            const std::string &footer, std::string &response, int &status);
  bool http_get_(const std::string &url, uint8_t *buffer, size_t capacity, size_t &length, int &status);
  bool play_wav_(const uint8_t *data, size_t len);
  bool parse_wav_(const uint8_t *data, size_t len, uint16_t &channels, uint32_t &sample_rate,
                  uint16_t &bits_per_sample, const uint8_t **pcm, size_t &pcm_len);
  std::string join_url_(const std::string &base, const std::string &path) const;
  void publish_state_();
  size_t ms_to_bytes_(uint32_t ms) const;
  bool allocate_buffers_();

  microphone::Microphone *mic_{nullptr};
  speaker::Speaker *speaker_{nullptr};
  text_sensor::TextSensor *state_sensor_{nullptr};

  std::string gateway_url_;
  std::string api_key_;

  uint32_t vad_threshold_{500};
  uint32_t silence_ms_{800};
  uint32_t max_command_ms_{12000};
  uint32_t pre_roll_ms_{300};
  uint32_t no_speech_timeout_ms_{3000};
  uint32_t http_timeout_ms_{30000};

  uint8_t *pre_roll_{nullptr};
  size_t pre_roll_capacity_{0};
  size_t pre_roll_len_{0};

  uint8_t *command_{nullptr};
  size_t command_capacity_{0};
  size_t command_len_{0};

  std::mutex buffer_mutex_;
  std::atomic<State> state_{State::IDLE};
  std::atomic<bool> wake_requested_{false};
  std::atomic<bool> recording_done_{false};
  std::atomic<bool> upload_task_started_{false};
  std::atomic<bool> speech_seen_{false};
  std::atomic<uint32_t> listen_start_ms_{0};
  std::atomic<uint32_t> last_voice_ms_{0};

  uint32_t error_since_ms_{0};
  State last_published_state_{State::IDLE};
};

}  // namespace bg_voice
}  // namespace esphome
