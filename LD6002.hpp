#pragma once
// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: LD6002 radar driver and LibXR service module
module_type: LD6002::Module
constructor_args: []
template_args: []
required_hardware:
  ld6002_uart
depends: []
=== END MANIFEST === */
// clang-format on

#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_rw.hpp"
#include "semaphore.hpp"
#include "timebase.hpp"
#include "uart.hpp"

namespace LD6002
{

inline constexpr uint8_t SOF = 0x01U;
inline constexpr size_t MAX_PAYLOAD_LEN = 128U;
inline constexpr uint16_t VERSION_QUERY_TYPE = 0xFFFFU;

inline constexpr uint16_t TYPE_FIRMWARE_STATUS = VERSION_QUERY_TYPE;
inline constexpr uint16_t TYPE_OTA = 0x3000U;
inline constexpr uint16_t TYPE_TEXT_MESSAGE = 0x0100U;
inline constexpr uint16_t TYPE_HUMAN_PRESENCE = 0x0F09U;
inline constexpr uint16_t TYPE_PERSONNEL_POSITION = 0x0A04U;
inline constexpr uint16_t TYPE_PHASE_TEST = 0x0A13U;
inline constexpr uint16_t TYPE_BREATH_RATE = 0x0A14U;
inline constexpr uint16_t TYPE_HEART_RATE = 0x0A15U;
inline constexpr uint16_t TYPE_TARGET_RANGE = 0x0A16U;
inline constexpr uint16_t TYPE_TRACK_POSITION = 0x0A17U;

inline constexpr size_t SERVICE_MAX_PERSON_TARGETS = 7U;

struct Version
{
  uint8_t project = 0;
  uint8_t version_major = 0;
  uint8_t version_minor = 0;
  uint8_t version_patch = 0;
};

struct Frame
{
  uint16_t id = 0;
  uint16_t type = 0;
  uint16_t len = 0;
  uint8_t data[MAX_PAYLOAD_LEN] = {0};
};

class Driver
{
 public:
  class FrameHandler
  {
   public:
    virtual ~FrameHandler() = default;
    virtual void OnFrame(const Frame& frame) = 0;
  };

  struct Config
  {
    LibXR::UART* uart = nullptr;
    uint32_t uart_read_timeout_ms = 0;
    uint32_t parser_timeout_ms = 50;
    bool auto_reply_version = false;
    Version local_version = {0, 1, 0, 0};
    FrameHandler* frame_handler = nullptr;
  };

  explicit Driver(const Config& config);

  bool IsInitialized() const;
  void ResetParser();
  void Poll();

  LibXR::ErrorCode SendFrame(uint16_t type, const void* payload, size_t payload_len);
  LibXR::ErrorCode RequestVersion();

  bool GetLastFrame(Frame& out_frame) const;

  static bool IsVersionQuery(const Frame& frame);
  static bool ParseVersionResponse(const Frame& frame, Version& out_version);
  static bool ParseU32LE(const Frame& frame, uint32_t& out_value);
  static bool ParseFloatLE(const Frame& frame, float& out_value);

 private:
  enum class ParserState : uint8_t
  {
    WAIT_SOF = 0,
    READ_ID,
    READ_LEN,
    READ_TYPE,
    READ_HEAD_CKSUM,
    READ_PAYLOAD,
    READ_BODY_CKSUM,
  };

  static uint8_t ChecksumAdd(uint8_t cksum, uint8_t byte);
  static uint8_t ChecksumFinalize(uint8_t cksum);
  static void WriteU16BE(uint8_t* dst, uint16_t value);

  void ResetParserState();
  bool StoreFrame(uint16_t id, uint16_t type, uint16_t len, const uint8_t* payload);
  void HandleFrame();
  void FeedByte(uint8_t byte);

  Config config_{};
  ParserState state_ = ParserState::WAIT_SOF;
  uint16_t next_id_ = 0;
  uint16_t current_id_ = 0;
  uint16_t current_len_ = 0;
  uint16_t current_type_ = 0;
  uint16_t field_value_ = 0;
  uint8_t field_index_ = 0;
  uint16_t payload_index_ = 0;
  uint8_t header_cksum_ = 0;
  uint8_t body_cksum_ = 0;
  bool discard_payload_ = false;
  bool initialized_ = false;
  bool has_last_frame_ = false;
  LibXR::MicrosecondTimestamp last_byte_us_{};
  Frame payload_frame_{};
  Frame last_frame_{};
};

enum class ServiceParseResult : uint8_t
{
  OK = 0,
  UNSUPPORTED,
  MALFORMED,
};

enum class ServiceEventKind : uint8_t
{
  UNKNOWN = 0,
  FIRMWARE_STATUS_QUERY,
  FIRMWARE_STATUS,
  OTA,
  TEXT_MESSAGE,
  HUMAN_PRESENCE,
  BREATH_RATE,
  HEART_RATE,
  TARGET_RANGE,
  TRACK_POSITION,
  PHASE_TEST,
  PERSONNEL_POSITION,
};

enum class ServicePresenceState : uint8_t
{
  NONE = 0,
  HUMAN = 1,
  UNKNOWN_VALUE = 2,
};

struct ServiceOta
{
  size_t payload_len = 0;
};

struct ServiceText
{
  char text[MAX_PAYLOAD_LEN + 1U] = {0};
};

struct ServicePresence
{
  uint16_t raw_value = 0;
  ServicePresenceState state = ServicePresenceState::NONE;
};

struct ServiceRate
{
  float bpm = 0.0f;
};

struct ServiceTargetRange
{
  uint32_t flag = 0;
  float range_cm = 0.0f;
};

struct ServiceTrackPosition
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  bool has_z = false;
};

struct ServicePhaseTest
{
  uint32_t raw_prefix = 0;
  float breath_phase = 0.0f;
  float heart_phase = 0.0f;
};

struct ServicePersonTarget
{
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  bool has_z = false;
  int32_t dop_idx = 0;
  int32_t cluster_id = 0;
};

struct ServicePersonnelPosition
{
  int32_t target_count = 0;
  uint8_t bytes_per_target = 0;
  ServicePersonTarget targets[SERVICE_MAX_PERSON_TARGETS] = {};
};

struct ServiceEvent
{
  ServiceEventKind kind = ServiceEventKind::UNKNOWN;
  ServiceParseResult result = ServiceParseResult::MALFORMED;
  const char* detail = nullptr;
  Frame frame = {};
  union
  {
    Version firmware_status;
    ServiceOta ota;
    ServiceText text;
    ServicePresence presence;
    ServiceRate breath_rate;
    ServiceRate heart_rate;
    ServiceTargetRange target_range;
    ServiceTrackPosition track_position;
    ServicePhaseTest phase_test;
    ServicePersonnelPosition personnel_position;
  } data = {};
};

struct ServiceSnapshot
{
  bool has_firmware_status = false;
  Version firmware_status = {};
  bool has_ota_message = false;
  size_t ota_payload_len = 0;
  bool has_text_message = false;
  ServiceText text_message = {};
  bool has_presence = false;
  ServicePresence presence = {};
  bool has_breath_rate = false;
  ServiceRate breath_rate = {};
  bool has_heart_rate = false;
  ServiceRate heart_rate = {};
  bool has_target_range = false;
  ServiceTargetRange target_range = {};
  bool has_track_position = false;
  ServiceTrackPosition track_position = {};
  bool has_phase_test = false;
  ServicePhaseTest phase_test = {};
  bool has_personnel_position = false;
  ServicePersonnelPosition personnel_position = {};
};

class Service : public Driver::FrameHandler
{
 public:
  class EventHandler
  {
   public:
    virtual ~EventHandler() = default;
    virtual void OnEvent(const ServiceEvent& event) = 0;
  };

  struct Config
  {
    Driver::Config driver_config = {};
    bool request_version_on_init = true;
    EventHandler* event_handler = nullptr;
  };

  explicit Service(const Config& config);

  bool IsInitialized() const;
  void Poll();
  void ResetParser();

  LibXR::ErrorCode RequestVersion();
  LibXR::ErrorCode SendFrame(uint16_t type, const void* payload, size_t payload_len);

  bool GetSnapshot(ServiceSnapshot& out_snapshot) const;
  bool GetLastEvent(ServiceEvent& out_event) const;

  static ServiceParseResult DecodeFrame(const Frame& frame, ServiceEvent& out_event);
  static const char* EventKindToString(ServiceEventKind kind);
  static const char* ParseResultToString(ServiceParseResult result);

  void OnFrame(const Frame& frame) override;

 private:
  static size_t PayloadLen(const Frame& frame);
  static uint16_t ReadU16LE(const uint8_t* data);
  static uint32_t ReadU32LE(const uint8_t* data);
  static int32_t ReadI32LE(const uint8_t* data);
  static float ReadF32LE(const uint8_t* data);
  static void BuildText(const Frame& frame, char* out_text, size_t out_size);
  static ServiceEventKind KindFromFrame(const Frame& frame);

  void UpdateSnapshot(const ServiceEvent& event);

  Driver driver_;
  EventHandler* event_handler_ = nullptr;
  ServiceSnapshot snapshot_{};
  ServiceEvent last_event_{};
  bool has_last_event_ = false;
};

namespace Detail
{
inline constexpr size_t MODULE_MAX_LISTENERS = 4U;
inline constexpr uint8_t kVersionQueryPayload[4] = {0x01, 0x01, 0x00, 0x00};
inline constexpr size_t kFrameOverhead = 9U;
inline constexpr size_t kPollChunkSize = 64U;

inline size_t ReadAvailableBytes(LibXR::UART& uart, uint8_t* buffer, size_t capacity)
{
  if ((buffer == nullptr) || (capacity == 0U))
  {
    return 0U;
  }

  const size_t available = uart.read_port_->Size();
  if (available == 0U)
  {
    return 0U;
  }

  const size_t take = (available < capacity) ? available : capacity;
  LibXR::Semaphore sem(0);
  LibXR::ReadOperation op(sem, 0);
  const auto ans = uart.Read(LibXR::RawData(buffer, take), op);
  return (ans == LibXR::ErrorCode::OK) ? take : 0U;
}
}  // namespace Detail

class Module : public LibXR::Application
{
 public:
  class EventListener
  {
   public:
    virtual ~EventListener() = default;
    virtual void OnLD6002Event(const ServiceEvent& event) = 0;
  };

  static constexpr const char* kRequiredUartAlias = "ld6002_uart";

  explicit Module(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& appmgr);

  bool RegisterListener(EventListener& listener);
  bool UnregisterListener(EventListener& listener);

  bool IsInitialized() const;
  void OnMonitor() override;

  bool GetSnapshot(ServiceSnapshot& out_snapshot) const;
  bool GetLastEvent(ServiceEvent& out_event) const;

  LibXR::ErrorCode RequestVersion();
  LibXR::ErrorCode SendFrame(uint16_t type, const void* payload, size_t payload_len);

  Service& GetService();
  const Service& GetService() const;

 private:
  class EventDispatcher : public Service::EventHandler
  {
   public:
    explicit EventDispatcher(Module& owner) : owner_(owner) {}

    void OnEvent(const ServiceEvent& event) override;

   private:
    Module& owner_;
  };

  void DispatchEvent(const ServiceEvent& event);

  EventDispatcher dispatcher_;
  EventListener* listeners_[Detail::MODULE_MAX_LISTENERS] = {};
  Service service_;
};

inline Driver::Driver(const Config& config) : config_(config)
{
  initialized_ = (config_.uart != nullptr);
  ResetParserState();
}

inline bool Driver::IsInitialized() const
{
  return initialized_;
}

inline void Driver::ResetParser()
{
  ResetParserState();
}

inline uint8_t Driver::ChecksumAdd(uint8_t cksum, uint8_t byte)
{
  return static_cast<uint8_t>(cksum ^ byte);
}

inline uint8_t Driver::ChecksumFinalize(uint8_t cksum)
{
  return static_cast<uint8_t>(~cksum);
}

inline void Driver::WriteU16BE(uint8_t* dst, uint16_t value)
{
  dst[0] = static_cast<uint8_t>((value >> 8) & 0xFFU);
  dst[1] = static_cast<uint8_t>(value & 0xFFU);
}

inline void Driver::ResetParserState()
{
  state_ = ParserState::WAIT_SOF;
  current_id_ = 0;
  current_len_ = 0;
  current_type_ = 0;
  field_value_ = 0;
  field_index_ = 0;
  payload_index_ = 0;
  header_cksum_ = 0;
  body_cksum_ = 0;
  discard_payload_ = false;
  last_byte_us_ = LibXR::MicrosecondTimestamp();
}

inline bool Driver::StoreFrame(uint16_t id, uint16_t type, uint16_t len, const uint8_t* payload)
{
  if (len > MAX_PAYLOAD_LEN)
  {
    return false;
  }

  last_frame_.id = id;
  last_frame_.type = type;
  last_frame_.len = len;

  if (len > 0U)
  {
    std::memcpy(last_frame_.data, payload, len);
  }

  has_last_frame_ = true;
  return true;
}

inline bool Driver::GetLastFrame(Frame& out_frame) const
{
  if (!has_last_frame_)
  {
    return false;
  }

  out_frame = last_frame_;
  return true;
}

inline bool Driver::IsVersionQuery(const Frame& frame)
{
  return (frame.type == VERSION_QUERY_TYPE) &&
         (frame.len == sizeof(Detail::kVersionQueryPayload)) &&
         (std::memcmp(frame.data, Detail::kVersionQueryPayload,
                      sizeof(Detail::kVersionQueryPayload)) == 0);
}

inline bool Driver::ParseVersionResponse(const Frame& frame, Version& out_version)
{
  if ((frame.type != VERSION_QUERY_TYPE) || (frame.len != sizeof(Version)) ||
      IsVersionQuery(frame))
  {
    return false;
  }

  out_version.project = frame.data[0];
  out_version.version_major = frame.data[1];
  out_version.version_minor = frame.data[2];
  out_version.version_patch = frame.data[3];
  return true;
}

inline bool Driver::ParseU32LE(const Frame& frame, uint32_t& out_value)
{
  if (frame.len != 4U)
  {
    return false;
  }

  out_value = static_cast<uint32_t>(frame.data[0]) |
              (static_cast<uint32_t>(frame.data[1]) << 8U) |
              (static_cast<uint32_t>(frame.data[2]) << 16U) |
              (static_cast<uint32_t>(frame.data[3]) << 24U);
  return true;
}

inline bool Driver::ParseFloatLE(const Frame& frame, float& out_value)
{
  uint32_t raw = 0;
  if (!ParseU32LE(frame, raw))
  {
    return false;
  }

  static_assert(sizeof(raw) == sizeof(out_value));
  std::memcpy(&out_value, &raw, sizeof(raw));
  return true;
}

inline void Driver::HandleFrame()
{
  if (!StoreFrame(current_id_, current_type_, current_len_, payload_frame_.data))
  {
    return;
  }

  const Frame frame = last_frame_;

  if (config_.auto_reply_version && IsVersionQuery(frame))
  {
    (void)SendFrame(VERSION_QUERY_TYPE, &config_.local_version, sizeof(config_.local_version));
  }

  if (config_.frame_handler != nullptr)
  {
    config_.frame_handler->OnFrame(frame);
  }
}

inline void Driver::FeedByte(uint8_t byte)
{
  const auto now_us = LibXR::Timebase::GetMicroseconds();
  if ((state_ != ParserState::WAIT_SOF) &&
      (static_cast<uint64_t>(last_byte_us_) != 0U) &&
      ((now_us - last_byte_us_).ToMillisecond() > config_.parser_timeout_ms))
  {
    ResetParserState();
  }
  last_byte_us_ = now_us;

  switch (state_)
  {
    case ParserState::WAIT_SOF:
      if (byte == SOF)
      {
        state_ = ParserState::READ_ID;
        field_value_ = 0;
        field_index_ = 0;
        payload_index_ = 0;
        header_cksum_ = ChecksumAdd(0, byte);
        body_cksum_ = 0;
        discard_payload_ = false;
      }
      break;

    case ParserState::READ_ID:
      header_cksum_ = ChecksumAdd(header_cksum_, byte);
      field_value_ = static_cast<uint16_t>((field_value_ << 8U) | byte);
      field_index_++;
      if (field_index_ == 2U)
      {
        current_id_ = field_value_;
        field_value_ = 0;
        field_index_ = 0;
        state_ = ParserState::READ_LEN;
      }
      break;

    case ParserState::READ_LEN:
      header_cksum_ = ChecksumAdd(header_cksum_, byte);
      field_value_ = static_cast<uint16_t>((field_value_ << 8U) | byte);
      field_index_++;
      if (field_index_ == 2U)
      {
        current_len_ = field_value_;
        field_value_ = 0;
        field_index_ = 0;
        state_ = ParserState::READ_TYPE;
      }
      break;

    case ParserState::READ_TYPE:
      header_cksum_ = ChecksumAdd(header_cksum_, byte);
      field_value_ = static_cast<uint16_t>((field_value_ << 8U) | byte);
      field_index_++;
      if (field_index_ == 2U)
      {
        current_type_ = field_value_;
        field_value_ = 0;
        field_index_ = 0;
        state_ = ParserState::READ_HEAD_CKSUM;
      }
      break;

    case ParserState::READ_HEAD_CKSUM:
      if (ChecksumFinalize(header_cksum_) != byte)
      {
        ResetParserState();
        return;
      }

      if (current_len_ > MAX_PAYLOAD_LEN)
      {
        discard_payload_ = true;
      }

      if (current_len_ == 0U)
      {
        if (!discard_payload_)
        {
          HandleFrame();
        }
        ResetParserState();
        return;
      }

      state_ = ParserState::READ_PAYLOAD;
      break;

    case ParserState::READ_PAYLOAD:
      body_cksum_ = ChecksumAdd(body_cksum_, byte);
      if (!discard_payload_)
      {
        payload_frame_.data[payload_index_] = byte;
      }
      payload_index_++;
      if (payload_index_ >= current_len_)
      {
        state_ = ParserState::READ_BODY_CKSUM;
      }
      break;

    case ParserState::READ_BODY_CKSUM:
      if (ChecksumFinalize(body_cksum_) == byte)
      {
        if (!discard_payload_)
        {
          HandleFrame();
        }
      }
      ResetParserState();
      return;
  }
}

inline LibXR::ErrorCode Driver::SendFrame(uint16_t type, const void* payload, size_t payload_len)
{
  if (!initialized_)
  {
    return LibXR::ErrorCode::STATE_ERR;
  }
  if ((payload_len > 0U) && (payload == nullptr))
  {
    return LibXR::ErrorCode::ARG_ERR;
  }
  if (payload_len > MAX_PAYLOAD_LEN)
  {
    return LibXR::ErrorCode::SIZE_ERR;
  }

  uint8_t frame[MAX_PAYLOAD_LEN + Detail::kFrameOverhead] = {0};
  size_t frame_len = 0;
  uint8_t head_cksum = 0;
  uint8_t body_cksum = 0;
  const auto* payload_bytes = static_cast<const uint8_t*>(payload);
  const uint16_t frame_id = next_id_++;

  frame[frame_len++] = SOF;
  head_cksum = ChecksumAdd(head_cksum, SOF);

  WriteU16BE(&frame[frame_len], frame_id);
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len]);
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len + 1U]);
  frame_len += 2U;

  WriteU16BE(&frame[frame_len], static_cast<uint16_t>(payload_len));
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len]);
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len + 1U]);
  frame_len += 2U;

  WriteU16BE(&frame[frame_len], type);
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len]);
  head_cksum = ChecksumAdd(head_cksum, frame[frame_len + 1U]);
  frame_len += 2U;

  frame[frame_len++] = ChecksumFinalize(head_cksum);

  for (size_t i = 0; i < payload_len; ++i)
  {
    frame[frame_len++] = payload_bytes[i];
    body_cksum = ChecksumAdd(body_cksum, payload_bytes[i]);
  }

  if (payload_len > 0U)
  {
    frame[frame_len++] = ChecksumFinalize(body_cksum);
  }

  LibXR::Semaphore sem(0);
  LibXR::WriteOperation op(sem, 20);
  return config_.uart->Write(LibXR::ConstRawData(frame, frame_len), op);
}

inline LibXR::ErrorCode Driver::RequestVersion()
{
  return SendFrame(VERSION_QUERY_TYPE, Detail::kVersionQueryPayload,
                   sizeof(Detail::kVersionQueryPayload));
}

inline void Driver::Poll()
{
  if (!initialized_)
  {
    return;
  }

  uint8_t rx_buffer[Detail::kPollChunkSize] = {0};
  while (true)
  {
    const size_t read_size = Detail::ReadAvailableBytes(*config_.uart, rx_buffer, sizeof(rx_buffer));
    if (read_size == 0U)
    {
      return;
    }

    for (size_t i = 0; i < read_size; ++i)
    {
      FeedByte(rx_buffer[i]);
    }
  }
}

inline Service::Service(const Config& config)
    : driver_([&]() {
        auto driver_config = config.driver_config;
        driver_config.frame_handler = this;
        return driver_config;
      }()),
      event_handler_(config.event_handler)
{
  if (config.request_version_on_init && driver_.IsInitialized())
  {
    (void)driver_.RequestVersion();
  }
}

inline bool Service::IsInitialized() const
{
  return driver_.IsInitialized();
}

inline void Service::Poll()
{
  driver_.Poll();
}

inline void Service::ResetParser()
{
  driver_.ResetParser();
}

inline LibXR::ErrorCode Service::RequestVersion()
{
  return driver_.RequestVersion();
}

inline LibXR::ErrorCode Service::SendFrame(uint16_t type, const void* payload, size_t payload_len)
{
  return driver_.SendFrame(type, payload, payload_len);
}

inline bool Service::GetSnapshot(ServiceSnapshot& out_snapshot) const
{
  if (!driver_.IsInitialized())
  {
    return false;
  }

  out_snapshot = snapshot_;
  return true;
}

inline bool Service::GetLastEvent(ServiceEvent& out_event) const
{
  if (!has_last_event_)
  {
    return false;
  }

  out_event = last_event_;
  return true;
}

inline size_t Service::PayloadLen(const Frame& frame)
{
  return (frame.len <= MAX_PAYLOAD_LEN) ? frame.len : 0U;
}

inline uint16_t Service::ReadU16LE(const uint8_t* data)
{
  return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                               (static_cast<uint16_t>(data[1]) << 8U));
}

inline uint32_t Service::ReadU32LE(const uint8_t* data)
{
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8U) |
         (static_cast<uint32_t>(data[2]) << 16U) |
         (static_cast<uint32_t>(data[3]) << 24U);
}

inline int32_t Service::ReadI32LE(const uint8_t* data)
{
  return static_cast<int32_t>(ReadU32LE(data));
}

inline float Service::ReadF32LE(const uint8_t* data)
{
  const uint32_t raw = ReadU32LE(data);
  float value = 0.0f;
  std::memcpy(&value, &raw, sizeof(value));
  return value;
}

inline void Service::BuildText(const Frame& frame, char* out_text, size_t out_size)
{
  if ((out_text == nullptr) || (out_size == 0U))
  {
    return;
  }

  size_t copy_len = PayloadLen(frame);
  if (copy_len >= out_size)
  {
    copy_len = out_size - 1U;
  }

  std::memcpy(out_text, frame.data, copy_len);
  out_text[copy_len] = '\0';

  while (copy_len > 0U)
  {
    const char ch = out_text[copy_len - 1U];
    if ((ch != ' ') && (ch != '\r') && (ch != '\n') && (ch != '\t'))
    {
      break;
    }
    out_text[copy_len - 1U] = '\0';
    copy_len--;
  }
}

inline ServiceEventKind Service::KindFromFrame(const Frame& frame)
{
  switch (frame.type)
  {
    case TYPE_FIRMWARE_STATUS:
      return Driver::IsVersionQuery(frame) ? ServiceEventKind::FIRMWARE_STATUS_QUERY
                                           : ServiceEventKind::FIRMWARE_STATUS;
    case TYPE_OTA:
      return ServiceEventKind::OTA;
    case TYPE_TEXT_MESSAGE:
      return ServiceEventKind::TEXT_MESSAGE;
    case TYPE_HUMAN_PRESENCE:
      return ServiceEventKind::HUMAN_PRESENCE;
    case TYPE_BREATH_RATE:
      return ServiceEventKind::BREATH_RATE;
    case TYPE_HEART_RATE:
      return ServiceEventKind::HEART_RATE;
    case TYPE_TARGET_RANGE:
      return ServiceEventKind::TARGET_RANGE;
    case TYPE_TRACK_POSITION:
      return ServiceEventKind::TRACK_POSITION;
    case TYPE_PHASE_TEST:
      return ServiceEventKind::PHASE_TEST;
    case TYPE_PERSONNEL_POSITION:
      return ServiceEventKind::PERSONNEL_POSITION;
    default:
      return ServiceEventKind::UNKNOWN;
  }
}

inline void Service::UpdateSnapshot(const ServiceEvent& event)
{
  if (event.result != ServiceParseResult::OK)
  {
    return;
  }

  switch (event.kind)
  {
    case ServiceEventKind::FIRMWARE_STATUS:
      snapshot_.has_firmware_status = true;
      snapshot_.firmware_status = event.data.firmware_status;
      break;
    case ServiceEventKind::OTA:
      snapshot_.has_ota_message = true;
      snapshot_.ota_payload_len = event.data.ota.payload_len;
      break;
    case ServiceEventKind::TEXT_MESSAGE:
      snapshot_.has_text_message = true;
      snapshot_.text_message = event.data.text;
      break;
    case ServiceEventKind::HUMAN_PRESENCE:
      snapshot_.has_presence = true;
      snapshot_.presence = event.data.presence;
      break;
    case ServiceEventKind::BREATH_RATE:
      snapshot_.has_breath_rate = true;
      snapshot_.breath_rate = event.data.breath_rate;
      break;
    case ServiceEventKind::HEART_RATE:
      snapshot_.has_heart_rate = true;
      snapshot_.heart_rate = event.data.heart_rate;
      break;
    case ServiceEventKind::TARGET_RANGE:
      snapshot_.has_target_range = true;
      snapshot_.target_range = event.data.target_range;
      break;
    case ServiceEventKind::TRACK_POSITION:
      snapshot_.has_track_position = true;
      snapshot_.track_position = event.data.track_position;
      break;
    case ServiceEventKind::PHASE_TEST:
      snapshot_.has_phase_test = true;
      snapshot_.phase_test = event.data.phase_test;
      break;
    case ServiceEventKind::PERSONNEL_POSITION:
      snapshot_.has_personnel_position = true;
      snapshot_.personnel_position = event.data.personnel_position;
      break;
    case ServiceEventKind::FIRMWARE_STATUS_QUERY:
    case ServiceEventKind::UNKNOWN:
      break;
  }
}

inline void Service::OnFrame(const Frame& frame)
{
  ServiceEvent event = {};
  DecodeFrame(frame, event);
  last_event_ = event;
  has_last_event_ = true;
  UpdateSnapshot(event);

  if (event_handler_ != nullptr)
  {
    event_handler_->OnEvent(event);
  }
}

inline ServiceParseResult Service::DecodeFrame(const Frame& frame, ServiceEvent& out_event)
{
  out_event = ServiceEvent{};
  out_event.kind = KindFromFrame(frame);
  out_event.frame = frame;

  if (frame.len > MAX_PAYLOAD_LEN)
  {
    out_event.result = ServiceParseResult::MALFORMED;
    out_event.detail = "payload length exceeds buffer";
    return out_event.result;
  }

  switch (out_event.kind)
  {
    case ServiceEventKind::FIRMWARE_STATUS_QUERY:
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::FIRMWARE_STATUS:
      if (!Driver::ParseVersionResponse(frame, out_event.data.firmware_status))
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "invalid firmware-status payload";
        return out_event.result;
      }
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::OTA:
      out_event.data.ota.payload_len = frame.len;
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::TEXT_MESSAGE:
      BuildText(frame, out_event.data.text.text, sizeof(out_event.data.text.text));
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::HUMAN_PRESENCE:
      if (frame.len != 2U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 2-byte presence payload";
        return out_event.result;
      }
      out_event.data.presence.raw_value = ReadU16LE(frame.data);
      if (out_event.data.presence.raw_value == 0U)
      {
        out_event.data.presence.state = ServicePresenceState::NONE;
      }
      else if (out_event.data.presence.raw_value == 1U)
      {
        out_event.data.presence.state = ServicePresenceState::HUMAN;
      }
      else
      {
        out_event.data.presence.state = ServicePresenceState::UNKNOWN_VALUE;
      }
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::BREATH_RATE:
      if (frame.len != 4U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 4-byte breath-rate payload";
        return out_event.result;
      }
      out_event.data.breath_rate.bpm = ReadF32LE(frame.data);
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::HEART_RATE:
      if (frame.len != 4U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 4-byte heart-rate payload";
        return out_event.result;
      }
      out_event.data.heart_rate.bpm = ReadF32LE(frame.data);
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::TARGET_RANGE:
      if (frame.len != 8U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 8-byte target-range payload";
        return out_event.result;
      }
      out_event.data.target_range.flag = ReadU32LE(frame.data);
      out_event.data.target_range.range_cm = ReadF32LE(&frame.data[4]);
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::TRACK_POSITION:
      if ((frame.len != 8U) && (frame.len != 12U))
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 8-byte or 12-byte track-position payload";
        return out_event.result;
      }
      out_event.data.track_position.x = ReadF32LE(&frame.data[0]);
      out_event.data.track_position.y = ReadF32LE(&frame.data[4]);
      out_event.data.track_position.has_z = (frame.len == 12U);
      if (out_event.data.track_position.has_z)
      {
        out_event.data.track_position.z = ReadF32LE(&frame.data[8]);
      }
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::PHASE_TEST:
      if (frame.len != 12U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected 12-byte phase-test payload";
        return out_event.result;
      }
      out_event.data.phase_test.raw_prefix = ReadU32LE(&frame.data[0]);
      out_event.data.phase_test.breath_phase = ReadF32LE(&frame.data[4]);
      out_event.data.phase_test.heart_phase = ReadF32LE(&frame.data[8]);
      out_event.result = ServiceParseResult::OK;
      return out_event.result;

    case ServiceEventKind::PERSONNEL_POSITION:
    {
      if (frame.len < 4U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "expected at least 4 bytes for target count";
        return out_event.result;
      }

      const int32_t target_count = ReadI32LE(&frame.data[0]);
      if (target_count < 0)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "negative target count";
        return out_event.result;
      }
      if (target_count > static_cast<int32_t>(SERVICE_MAX_PERSON_TARGETS))
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "target count exceeds service buffer";
        return out_event.result;
      }

      size_t expected_len = 4U + (static_cast<size_t>(target_count) * 16U);
      uint8_t bytes_per_target = 0U;
      if (frame.len == expected_len)
      {
        bytes_per_target = 16U;
      }
      else
      {
        expected_len = 4U + (static_cast<size_t>(target_count) * 20U);
        if (frame.len == expected_len)
        {
          bytes_per_target = 20U;
        }
      }

      if (bytes_per_target == 0U)
      {
        out_event.result = ServiceParseResult::MALFORMED;
        out_event.detail = "payload length does not match target count";
        return out_event.result;
      }

      out_event.data.personnel_position.target_count = target_count;
      out_event.data.personnel_position.bytes_per_target = bytes_per_target;

      size_t offset = 4U;
      for (int32_t i = 0; i < target_count; ++i)
      {
        auto& target = out_event.data.personnel_position.targets[i];
        target.x = ReadF32LE(&frame.data[offset]);
        target.y = ReadF32LE(&frame.data[offset + 4U]);
        target.has_z = (bytes_per_target == 20U);
        if (target.has_z)
        {
          target.z = ReadF32LE(&frame.data[offset + 8U]);
          target.dop_idx = ReadI32LE(&frame.data[offset + 12U]);
          target.cluster_id = ReadI32LE(&frame.data[offset + 16U]);
        }
        else
        {
          target.dop_idx = ReadI32LE(&frame.data[offset + 8U]);
          target.cluster_id = ReadI32LE(&frame.data[offset + 12U]);
        }
        offset += bytes_per_target;
      }

      out_event.result = ServiceParseResult::OK;
      return out_event.result;
    }

    case ServiceEventKind::UNKNOWN:
    default:
      out_event.result = ServiceParseResult::UNSUPPORTED;
      out_event.detail = "no semantic decoder";
      return out_event.result;
  }
}

inline const char* Service::EventKindToString(ServiceEventKind kind)
{
  switch (kind)
  {
    case ServiceEventKind::FIRMWARE_STATUS_QUERY:
      return "firmware_status_query";
    case ServiceEventKind::FIRMWARE_STATUS:
      return "firmware_status";
    case ServiceEventKind::OTA:
      return "ota";
    case ServiceEventKind::TEXT_MESSAGE:
      return "text_message";
    case ServiceEventKind::HUMAN_PRESENCE:
      return "human_presence";
    case ServiceEventKind::BREATH_RATE:
      return "breath_rate";
    case ServiceEventKind::HEART_RATE:
      return "heart_rate";
    case ServiceEventKind::TARGET_RANGE:
      return "target_range";
    case ServiceEventKind::TRACK_POSITION:
      return "track_position";
    case ServiceEventKind::PHASE_TEST:
      return "phase_test";
    case ServiceEventKind::PERSONNEL_POSITION:
      return "personnel_position";
    case ServiceEventKind::UNKNOWN:
    default:
      return "unknown";
  }
}

inline const char* Service::ParseResultToString(ServiceParseResult result)
{
  switch (result)
  {
    case ServiceParseResult::OK:
      return "ok";
    case ServiceParseResult::UNSUPPORTED:
      return "unsupported";
    case ServiceParseResult::MALFORMED:
      return "malformed";
    default:
      return "invalid";
  }
}

inline void Module::EventDispatcher::OnEvent(const ServiceEvent& event)
{
  owner_.DispatchEvent(event);
}

inline Module::Module(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& appmgr)
    : dispatcher_(*this),
      service_([&]() {
        auto* uart = hw.FindOrExit<LibXR::UART>({kRequiredUartAlias});
        Service::Config config = {};
        config.driver_config.uart = uart;
        config.driver_config.uart_read_timeout_ms = 0;
        config.driver_config.parser_timeout_ms = 50;
        config.driver_config.auto_reply_version = false;
        config.request_version_on_init = true;
        config.event_handler = &dispatcher_;
        return config;
      }())
{
  appmgr.Register(*this);
}

inline bool Module::RegisterListener(EventListener& listener)
{
  for (size_t i = 0; i < Detail::MODULE_MAX_LISTENERS; ++i)
  {
    if (listeners_[i] == &listener)
    {
      return true;
    }
  }

  for (size_t i = 0; i < Detail::MODULE_MAX_LISTENERS; ++i)
  {
    if (listeners_[i] == nullptr)
    {
      listeners_[i] = &listener;
      return true;
    }
  }

  return false;
}

inline bool Module::UnregisterListener(EventListener& listener)
{
  for (size_t i = 0; i < Detail::MODULE_MAX_LISTENERS; ++i)
  {
    if (listeners_[i] == &listener)
    {
      listeners_[i] = nullptr;
      return true;
    }
  }

  return false;
}

inline void Module::DispatchEvent(const ServiceEvent& event)
{
  for (size_t i = 0; i < Detail::MODULE_MAX_LISTENERS; ++i)
  {
    if (listeners_[i] != nullptr)
    {
      listeners_[i]->OnLD6002Event(event);
    }
  }
}

inline bool Module::IsInitialized() const
{
  return service_.IsInitialized();
}

inline void Module::OnMonitor()
{
  service_.Poll();
}

inline bool Module::GetSnapshot(ServiceSnapshot& out_snapshot) const
{
  return service_.GetSnapshot(out_snapshot);
}

inline bool Module::GetLastEvent(ServiceEvent& out_event) const
{
  return service_.GetLastEvent(out_event);
}

inline LibXR::ErrorCode Module::RequestVersion()
{
  return service_.RequestVersion();
}

inline LibXR::ErrorCode Module::SendFrame(uint16_t type, const void* payload, size_t payload_len)
{
  return service_.SendFrame(type, payload, payload_len);
}

inline Service& Module::GetService()
{
  return service_;
}

inline const Service& Module::GetService() const
{
  return service_;
}

}  // namespace LD6002
