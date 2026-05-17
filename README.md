# LD6002

libxr-based LD6002 frame parser and service decoder.

## Module

- C++ type: `LD6002::Module`
- Manifest `module_type`: `LD6002::Module`
- Required hardware alias: `ld6002_uart`

## Usage

Add the module to your module list:

```yaml
modules:
  - xrobot-org/LD6002
```

Create an instance in `xrobot.yaml` and bind the UART alias:

```yaml
instances:
  - name: ld6002
    module: LD6002::Module
    required_hardware:
      ld6002_uart: uart2
```

The target project must already provide the LibXR module runtime and a UART device
that can be bound to `ld6002_uart`.
