# LD6002

libxr-based LD6002 single-class driver and frame decoder.

## Driver

- C++ type: `LD6002`
- Required hardware alias: `ld6002_uart`

## Usage

Construct `LD6002` with either:

- `LibXR::HardwareContainer&` and alias `ld6002_uart`
- `LibXR::UART&`
- `LD6002::Config`

Typical polling usage:

```cpp
LD6002 ld6002(hw);
(void)ld6002.Init();

while (true)
{
  ld6002.Poll();

  LD6002::Event event = {};
  while (ld6002.PopEvent(event))
  {
    // consume decoded events
  }
}
```

Key API notes:

- `Init()` performs explicit driver startup and optional version query.
- `Poll()` drains UART bytes and updates internal parser/state.
- `GetStatus()` exposes initialization, queue, and online-state information.
