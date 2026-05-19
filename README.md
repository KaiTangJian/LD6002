# LD6002

libxr-based LD6002 single-class driver and frame decoder.

## Driver

- C++ type: `LD6002`
- Required hardware alias: `ld6002_uart`

## Usage

Construct `LD6002` with `LibXR::HardwareContainer&`.
The module resolves `ld6002_uart` in the constructor, starts its internal worker thread,
consumes the DMA-backed UART queue there, and publishes parsed outputs through topics.

```cpp
LD6002 ld6002(hw, LD6002::Config{});
LibXR::LockFreeQueue<LibXR::Topic::Message<LD6002::Event>> event_queue(16);
LibXR::Topic::QueuedSubscriber event_subscriber(ld6002.EventTopic(), event_queue);

while (true)
{
  LibXR::Topic::Message<LD6002::Event> message = {};
  while (event_queue.Pop(message) == LibXR::ErrorCode::OK)
  {
    // consume decoded events in application layer
  }
}
```

Key API notes:

- The constructor performs topic setup, optional version query, and worker-thread startup.
- `Reset()` resets the parser/transport worker state only.
- Runtime timestamps, online heuristics, aggregation, and history are application-layer concerns.
