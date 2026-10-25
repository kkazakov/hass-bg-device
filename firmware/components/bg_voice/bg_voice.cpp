#include "bg_voice.h"

#include "esphome/components/audio/audio.h"
#include "esphome/components/json/json_util.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstring>

namespace esphome {
namespace bg_voice {

static const char *const TAG = "bg_voice";

static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr uint16_t BITS_PER_SAMPLE = 16;
static constexpr uint16_t CHANNELS = 1;
static constexpr size_t WAV_HEADER_SIZE = 44;
static constexpr size_t MAX_RESPONSE_BYTES = 16384;
static constexpr size_t MAX_AUDIO_BYTES = 2 * 1024 * 1024;

static inline uint16_t read_u16(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

static inline uint32_t read_u32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

static inline void write_u16(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xff);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

static inline void write_u32(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xff);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xff);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xff);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xff);
}

static void write_wav_header(uint8_t *p, uint32_t pcm_len) {
  std::memcpy(p, "RIFF", 4);
  write_u32(p + 4, 36 + pcm_len);
  std::memcpy(p + 8, "WAVE", 4);
  std::memcpy(p + 12, "fmt ", 4);
  write_u32(p + 16, 16);           // PCM fmt chunk size
  write_u16(p + 20, 1);            // PCM
  write_u16(p + 22, CHANNELS);
  write_u32(p + 24, SAMPLE_RATE);
  write_u32(p + 28, SAMPLE_RATE * CHANNELS * (BITS_PER_SAMPLE / 8));
  write_u16(p + 32, CHANNELS * (BITS_PER_SAMPLE / 8));
  write_u16(p + 34, BITS_PER_SAMPLE);
  std::memcpy(p + 36, "data", 4);
  write_u32(p + 40, pcm_len);
}

static bool write_all(esp_http_client_handle_t client, const uint8_t *data, size_t len) {
  size_t written = 0;
  while (written < len) {
    const int n = esp_http_client_write(client, reinterpret_cast<const char *>(data + written),
                                        static_cast<int>(len - written));
    if (n <= 0) {
      return false;
    }
    written += static_cast<size_t>(n);
  }
  return true;
}

static bool read_response(esp_http_client_handle_t client, std::string &out) {
  uint8_t buffer[512];
  int n = 0;
  while ((n = esp_http_client_read(client, reinterpret_cast<char *>(buffer), sizeof(buffer))) > 0) {
    out.append(reinterpret_cast<const char *>(buffer), static_cast<size_t>(n));
    if (out.size() > MAX_RESPONSE_BYTES) {
      return false;
    }
  }
  return n == 0;
}

const char *BgVoice::state_to_string(State state) {
  switch (state) {
    case State::IDLE:
      return "idle";
    case State::LISTENING:
      return "listening";
    case State::THINKING:
      return "thinking";
    case State::SPEAKING:
      return "speaking";
    case State::ERROR:
      return "error";
  }
  return "unknown";
}

size_t BgVoice::ms_to_bytes_(uint32_t ms) const {
  return static_cast<size_t>(SAMPLE_RATE) * (BITS_PER_SAMPLE / 8) * CHANNELS * ms / 1000;
}

bool BgVoice::allocate_buffers_() {
  this->pre_roll_capacity_ = this->ms_to_bytes_(this->pre_roll_ms_);
  this->command_capacity_ = this->ms_to_bytes_(this->max_command_ms_);

  if (this->pre_roll_capacity_ > 0) {
    this->pre_roll_ = static_cast<uint8_t *>(
        heap_caps_malloc(this->pre_roll_capacity_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->pre_roll_ == nullptr) {
      this->pre_roll_ = static_cast<uint8_t *>(malloc(this->pre_roll_capacity_));
    }
  }

  this->command_ = static_cast<uint8_t *>(
      heap_caps_malloc(WAV_HEADER_SIZE + this->command_capacity_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->command_ == nullptr) {
    this->command_ = static_cast<uint8_t *>(malloc(WAV_HEADER_SIZE + this->command_capacity_));
  }

  if ((this->pre_roll_capacity_ > 0 && this->pre_roll_ == nullptr) || this->command_ == nullptr) {
    ESP_LOGE(TAG, "Not enough memory for audio buffers (pre-roll %u B, command %u B). "
                  "Enable PSRAM (8 MB) and use an N16R8 board.",
             static_cast<unsigned>(this->pre_roll_capacity_),
             static_cast<unsigned>(WAV_HEADER_SIZE + this->command_capacity_));
    return false;
  }

  this->pre_roll_len_ = 0;
  this->command_len_ = 0;
  return true;
}

void BgVoice::setup() {
  if (this->mic_ == nullptr || this->speaker_ == nullptr) {
    ESP_LOGE(TAG, "microphone and speaker are required");
    this->mark_failed();
    return;
  }

  const auto info = this->mic_->get_audio_stream_info();
  if (info.get_bits_per_sample() != BITS_PER_SAMPLE || info.get_channels() != CHANNELS ||
      info.get_sample_rate() != SAMPLE_RATE) {
    ESP_LOGE(TAG, "Microphone must be %u Hz, %u-bit, %u channel(s); got %u Hz, %u-bit, %u channel(s)",
             static_cast<unsigned>(SAMPLE_RATE), static_cast<unsigned>(BITS_PER_SAMPLE),
             static_cast<unsigned>(CHANNELS), static_cast<unsigned>(info.get_sample_rate()),
             static_cast<unsigned>(info.get_bits_per_sample()), static_cast<unsigned>(info.get_channels()));
    this->mark_failed();
    return;
  }

  if (!this->allocate_buffers_()) {
    this->mark_failed();
    return;
  }

  this->mic_->add_data_callback([this](const std::vector<uint8_t> &data) { this->on_audio_(data); });
  this->speaker_->set_audio_stream_info(audio::AudioStreamInfo(BITS_PER_SAMPLE, CHANNELS, SAMPLE_RATE));
  this->state_ = State::IDLE;
  ESP_LOGI(TAG, "Ready (gateway=%s, api key %s)", this->gateway_url_.c_str(),
           this->api_key_.empty() ? "not set" : "set");
}

void BgVoice::dump_config() {
  ESP_LOGCONFIG(TAG, "BG Voice:");
  ESP_LOGCONFIG(TAG, "  Gateway: %s", this->gateway_url_.c_str());
  ESP_LOGCONFIG(TAG, "  API key: %s", this->api_key_.empty() ? "not set" : "set");
  ESP_LOGCONFIG(TAG, "  VAD threshold: %u", static_cast<unsigned>(this->vad_threshold_));
  ESP_LOGCONFIG(TAG, "  Silence: %u ms", static_cast<unsigned>(this->silence_ms_));
  ESP_LOGCONFIG(TAG, "  Max command: %u ms", static_cast<unsigned>(this->max_command_ms_));
  ESP_LOGCONFIG(TAG, "  Pre-roll: %u ms", static_cast<unsigned>(this->pre_roll_ms_));
  ESP_LOGCONFIG(TAG, "  No-speech timeout: %u ms", static_cast<unsigned>(this->no_speech_timeout_ms_));
  ESP_LOGCONFIG(TAG, "  HTTP timeout: %u ms", static_cast<unsigned>(this->http_timeout_ms_));
}

void BgVoice::on_wake_word() { this->begin_listening_("wake word"); }

void BgVoice::start_listening() { this->begin_listening_("button"); }

void BgVoice::begin_listening_(const char *reason) {
  std::lock_guard<std::mutex> lock(this->buffer_mutex_);
  const State state = this->state_.load();
  if (this->upload_task_started_.load() || state == State::LISTENING || state == State::THINKING ||
      state == State::SPEAKING) {
    ESP_LOGD(TAG, "Ignoring %s: busy in state %s", reason, state_to_string(state));
    return;
  }

  const uint32_t now = millis();
  this->command_len_ = 0;
  this->speech_seen_ = false;
  this->recording_done_ = false;
  this->listen_start_ms_ = now;
  this->last_voice_ms_ = now;
  this->wake_requested_ = true;
  this->state_ = State::LISTENING;
  ESP_LOGI(TAG, "Listening (%s)", reason);
}

void BgVoice::on_audio_(const std::vector<uint8_t> &data) {
  std::lock_guard<std::mutex> lock(this->buffer_mutex_);

  if (this->wake_requested_.exchange(false)) {
    this->command_len_ = this->copy_pre_roll_to_command_();
    this->speech_seen_ = false;
    this->last_voice_ms_ = millis();
    ESP_LOGD(TAG, "Command starts with %u bytes of pre-roll",
             static_cast<unsigned>(this->command_len_));
  }

  if (this->state_.load() == State::LISTENING) {
    this->append_command_(data.data(), data.size());

    uint32_t peak = 0;
    for (size_t i = 0; i + 1 < data.size(); i += 2) {
      const int16_t sample = static_cast<int16_t>(read_u16(data.data() + i));
      const uint32_t magnitude =
          sample < 0 ? static_cast<uint32_t>(-static_cast<int32_t>(sample)) : static_cast<uint32_t>(sample);
      if (magnitude > peak) {
        peak = magnitude;
      }
    }
    if (peak >= this->vad_threshold_) {
      this->speech_seen_ = true;
      this->last_voice_ms_ = millis();
    }

    if (this->command_len_ >= this->command_capacity_) {
      this->finish_recording_();
    }
  }

  this->append_pre_roll_(data.data(), data.size());
}

void BgVoice::append_pre_roll_(const uint8_t *data, size_t len) {
  if (this->pre_roll_capacity_ == 0 || len == 0) {
    return;
  }

  // Linear history buffer: simple and cheap at these sizes (a few KB).
  if (len >= this->pre_roll_capacity_) {
    std::memcpy(this->pre_roll_, data + (len - this->pre_roll_capacity_), this->pre_roll_capacity_);
    this->pre_roll_len_ = this->pre_roll_capacity_;
    return;
  }

  if (this->pre_roll_len_ + len > this->pre_roll_capacity_) {
    const size_t drop = this->pre_roll_len_ + len - this->pre_roll_capacity_;
    std::memmove(this->pre_roll_, this->pre_roll_ + drop, this->pre_roll_len_ - drop);
    this->pre_roll_len_ -= drop;
  }
  std::memcpy(this->pre_roll_ + this->pre_roll_len_, data, len);
  this->pre_roll_len_ += len;
}

size_t BgVoice::copy_pre_roll_to_command_() {
  if (this->pre_roll_len_ == 0 || this->pre_roll_capacity_ == 0) {
    return 0;
  }
  std::memcpy(this->command_ + WAV_HEADER_SIZE, this->pre_roll_, this->pre_roll_len_);
  return this->pre_roll_len_;
}

void BgVoice::append_command_(const uint8_t *data, size_t len) {
  if (this->command_len_ >= this->command_capacity_) {
    return;
  }
  const size_t space = this->command_capacity_ - this->command_len_;
  const size_t copy_len = std::min(len, space);
  std::memcpy(this->command_ + WAV_HEADER_SIZE + this->command_len_, data, copy_len);
  this->command_len_ += copy_len;
  if (copy_len < len) {
    this->finish_recording_();
  }
}

void BgVoice::finish_recording_() {
  if (this->state_.load() != State::LISTENING) {
    return;
  }
  this->state_ = State::THINKING;
  this->recording_done_ = true;
  const float seconds = static_cast<float>(this->command_len_) /
                        static_cast<float>(this->ms_to_bytes_(1000));
  ESP_LOGI(TAG, "Recorded %u bytes (%.1f s)", static_cast<unsigned>(this->command_len_), seconds);
}

void BgVoice::abort_recording_() {
  if (this->state_.load() != State::LISTENING) {
    return;
  }
  this->state_ = State::IDLE;
  this->command_len_ = 0;
  this->recording_done_ = false;
  ESP_LOGW(TAG, "No speech detected; command discarded");
}

void BgVoice::set_error_(const char *message) {
  ESP_LOGE(TAG, "%s", message);
  this->state_ = State::ERROR;
  this->error_since_ms_ = millis();
}

void BgVoice::loop() {
  if (this->state_.load() == State::LISTENING) {
    std::lock_guard<std::mutex> lock(this->buffer_mutex_);
    const uint32_t now = millis();
    const uint32_t start = this->listen_start_ms_.load();
    if (!this->speech_seen_.load() && now - start > this->no_speech_timeout_ms_) {
      this->abort_recording_();
    } else if (this->speech_seen_.load() && now - this->last_voice_ms_.load() > this->silence_ms_) {
      this->finish_recording_();
    } else if (now - start > this->max_command_ms_) {
      this->finish_recording_();
    }
  }

  if (this->state_.load() == State::THINKING && this->recording_done_.load() &&
      !this->upload_task_started_.exchange(true)) {
    this->start_upload_task_();
  }

  if (this->state_.load() == State::ERROR && millis() - this->error_since_ms_ > 3000) {
    this->state_ = State::IDLE;
  }

  this->publish_state_();
}

void BgVoice::start_upload_task_() {
  const BaseType_t result =
      xTaskCreate(&BgVoice::upload_task_, "bg_voice_upload", 8192, this, 5, nullptr);
  if (result != pdPASS) {
    this->upload_task_started_ = false;
    this->set_error_("Failed to create upload task");
  }
}

void BgVoice::upload_task_(void *arg) {
  auto *self = static_cast<BgVoice *>(arg);
  self->process_command_();
  self->upload_task_started_ = false;
  vTaskDelete(nullptr);
}

void BgVoice::process_command_() {
  size_t pcm_len = 0;
  {
    std::lock_guard<std::mutex> lock(this->buffer_mutex_);
    pcm_len = this->command_len_;
    if (pcm_len > 0) {
      write_wav_header(this->command_, static_cast<uint32_t>(pcm_len));
    }
  }

  if (pcm_len == 0) {
    this->set_error_("Empty command buffer");
    return;
  }

  const size_t wav_len = WAV_HEADER_SIZE + pcm_len;
  const std::string boundary = "----bgvoice" + std::to_string(esp_random());
  const std::string preamble = "--" + boundary +
                               "\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"command.wav\""
                               "\r\nContent-Type: audio/wav\r\n\r\n";
  const std::string footer = "\r\n--" + boundary + "--\r\n";

  const std::string url = this->join_url_(this->gateway_url_, "/v1/voice");
  std::string response;
  int status = 0;

  ESP_LOGI(TAG, "POST %s (%u bytes WAV)", url.c_str(), static_cast<unsigned>(wav_len));
  if (!this->http_post_multipart_(url, this->command_, wav_len, boundary, preamble, footer, response, status)) {
    this->set_error_("Gateway POST failed");
    return;
  }

  if (status != 200) {
    ESP_LOGE(TAG, "Gateway returned HTTP %d: %.200s", status, response.c_str());
    this->set_error_("Gateway returned an error");
    return;
  }

  bool speak = false;
  std::string audio_url;
  std::string kind;
  std::string text;
  std::string transcript;
  const bool parsed = json::parse_json(response, [&](JsonObject root) {
    speak = root["speak"] | false;
    audio_url = root["audio_url"] | "";
    kind = root["kind"] | "";
    text = root["text"] | "";
    transcript = root["transcript"] | "";
    return true;
  });

  if (!parsed) {
    this->set_error_("Invalid JSON from gateway");
    return;
  }

  ESP_LOGI(TAG, "Gateway: kind=%s speak=%s text=%s", kind.c_str(), speak ? "true" : "false", text.c_str());
  if (!transcript.empty()) {
    ESP_LOGD(TAG, "Transcript: %s", transcript.c_str());
  }

  if (speak && !audio_url.empty()) {
    this->state_ = State::SPEAKING;
    const std::string audio_full_url = this->join_url_(this->gateway_url_, audio_url);
    auto *audio = static_cast<uint8_t *>(
        heap_caps_malloc(MAX_AUDIO_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (audio == nullptr) {
      this->set_error_("Not enough PSRAM for reply audio");
      return;
    }

    size_t audio_len = 0;
    int audio_status = 0;
    const bool audio_ok = this->http_get_(audio_full_url, audio, MAX_AUDIO_BYTES, audio_len, audio_status);
    if (!audio_ok || audio_status != 200) {
      ESP_LOGW(TAG, "Reply audio download failed (HTTP %d)", audio_status);
    } else if (!this->play_wav_(audio, audio_len)) {
      ESP_LOGW(TAG, "Reply is not a playable PCM16 mono WAV; skipping playback");
    }
    free(audio);
  }

  this->state_ = State::IDLE;
  this->recording_done_ = false;
}

bool BgVoice::http_post_multipart_(const std::string &url, const uint8_t *wav, size_t wav_len,
                                   const std::string &boundary, const std::string &preamble,
                                   const std::string &footer, std::string &response, int &status) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = static_cast<int>(this->http_timeout_ms_);
  config.buffer_size = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_POST);
  const std::string content_type = "multipart/form-data; boundary=" + boundary;
  esp_http_client_set_header(client, "Content-Type", content_type.c_str());
  esp_http_client_set_header(client, "Accept", "application/json");
  if (!this->api_key_.empty()) {
    esp_http_client_set_header(client, "X-API-Key", this->api_key_.c_str());
  }

  const size_t content_length = preamble.size() + wav_len + footer.size();
  bool ok = esp_http_client_open(client, static_cast<int>(content_length));
  if (ok) {
    ok = write_all(client, reinterpret_cast<const uint8_t *>(preamble.data()), preamble.size());
  }
  if (ok) {
    ok = write_all(client, wav, wav_len);
  }
  if (ok) {
    ok = write_all(client, reinterpret_cast<const uint8_t *>(footer.data()), footer.size());
  }
  if (ok) {
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ok = read_response(client, response);
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

bool BgVoice::http_get_(const std::string &url, uint8_t *buffer, size_t capacity, size_t &length, int &status) {
  length = 0;
  status = 0;

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.timeout_ms = static_cast<int>(this->http_timeout_ms_);
  config.buffer_size = 4096;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) {
    return false;
  }

  esp_http_client_set_method(client, HTTP_METHOD_GET);
  esp_http_client_set_header(client, "Accept", "audio/wav, audio/x-wav, application/octet-stream");
  if (!this->api_key_.empty()) {
    esp_http_client_set_header(client, "X-API-Key", this->api_key_.c_str());
  }

  bool ok = esp_http_client_open(client, 0);
  if (ok) {
    esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    if (status == 200) {
      const int64_t content_length = esp_http_client_get_content_length(client);
      if (content_length > static_cast<int64_t>(capacity)) {
        ESP_LOGE(TAG, "Reply audio too large: %lld bytes (max %u)", static_cast<long long>(content_length),
                 static_cast<unsigned>(capacity));
        ok = false;
      } else {
        uint8_t chunk[512];
        int n = 0;
        while ((n = esp_http_client_read(client, reinterpret_cast<char *>(chunk), sizeof(chunk))) > 0) {
          if (length + static_cast<size_t>(n) > capacity) {
            ok = false;
            break;
          }
          std::memcpy(buffer + length, chunk, static_cast<size_t>(n));
          length += static_cast<size_t>(n);
        }
        if (n < 0) {
          ok = false;
        }
      }
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);
  return ok;
}

bool BgVoice::parse_wav_(const uint8_t *data, size_t len, uint16_t &channels, uint32_t &sample_rate,
                         uint16_t &bits_per_sample, const uint8_t **pcm, size_t &pcm_len) {
  channels = 0;
  sample_rate = 0;
  bits_per_sample = 0;
  *pcm = nullptr;
  pcm_len = 0;

  if (len < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0) {
    return false;
  }

  bool have_fmt = false;
  bool have_data = false;
  size_t pos = 12;
  while (pos + 8 <= len) {
    uint32_t chunk_len = read_u32(data + pos + 4);
    const uint8_t *chunk = data + pos + 8;
    const size_t available = len - (pos + 8);
    if (chunk_len > available) {
      chunk_len = static_cast<uint32_t>(available);
    }

    if (std::memcmp(data + pos, "fmt ", 4) == 0 && chunk_len >= 16) {
      const uint16_t format = read_u16(chunk);
      channels = read_u16(chunk + 2);
      sample_rate = read_u32(chunk + 4);
      bits_per_sample = read_u16(chunk + 14);
      if (format != 1) {
        return false;
      }
      have_fmt = true;
    } else if (std::memcmp(data + pos, "data", 4) == 0) {
      *pcm = chunk;
      pcm_len = chunk_len;
      have_data = true;
    }

    pos += 8 + chunk_len + (chunk_len & 1);
  }

  return have_fmt && have_data;
}

bool BgVoice::play_wav_(const uint8_t *data, size_t len) {
  uint16_t channels = 0;
  uint16_t bits = 0;
  uint32_t sample_rate = 0;
  const uint8_t *pcm = nullptr;
  size_t pcm_len = 0;

  if (!this->parse_wav_(data, len, channels, sample_rate, bits, &pcm, pcm_len)) {
    ESP_LOGW(TAG, "Reply is not a RIFF/WAVE file");
    return false;
  }
  if (bits != 16 || channels != 1) {
    ESP_LOGW(TAG, "Unsupported reply WAV: %u-bit, %u channel(s); need 16-bit mono", static_cast<unsigned>(bits),
             static_cast<unsigned>(channels));
    return false;
  }
  if (sample_rate < 16000 || sample_rate > 48000) {
    ESP_LOGW(TAG, "Unsupported reply sample rate: %u Hz", static_cast<unsigned>(sample_rate));
    return false;
  }
  if (pcm_len == 0) {
    return false;
  }

  this->mic_->set_mute_state(true);
  this->speaker_->set_audio_stream_info(audio::AudioStreamInfo(16, 1, sample_rate));
  this->speaker_->start();

  size_t written = 0;
  const uint32_t start = millis();
  while (written < pcm_len && millis() - start < 20000) {
    const size_t n = this->speaker_->play(pcm + written, pcm_len - written, pdMS_TO_TICKS(100));
    if (n == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
    } else {
      written += n;
    }
  }

  this->speaker_->finish();
  const uint32_t drain_start = millis();
  while (this->speaker_->has_buffered_data() && millis() - drain_start < 10000) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  this->speaker_->stop();
  this->mic_->set_mute_state(false);
  return true;
}

std::string BgVoice::join_url_(const std::string &base, const std::string &path) const {
  if (path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0) {
    return path;
  }

  std::string normalized = base;
  while (!normalized.empty() && normalized.back() == '/') {
    normalized.pop_back();
  }
  if (!path.empty() && path.front() == '/') {
    return normalized + path;
  }
  return normalized + "/" + path;
}

void BgVoice::publish_state_() {
  const State state = this->state_.load();
  if (state == this->last_published_state_) {
    return;
  }
  this->last_published_state_ = state;
  if (this->state_sensor_ != nullptr) {
    this->state_sensor_->publish_state(state_to_string(state));
  }
}

}  // namespace bg_voice
}  // namespace esphome
