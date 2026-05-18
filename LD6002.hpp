#pragma once
// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: LD6002 radar driver
constructor_args: []
template_args: []
required_hardware:
  ld6002_uart
depends: []
=== END MANIFEST === */
// clang-format on

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_rw.hpp"
#include "semaphore.hpp"
#include "timebase.hpp"
#include "uart.hpp"

class LD6002 : public LibXR::Application
{
 public:
  static constexpr const char* kRequiredUartAlias = "ld6002_uart";

  static constexpr uint8_t SOF = 0x01U;
  static constexpr size_t MAX_PAYLOAD_LEN = 128U;
  static constexpr size_t MAX_PERSON_TARGETS = 7U;
  static constexpr uint16_t VERSION_QUERY_TYPE = 0xFFFFU;

  static constexpr uint16_t TYPE_FIRMWARE_STATUS = VERSION_QUERY_TYPE;
  static constexpr uint16_t TYPE_OTA = 0x3000U;
  static constexpr uint16_t TYPE_TEXT_MESSAGE = 0x0100U;
  static constexpr uint16_t TYPE_HUMAN_PRESENCE = 0x0F09U;
  static constexpr uint16_t TYPE_PERSONNEL_POSITION = 0x0A04U;
  static constexpr uint16_t TYPE_PHASE_TEST = 0x0A13U;
  static constexpr uint16_t TYPE_BREATH_RATE = 0x0A14U;
  static constexpr uint16_t TYPE_HEART_RATE = 0x0A15U;
  static constexpr uint16_t TYPE_TARGET_RANGE = 0x0A16U;
  static constexpr uint16_t TYPE_TRACK_POSITION = 0x0A17U;
  static constexpr uint32_t kDefaultOnlineTimeoutMs = 1000U;

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

  enum class ParseResult : uint8_t
  {
    OK = 0,
    UNSUPPORTED,
    MALFORMED,
  };

  enum class EventKind : uint8_t
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

  enum class PresenceState : uint8_t
  {
    NONE = 0,
    HUMAN = 1,
    UNKNOWN_VALUE = 2,
  };

  struct Presence
  {
    uint16_t raw_value = 0;
    PresenceState state = PresenceState::NONE;
  };

  struct TargetRange
  {
    uint32_t flag = 0;
    float range_cm = 0.0f;
  };

  struct TrackPosition
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool has_z = false;
  };

  struct PhaseTest
  {
    uint32_t raw_prefix = 0;
    float breath_phase = 0.0f;
    float heart_phase = 0.0f;
  };

  struct PersonTarget
  {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool has_z = false;
    int32_t dop_idx = 0;
    int32_t cluster_id = 0;
  };

  struct PersonnelPosition
  {
    int32_t target_count = 0;
    uint8_t bytes_per_target = 0;
    PersonTarget targets[MAX_PERSON_TARGETS] = {};
  };

  struct Event
  {
    EventKind kind = EventKind::UNKNOWN;
    ParseResult result = ParseResult::MALFORMED;
    const char* detail = nullptr;
    Frame frame = {};
    union
    {
      Version firmware_status;
      size_t ota_payload_len;
      char text[MAX_PAYLOAD_LEN + 1U];
      Presence presence;
      float breath_rate_bpm;
      float heart_rate_bpm;
      TargetRange target_range;
      TrackPosition track_position;
      PhaseTest phase_test;
      PersonnelPosition personnel_position;
    } data = {};
  };

  struct Snapshot
  {
    bool has_firmware_status = false;
    Version firmware_status = {};
    bool has_ota_message = false;
    size_t ota_payload_len = 0;
    bool has_text_message = false;
    char text_message[MAX_PAYLOAD_LEN + 1U] = {0};
    bool has_presence = false;
    Presence presence = {};
    bool has_breath_rate = false;
    float breath_rate_bpm = 0.0f;
    bool has_heart_rate = false;
    float heart_rate_bpm = 0.0f;
    bool has_target_range = false;
    TargetRange target_range = {};
    bool has_track_position = false;
    TrackPosition track_position = {};
    bool has_phase_test = false;
    PhaseTest phase_test = {};
    bool has_personnel_position = false;
    PersonnelPosition personnel_position = {};
  };

  struct Config
  {
    LibXR::UART* uart = nullptr;
    uint32_t uart_read_timeout_ms = 0;
    uint32_t parser_timeout_ms = 50;
    bool auto_reply_version = false;
    bool request_version_on_init = true;
    Version local_version = {0, 1, 0, 0};
  };

  struct Status
  {
    bool has_uart = false;
    bool has_last_frame = false;
    bool has_last_event = false;
    bool has_pending_event = false;
    bool online = false;
    size_t pending_event_count = 0;
    LibXR::MicrosecondTimestamp last_rx_timestamp = {};
    LibXR::MicrosecondTimestamp last_frame_timestamp = {};
  };

  explicit LD6002(const Config& config) : config_(config)
  {
    ResetRuntimeState();
    ResetParserState();
  }

  explicit LD6002(LibXR::UART& uart) : LD6002([&]() {
    Config config = {};
    config.uart = &uart;
    return config;
  }())
  {
  }

  explicit LD6002(LibXR::HardwareContainer& hw) : LD6002(*hw.FindOrExit<LibXR::UART>({
    kRequiredUartAlias
  }))
  {
  }

  explicit LD6002(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& appmgr) : LD6002(hw)
  {
    (void)Init();
    appmgr.Register(*this);
  }

  LibXR::ErrorCode Init()
  {
    ResetState();
    if (!HasUart())
    {
      return LibXR::ErrorCode::STATE_ERR;
    }

    if (!config_.request_version_on_init)
    {
      return LibXR::ErrorCode::OK;
    }

    return RequestVersion();
  }

  bool HasUart() const
  {
    return config_.uart != nullptr;
  }

  const Config& GetConfig() const
  {
    return config_;
  }

  Status GetStatus(uint32_t online_timeout_ms = kDefaultOnlineTimeoutMs) const
  {
    Status status = {};
    status.has_uart = HasUart();
    status.has_last_frame = has_last_frame_;
    status.has_last_event = has_last_event_;
    status.has_pending_event = (event_queue_size_ != 0U);
    status.online = IsOnline(online_timeout_ms);
    status.pending_event_count = event_queue_size_;
    status.last_rx_timestamp = last_rx_us_;
    status.last_frame_timestamp = last_frame_us_;
    return status;
  }

  bool IsOnline(uint32_t stale_timeout_ms = kDefaultOnlineTimeoutMs) const
  {
    if (!HasUart() || !has_last_frame_)
    {
      return false;
    }

    if ((stale_timeout_ms == 0U) || (LibXR::Timebase::timebase == nullptr))
    {
      return true;
    }

    return ((LibXR::Timebase::GetMicroseconds() - last_frame_us_).ToMillisecond() <=
            stale_timeout_ms);
  }

  void Poll()
  {
    if (config_.uart == nullptr)
    {
      return;
    }

    uint8_t rx_buffer[kPollChunkSize] = {0};
    while (true)
    {
      const size_t read_size =
          ReadAvailableBytes(*config_.uart, rx_buffer, sizeof(rx_buffer), config_.uart_read_timeout_ms);
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

  void OnMonitor() override
  {
    Poll();
  }

  void ResetState()
  {
    ResetRuntimeState();
    ResetParserState();
  }

  void ResetParser()
  {
    ResetParserState();
  }

  LibXR::ErrorCode SendFrame(uint16_t type, const void* payload, size_t payload_len)
  {
    if (config_.uart == nullptr)
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

    uint8_t frame[MAX_PAYLOAD_LEN + kFrameOverhead] = {0};
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

  LibXR::ErrorCode RequestVersion()
  {
    return SendFrame(VERSION_QUERY_TYPE, kVersionQueryPayload, sizeof(kVersionQueryPayload));
  }

  bool GetSnapshot(Snapshot& out_snapshot) const
  {
    if (config_.uart == nullptr)
    {
      return false;
    }

    out_snapshot = snapshot_;
    return true;
  }

  bool GetLastFrame(Frame& out_frame) const
  {
    if (!has_last_frame_)
    {
      return false;
    }

    out_frame = last_frame_;
    return true;
  }

  bool GetLastEvent(Event& out_event) const
  {
    if (!has_last_event_)
    {
      return false;
    }

    out_event = last_event_;
    return true;
  }

  bool PopEvent(Event& out_event)
  {
    if (event_queue_size_ == 0U)
    {
      return false;
    }

    out_event = event_queue_[event_queue_tail_];
    event_queue_tail_ = (event_queue_tail_ + 1U) % kEventQueueCapacity;
    event_queue_size_--;
    return true;
  }

  static bool IsVersionQuery(const Frame& frame)
  {
    return (frame.type == VERSION_QUERY_TYPE) &&
           (frame.len == sizeof(kVersionQueryPayload)) &&
           (std::memcmp(frame.data, kVersionQueryPayload, sizeof(kVersionQueryPayload)) == 0);
  }

  static bool ParseVersionResponse(const Frame& frame, Version& out_version)
  {
    if ((frame.type != VERSION_QUERY_TYPE) || (frame.len != sizeof(Version)) || IsVersionQuery(frame))
    {
      return false;
    }

    out_version.project = frame.data[0];
    out_version.version_major = frame.data[1];
    out_version.version_minor = frame.data[2];
    out_version.version_patch = frame.data[3];
    return true;
  }

  static bool ParseU32LE(const Frame& frame, uint32_t& out_value)
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

  static bool ParseFloatLE(const Frame& frame, float& out_value)
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

  static ParseResult DecodeFrame(const Frame& frame, Event& out_event)
  {
    out_event = Event{};
    out_event.kind = KindFromFrame(frame);
    out_event.frame = frame;

    if (frame.len > MAX_PAYLOAD_LEN)
    {
      out_event.result = ParseResult::MALFORMED;
      out_event.detail = "payload length exceeds buffer";
      return out_event.result;
    }

    switch (out_event.kind)
    {
      case EventKind::FIRMWARE_STATUS_QUERY:
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::FIRMWARE_STATUS:
        if (!ParseVersionResponse(frame, out_event.data.firmware_status))
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "invalid firmware-status payload";
          return out_event.result;
        }
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::OTA:
        out_event.data.ota_payload_len = frame.len;
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::TEXT_MESSAGE:
        BuildText(frame, out_event.data.text, sizeof(out_event.data.text));
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::HUMAN_PRESENCE:
        if (frame.len != 2U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected 2-byte presence payload";
          return out_event.result;
        }
        out_event.data.presence.raw_value = ReadU16LE(frame.data);
        if (out_event.data.presence.raw_value == 0U)
        {
          out_event.data.presence.state = PresenceState::NONE;
        }
        else if (out_event.data.presence.raw_value == 1U)
        {
          out_event.data.presence.state = PresenceState::HUMAN;
        }
        else
        {
          out_event.data.presence.state = PresenceState::UNKNOWN_VALUE;
        }
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::BREATH_RATE:
        if (frame.len != 4U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected 4-byte breath-rate payload";
          return out_event.result;
        }
        out_event.data.breath_rate_bpm = ReadF32LE(frame.data);
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::HEART_RATE:
        if (frame.len != 4U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected 4-byte heart-rate payload";
          return out_event.result;
        }
        out_event.data.heart_rate_bpm = ReadF32LE(frame.data);
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::TARGET_RANGE:
        if (frame.len != 8U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected 8-byte target-range payload";
          return out_event.result;
        }
        out_event.data.target_range.flag = ReadU32LE(frame.data);
        out_event.data.target_range.range_cm = ReadF32LE(&frame.data[4]);
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::TRACK_POSITION:
        if ((frame.len != 8U) && (frame.len != 12U))
        {
          out_event.result = ParseResult::MALFORMED;
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
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::PHASE_TEST:
        if (frame.len != 12U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected 12-byte phase-test payload";
          return out_event.result;
        }
        out_event.data.phase_test.raw_prefix = ReadU32LE(&frame.data[0]);
        out_event.data.phase_test.breath_phase = ReadF32LE(&frame.data[4]);
        out_event.data.phase_test.heart_phase = ReadF32LE(&frame.data[8]);
        out_event.result = ParseResult::OK;
        return out_event.result;

      case EventKind::PERSONNEL_POSITION:
      {
        if (frame.len < 4U)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "expected at least 4 bytes for target count";
          return out_event.result;
        }

        const int32_t target_count = ReadI32LE(&frame.data[0]);
        if (target_count < 0)
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "negative target count";
          return out_event.result;
        }
        if (target_count > static_cast<int32_t>(MAX_PERSON_TARGETS))
        {
          out_event.result = ParseResult::MALFORMED;
          out_event.detail = "target count exceeds buffer";
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
          out_event.result = ParseResult::MALFORMED;
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

        out_event.result = ParseResult::OK;
        return out_event.result;
      }

      case EventKind::UNKNOWN:
      default:
        out_event.result = ParseResult::UNSUPPORTED;
        out_event.detail = "no semantic decoder";
        return out_event.result;
    }
  }

  static const char* EventKindToString(EventKind kind)
  {
    switch (kind)
    {
      case EventKind::FIRMWARE_STATUS_QUERY:
        return "firmware_status_query";
      case EventKind::FIRMWARE_STATUS:
        return "firmware_status";
      case EventKind::OTA:
        return "ota";
      case EventKind::TEXT_MESSAGE:
        return "text_message";
      case EventKind::HUMAN_PRESENCE:
        return "human_presence";
      case EventKind::BREATH_RATE:
        return "breath_rate";
      case EventKind::HEART_RATE:
        return "heart_rate";
      case EventKind::TARGET_RANGE:
        return "target_range";
      case EventKind::TRACK_POSITION:
        return "track_position";
      case EventKind::PHASE_TEST:
        return "phase_test";
      case EventKind::PERSONNEL_POSITION:
        return "personnel_position";
      case EventKind::UNKNOWN:
      default:
        return "unknown";
    }
  }

  static const char* ParseResultToString(ParseResult result)
  {
    switch (result)
    {
      case ParseResult::OK:
        return "ok";
      case ParseResult::UNSUPPORTED:
        return "unsupported";
      case ParseResult::MALFORMED:
        return "malformed";
      default:
        return "invalid";
    }
  }

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

  static constexpr uint8_t kVersionQueryPayload[4] = {0x01, 0x01, 0x00, 0x00};
  static constexpr size_t kFrameOverhead = 9U;
  static constexpr size_t kPollChunkSize = 64U;
  static constexpr size_t kEventQueueCapacity = 16U;

  static size_t ReadAvailableBytes(LibXR::UART& uart, uint8_t* buffer, size_t capacity,
                                   uint32_t timeout_ms)
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
    LibXR::ReadOperation op(sem, timeout_ms);
    const auto ans = uart.Read(LibXR::RawData(buffer, take), op);
    return (ans == LibXR::ErrorCode::OK) ? take : 0U;
  }

  static constexpr uint8_t ChecksumAdd(uint8_t cksum, uint8_t byte)
  {
    return static_cast<uint8_t>(cksum ^ byte);
  }

  static constexpr uint8_t ChecksumFinalize(uint8_t cksum)
  {
    return static_cast<uint8_t>(~cksum);
  }

  static void WriteU16BE(uint8_t* dst, uint16_t value)
  {
    dst[0] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    dst[1] = static_cast<uint8_t>(value & 0xFFU);
  }

  static size_t PayloadLen(const Frame& frame)
  {
    return (frame.len <= MAX_PAYLOAD_LEN) ? frame.len : 0U;
  }

  static uint16_t ReadU16LE(const uint8_t* data)
  {
    return static_cast<uint16_t>(static_cast<uint16_t>(data[0]) |
                                 (static_cast<uint16_t>(data[1]) << 8U));
  }

  static uint32_t ReadU32LE(const uint8_t* data)
  {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
  }

  static int32_t ReadI32LE(const uint8_t* data)
  {
    return static_cast<int32_t>(ReadU32LE(data));
  }

  static float ReadF32LE(const uint8_t* data)
  {
    const uint32_t raw = ReadU32LE(data);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
  }

  static void BuildText(const Frame& frame, char* out_text, size_t out_size)
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

  static EventKind KindFromFrame(const Frame& frame)
  {
    switch (frame.type)
    {
      case TYPE_FIRMWARE_STATUS:
        return IsVersionQuery(frame) ? EventKind::FIRMWARE_STATUS_QUERY : EventKind::FIRMWARE_STATUS;
      case TYPE_OTA:
        return EventKind::OTA;
      case TYPE_TEXT_MESSAGE:
        return EventKind::TEXT_MESSAGE;
      case TYPE_HUMAN_PRESENCE:
        return EventKind::HUMAN_PRESENCE;
      case TYPE_BREATH_RATE:
        return EventKind::BREATH_RATE;
      case TYPE_HEART_RATE:
        return EventKind::HEART_RATE;
      case TYPE_TARGET_RANGE:
        return EventKind::TARGET_RANGE;
      case TYPE_TRACK_POSITION:
        return EventKind::TRACK_POSITION;
      case TYPE_PHASE_TEST:
        return EventKind::PHASE_TEST;
      case TYPE_PERSONNEL_POSITION:
        return EventKind::PERSONNEL_POSITION;
      default:
        return EventKind::UNKNOWN;
    }
  }

  void ResetRuntimeState()
  {
    next_id_ = 0;
    has_last_frame_ = false;
    has_last_event_ = false;
    last_rx_us_ = LibXR::MicrosecondTimestamp();
    last_frame_us_ = LibXR::MicrosecondTimestamp();
    payload_frame_ = Frame{};
    last_frame_ = Frame{};
    snapshot_ = Snapshot{};
    last_event_ = Event{};

    for (size_t i = 0; i < kEventQueueCapacity; ++i)
    {
      event_queue_[i] = Event{};
    }
    event_queue_head_ = 0;
    event_queue_tail_ = 0;
    event_queue_size_ = 0;
  }

  void ResetParserState()
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

  bool StoreFrame(uint16_t id, uint16_t type, uint16_t len, const uint8_t* payload)
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

  void PushEvent(const Event& event)
  {
    if (event_queue_size_ == kEventQueueCapacity)
    {
      event_queue_tail_ = (event_queue_tail_ + 1U) % kEventQueueCapacity;
      event_queue_size_--;
    }

    event_queue_[event_queue_head_] = event;
    event_queue_head_ = (event_queue_head_ + 1U) % kEventQueueCapacity;
    event_queue_size_++;
  }

  void UpdateSnapshot(const Event& event)
  {
    if (event.result != ParseResult::OK)
    {
      return;
    }

    switch (event.kind)
    {
      case EventKind::FIRMWARE_STATUS:
        snapshot_.has_firmware_status = true;
        snapshot_.firmware_status = event.data.firmware_status;
        break;
      case EventKind::OTA:
        snapshot_.has_ota_message = true;
        snapshot_.ota_payload_len = event.data.ota_payload_len;
        break;
      case EventKind::TEXT_MESSAGE:
        snapshot_.has_text_message = true;
        std::memcpy(snapshot_.text_message, event.data.text, sizeof(snapshot_.text_message));
        break;
      case EventKind::HUMAN_PRESENCE:
        snapshot_.has_presence = true;
        snapshot_.presence = event.data.presence;
        break;
      case EventKind::BREATH_RATE:
        snapshot_.has_breath_rate = true;
        snapshot_.breath_rate_bpm = event.data.breath_rate_bpm;
        break;
      case EventKind::HEART_RATE:
        snapshot_.has_heart_rate = true;
        snapshot_.heart_rate_bpm = event.data.heart_rate_bpm;
        break;
      case EventKind::TARGET_RANGE:
        snapshot_.has_target_range = true;
        snapshot_.target_range = event.data.target_range;
        break;
      case EventKind::TRACK_POSITION:
        snapshot_.has_track_position = true;
        snapshot_.track_position = event.data.track_position;
        break;
      case EventKind::PHASE_TEST:
        snapshot_.has_phase_test = true;
        snapshot_.phase_test = event.data.phase_test;
        break;
      case EventKind::PERSONNEL_POSITION:
        snapshot_.has_personnel_position = true;
        snapshot_.personnel_position = event.data.personnel_position;
        break;
      case EventKind::FIRMWARE_STATUS_QUERY:
      case EventKind::UNKNOWN:
        break;
    }
  }

  void RecordEvent(const Event& event)
  {
    last_event_ = event;
    has_last_event_ = true;
    UpdateSnapshot(event);
    PushEvent(event);
  }

  void HandleFrame()
  {
    if (!StoreFrame(current_id_, current_type_, current_len_, payload_frame_.data))
    {
      return;
    }

    last_frame_us_ = LibXR::Timebase::GetMicroseconds();
    const Frame frame = last_frame_;
    if (config_.auto_reply_version && IsVersionQuery(frame))
    {
      (void)SendFrame(VERSION_QUERY_TYPE, &config_.local_version, sizeof(config_.local_version));
    }

    Event event = {};
    DecodeFrame(frame, event);
    RecordEvent(event);
  }

  void FeedByte(uint8_t byte)
  {
    const auto now_us = LibXR::Timebase::GetMicroseconds();
    last_rx_us_ = now_us;
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
          header_cksum_ = ChecksumAdd(0U, byte);
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
        if ((ChecksumFinalize(body_cksum_) == byte) && !discard_payload_)
        {
          HandleFrame();
        }
        ResetParserState();
        return;
    }
  }

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
  bool has_last_frame_ = false;
  bool has_last_event_ = false;
  LibXR::MicrosecondTimestamp last_rx_us_{};
  LibXR::MicrosecondTimestamp last_frame_us_{};
  LibXR::MicrosecondTimestamp last_byte_us_{};
  Frame payload_frame_ = {};
  Frame last_frame_ = {};
  Snapshot snapshot_ = {};
  Event last_event_ = {};
  Event event_queue_[kEventQueueCapacity] = {};
  size_t event_queue_head_ = 0;
  size_t event_queue_tail_ = 0;
  size_t event_queue_size_ = 0;
};
