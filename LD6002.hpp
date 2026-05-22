// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: LD6002 radar driver
constructor_args:
  - config:
      expr: LD6002::Config{}
template_args: []
required_hardware:
  ld6002_uart
depends: []
=== END MANIFEST === */
// clang-format on

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_mem.hpp"
#include "libxr_rw.hpp"
#include "message.hpp"
#include "semaphore.hpp"
#include "thread.hpp"
#include "timebase.hpp"
#include "uart.hpp"

class LD6002
{
 public:
  static constexpr const char* REQUIRED_UART_ALIAS = "ld6002_uart";

  static constexpr const char* FRAME_TOPIC_NAME = "/ld6002/frame";
  static constexpr const char* EVENT_TOPIC_NAME = "/ld6002/event";
  static constexpr const char* VERSION_TOPIC_NAME = "/ld6002/firmware_status";
  static constexpr const char* TEXT_TOPIC_NAME = "/ld6002/text_message";
  static constexpr const char* PRESENCE_TOPIC_NAME = "/ld6002/human_presence";
  static constexpr const char* BREATH_RATE_TOPIC_NAME = "/ld6002/breath_rate";
  static constexpr const char* HEART_RATE_TOPIC_NAME = "/ld6002/heart_rate";
  static constexpr const char* TARGET_RANGE_TOPIC_NAME = "/ld6002/target_range";
  static constexpr const char* TRACK_POSITION_TOPIC_NAME = "/ld6002/track_position";
  static constexpr const char* PHASE_TEST_TOPIC_NAME = "/ld6002/phase_test";
  static constexpr const char* PERSONNEL_POSITION_TOPIC_NAME = "/ld6002/personnel_position";

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

  struct Config
  {
    uint32_t uart_read_timeout_ms = 0;
    uint32_t parser_timeout_ms = 50;
    size_t worker_stack_depth = 1024;
    LibXR::Thread::Priority worker_priority = LibXR::Thread::Priority::MEDIUM;
    bool auto_reply_version = false;
    bool request_version_on_init = true;
    Version local_version = {0, 1, 0, 0};
  };

  LD6002(LibXR::HardwareContainer& hw) : LD6002(hw, Config{}) {}

  LD6002(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& appmgr)
      : LD6002(hw, appmgr, Config{})
  {
  }

  LD6002(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& appmgr, Config config)
      : LD6002(hw, config)
  {
    (void)appmgr;
  }

  explicit LD6002(LibXR::HardwareContainer& hw, Config config)
      : uart_(*hw.FindOrExit<LibXR::UART>({REQUIRED_UART_ALIAS})),
        config_(config),
        frame_topic_(LibXR::Topic::FindOrCreate<Frame>(FRAME_TOPIC_NAME, nullptr, false, false,
                                                       true)),
        event_topic_(LibXR::Topic::FindOrCreate<Event>(EVENT_TOPIC_NAME, nullptr, false, false,
                                                       true)),
        version_topic_(LibXR::Topic::FindOrCreate<Version>(VERSION_TOPIC_NAME, nullptr, false,
                                                           false, true)),
        text_topic_(LibXR::Topic::FindOrCreate<EventText>(TEXT_TOPIC_NAME, nullptr, false, false,
                                                          true)),
        presence_topic_(LibXR::Topic::FindOrCreate<Presence>(PRESENCE_TOPIC_NAME, nullptr, false,
                                                             false, true)),
        breath_rate_topic_(LibXR::Topic::FindOrCreate<FloatValue>(BREATH_RATE_TOPIC_NAME, nullptr,
                                                                  false, false, true)),
        heart_rate_topic_(LibXR::Topic::FindOrCreate<FloatValue>(HEART_RATE_TOPIC_NAME, nullptr,
                                                                 false, false, true)),
        target_range_topic_(LibXR::Topic::FindOrCreate<TargetRange>(TARGET_RANGE_TOPIC_NAME,
                                                                    nullptr, false, false,
                                                                    true)),
        track_position_topic_(LibXR::Topic::FindOrCreate<TrackPosition>(TRACK_POSITION_TOPIC_NAME,
                                                                        nullptr, false, false,
                                                                        true)),
        phase_test_topic_(LibXR::Topic::FindOrCreate<PhaseTest>(PHASE_TEST_TOPIC_NAME, nullptr,
                                                                false, false, true)),
        personnel_position_topic_(
            LibXR::Topic::FindOrCreate<PersonnelPosition>(PERSONNEL_POSITION_TOPIC_NAME, nullptr,
                                                          false, false, true))
  {
    Reset();
    if (config_.request_version_on_init)
    {
      const auto ans = RequestVersion();
      ASSERT(ans == LibXR::ErrorCode::OK);
    }

    worker_thread_.Create(this, WorkerThreadFun, "ld6002", config_.worker_stack_depth,
                          config_.worker_priority);
  }

  void Reset()
  {
    next_id_ = 0;
    ClearParserState();
    payload_frame_ = Frame{};
  }

  LibXR::ErrorCode SendFrame(uint16_t type, const void* payload, size_t payload_len)
  {
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
    return uart_.Write(LibXR::ConstRawData(frame, frame_len), op);
  }

  LibXR::ErrorCode RequestVersion()
  {
    return SendFrame(VERSION_QUERY_TYPE, kVersionQueryPayload, sizeof(kVersionQueryPayload));
  }

  LibXR::Topic FrameTopic() const { return frame_topic_; }
  LibXR::Topic EventTopic() const { return event_topic_; }
  LibXR::Topic VersionTopic() const { return version_topic_; }
  LibXR::Topic TextTopic() const { return text_topic_; }
  LibXR::Topic PresenceTopic() const { return presence_topic_; }
  LibXR::Topic BreathRateTopic() const { return breath_rate_topic_; }
  LibXR::Topic HeartRateTopic() const { return heart_rate_topic_; }
  LibXR::Topic TargetRangeTopic() const { return target_range_topic_; }
  LibXR::Topic TrackPositionTopic() const { return track_position_topic_; }
  LibXR::Topic PhaseTestTopic() const { return phase_test_topic_; }
  LibXR::Topic PersonnelPositionTopic() const { return personnel_position_topic_; }

  static bool IsVersionQuery(const Frame& frame)
  {
    return (frame.type == VERSION_QUERY_TYPE) &&
           (frame.len == sizeof(kVersionQueryPayload)) &&
           (LibXR::Memory::FastCmp(frame.data, kVersionQueryPayload,
                                   sizeof(kVersionQueryPayload)) == 0);
  }

  static bool ParseVersionResponse(const Frame& frame, Version& out_version)
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
  struct FloatValue
  {
    float value = 0.0f;
  };

  struct EventText
  {
    char text[MAX_PAYLOAD_LEN + 1U] = {0};
  };

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
  static constexpr size_t kReadChunkSize = 64U;

  static void WorkerThreadFun(LD6002* self)
  {
    ASSERT(self != nullptr);
    while (true)
    {
      self->RunWorkerLoop();
    }
  }

  void RunWorkerLoop()
  {
    if (!WaitForReadable())
    {
      return;
    }

    uint8_t rx_buffer[kReadChunkSize] = {0};
    while (true)
    {
      const size_t read_size = ReadAvailableBytes(uart_, rx_buffer, sizeof(rx_buffer));
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

  bool WaitForReadable()
  {
    LibXR::Semaphore sem(0);
    LibXR::ReadOperation op(sem, UartReadTimeoutMs());
    const auto ans = uart_.Read(LibXR::RawData(nullptr, 0U), op);
    ASSERT((ans == LibXR::ErrorCode::OK) || (ans == LibXR::ErrorCode::TIMEOUT));
    return ans == LibXR::ErrorCode::OK;
  }

  uint32_t UartReadTimeoutMs() const
  {
    return (config_.uart_read_timeout_ms == 0U) ? UINT32_MAX : config_.uart_read_timeout_ms;
  }

  static size_t ReadAvailableBytes(LibXR::UART& uart, uint8_t* buffer, size_t capacity)
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
    LibXR::ReadOperation::OperationPollingStatus status =
        LibXR::ReadOperation::OperationPollingStatus::READY;
    LibXR::ReadOperation op(status);
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
    LibXR::Memory::FastCopy(&value, &raw, sizeof(value));
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

    LibXR::Memory::FastCopy(out_text, frame.data, copy_len);
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
        return IsVersionQuery(frame) ? EventKind::FIRMWARE_STATUS_QUERY
                                     : EventKind::FIRMWARE_STATUS;
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

  void ClearParserState()
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

  void PublishTypedData(const Event& event, LibXR::MicrosecondTimestamp timestamp)
  {
    if (event.result != ParseResult::OK)
    {
      return;
    }

    switch (event.kind)
    {
      case EventKind::FIRMWARE_STATUS:
      {
        Version version = event.data.firmware_status;
        version_topic_.Publish(version, timestamp);
        break;
      }

      case EventKind::TEXT_MESSAGE:
      {
        EventText text = {};
        LibXR::Memory::FastCopy(text.text, event.data.text, sizeof(text.text));
        text_topic_.Publish(text, timestamp);
        break;
      }

      case EventKind::HUMAN_PRESENCE:
      {
        Presence presence = event.data.presence;
        presence_topic_.Publish(presence, timestamp);
        break;
      }

      case EventKind::BREATH_RATE:
      {
        FloatValue breath = {event.data.breath_rate_bpm};
        breath_rate_topic_.Publish(breath, timestamp);
        break;
      }

      case EventKind::HEART_RATE:
      {
        FloatValue heart = {event.data.heart_rate_bpm};
        heart_rate_topic_.Publish(heart, timestamp);
        break;
      }

      case EventKind::TARGET_RANGE:
      {
        TargetRange target_range = event.data.target_range;
        target_range_topic_.Publish(target_range, timestamp);
        break;
      }

      case EventKind::TRACK_POSITION:
      {
        TrackPosition track_position = event.data.track_position;
        track_position_topic_.Publish(track_position, timestamp);
        break;
      }

      case EventKind::PHASE_TEST:
      {
        PhaseTest phase_test = event.data.phase_test;
        phase_test_topic_.Publish(phase_test, timestamp);
        break;
      }

      case EventKind::PERSONNEL_POSITION:
      {
        PersonnelPosition personnel_position = event.data.personnel_position;
        personnel_position_topic_.Publish(personnel_position, timestamp);
        break;
      }

      case EventKind::FIRMWARE_STATUS_QUERY:
      case EventKind::OTA:
      case EventKind::UNKNOWN:
      default:
        break;
    }
  }

  void PublishFrameAndEvent(Frame& frame, Event& event, LibXR::MicrosecondTimestamp timestamp)
  {
    frame_topic_.Publish(frame, timestamp);
    event_topic_.Publish(event, timestamp);
    PublishTypedData(event, timestamp);
  }

  void HandleFrame(uint16_t id, uint16_t type, uint16_t len, const uint8_t* payload)
  {
    if (len > MAX_PAYLOAD_LEN)
    {
      return;
    }

    Frame frame = {};
    frame.id = id;
    frame.type = type;
    frame.len = len;
    if (len > 0U)
    {
      LibXR::Memory::FastCopy(frame.data, payload, len);
    }

    if (config_.auto_reply_version && IsVersionQuery(frame))
    {
      (void)SendFrame(VERSION_QUERY_TYPE, &config_.local_version, sizeof(config_.local_version));
    }

    Event event = {};
    DecodeFrame(frame, event);
    const LibXR::MicrosecondTimestamp timestamp = LibXR::Timebase::GetMicroseconds();
    PublishFrameAndEvent(frame, event, timestamp);
  }

  void FeedByte(uint8_t byte)
  {
    const auto now_us = LibXR::Timebase::GetMicroseconds();
    if ((state_ != ParserState::WAIT_SOF) &&
        (static_cast<uint64_t>(last_byte_us_) != 0U) &&
        ((now_us - last_byte_us_).ToMillisecond() > config_.parser_timeout_ms))
    {
      ClearParserState();
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
          ClearParserState();
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
            HandleFrame(current_id_, current_type_, current_len_, payload_frame_.data);
          }
          ClearParserState();
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
          HandleFrame(current_id_, current_type_, current_len_, payload_frame_.data);
        }
        ClearParserState();
        return;
    }
  }

  LibXR::UART& uart_;
  Config config_{};
  LibXR::Thread worker_thread_;
  LibXR::Topic frame_topic_;
  LibXR::Topic event_topic_;
  LibXR::Topic version_topic_;
  LibXR::Topic text_topic_;
  LibXR::Topic presence_topic_;
  LibXR::Topic breath_rate_topic_;
  LibXR::Topic heart_rate_topic_;
  LibXR::Topic target_range_topic_;
  LibXR::Topic track_position_topic_;
  LibXR::Topic phase_test_topic_;
  LibXR::Topic personnel_position_topic_;
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
  LibXR::MicrosecondTimestamp last_byte_us_{};
  Frame payload_frame_ = {};
};
