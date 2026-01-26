# Hardware Module Compatibility Guide

## Overview

The Picocalc PIM has been updated to use existing Picocalc MicroPython modules instead of custom hardware drivers. This significantly improves compatibility and reduces setup complexity.

## Changes Made

### Display Module (`lib/display.py`)

**Before:**
- Custom SPI driver implementation
- Manual pin configuration (SCK, MOSI, CS, DC, RST)
- Direct hardware initialization
- ST7789/ST7365P command sequences

**After:**
- Automatic detection of existing modules
- Uses `picocalcdisplay` C module (if available)
- Falls back to `picocalc` Python module
- Graceful degradation to framebuffer mode
- **No pin configuration needed**

### Keyboard Module (`lib/keyboard.py`)

**Before:**
- Custom I2C driver implementation
- Manual keyboard controller address configuration
- Direct I2C communication with STM32

**After:**
- Automatic detection of existing modules
- Uses `pico_keyboard` module (if available)
- Falls back to `sys.stdin` for terminal input
- Graceful degradation to simulation mode
- **No I2C configuration needed**

## Module Detection

The PIM automatically detects available modules on startup:

### Display Detection Order
1. **`picocalcdisplay`** - C module (fastest, hardware-accelerated)
   ```python
   import picocalcdisplay
   picocalcdisplay.init()
   picocalcdisplay.show(buffer)
   ```

2. **`picocalc`** - Python module (alternative)
   ```python
   import picocalc
   picocalc.display_buffer(buffer)
   # or
   picocalc.show(buffer)
   ```

3. **Framebuffer mode** - No hardware (testing/development)
   - All operations work but don't update physical display
   - Useful for testing logic without hardware

### Keyboard Detection Order
1. **`pico_keyboard`** - Dedicated keyboard module
   ```python
   import pico_keyboard
   pico_keyboard.init()
   key = pico_keyboard.get_key()
   # or
   key = pico_keyboard.read()
   ```

2. **`sys.stdin`** - Standard input (REPL/terminal)
   ```python
   import sys, select
   char = sys.stdin.read(1)
   ```

3. **Simulation mode** - No hardware (testing/development)
   - Returns `None` for all key reads
   - Useful for testing without physical keyboard

## Benefits

### ✅ Compatibility
- Works with standard Picocalc firmware
- No custom hardware initialization needed
- Uses battle-tested existing modules

### ✅ Reliability
- Existing modules are pre-installed and tested
- No pin configuration errors
- Automatic fallback mechanisms

### ✅ Simplicity
- No hardware setup required
- Plug and play experience
- Fewer points of failure

### ✅ Flexibility
- Works in multiple modes (hardware, terminal, simulation)
- Easy testing without physical hardware
- Gradual degradation if modules missing

## Startup Messages

When you run `main.py`, you'll see detection messages:

### Successful Hardware Detection
```
Using picocalcdisplay module
Using pico_keyboard module
```

### Partial Detection
```
Using picocalcdisplay module
Keyboard: Using simulation mode
```

### No Hardware Detection
```
Display: Using framebuffer mode (no hardware)
Keyboard: Using simulation mode
```

## Common Picocalc Module Locations

Typical Picocalc firmware includes these modules in:

```
/lib/
├── picocalc.py          # Main Picocalc module
├── pico_keyboard.py     # Keyboard handler
└── picocalcdisplay/     # C module (if compiled)
```

Or in the frozen modules directory (built into firmware).

## Installing Missing Modules

If modules are not found, you can install them:

### Option 1: Use Standard Picocalc Firmware
The easiest solution - just use the official Picocalc MicroPython firmware which includes all modules.

### Option 2: Manual Installation
Copy the required modules to `/lib/`:

```bash
# Using mpremote
mpremote cp pico_keyboard.py :lib/pico_keyboard.py
mpremote cp picocalc.py :lib/picocalc.py
```

### Option 3: Compile C Modules
For `picocalcdisplay`, you'll need to compile it into the firmware:

```bash
# Clone driver repository
git clone https://github.com/zenodante/PicoCalc-micropython-driver

# Build MicroPython with the module
# Follow instructions in the repository
```

## Testing Module Detection

You can test which modules are available:

```python
import sys

# Test display modules
try:
    import picocalcdisplay
    print("✓ picocalcdisplay available")
except ImportError:
    print("✗ picocalcdisplay not found")

try:
    import picocalc
    print("✓ picocalc available")
except ImportError:
    print("✗ picocalc not found")

# Test keyboard module
try:
    import pico_keyboard
    print("✓ pico_keyboard available")
except ImportError:
    print("✗ pico_keyboard not found")

# Test stdin
if hasattr(sys.stdin, 'read'):
    print("✓ sys.stdin available")
else:
    print("✗ sys.stdin not available")
```

## Fallback Behavior

The PIM is designed to work in multiple scenarios:

| Scenario | Display | Keyboard | Usage |
|----------|---------|----------|-------|
| **Full Hardware** | `picocalcdisplay` | `pico_keyboard` | Normal use on Picocalc |
| **Terminal Mode** | Framebuffer | `sys.stdin` | Testing via REPL |
| **Simulation** | Framebuffer | None | Development/testing |
| **Mixed** | Hardware | `sys.stdin` | Debugging display |

## Resources

- [PicoCalc MicroPython Driver](https://github.com/zenodante/PicoCalc-micropython-driver)
- [PicoCalc MicroPython Examples](https://github.com/jamesmunro/PicoCalc_MicroPython)
- [PicoCalc GitHub](https://github.com/clockworkpi/PicoCalc)
- [MicroPython Documentation](https://docs.micropython.org/)

## Troubleshooting

### "Display: Using framebuffer mode"
- Install `picocalcdisplay` or `picocalc` module
- Use official Picocalc firmware
- Check `/lib/` directory for modules

### "Keyboard: Using simulation mode"
- Install `pico_keyboard` module
- Use official Picocalc firmware
- Try typing in the REPL (uses `sys.stdin`)

### Apps run but display doesn't update
- Verify module detection messages
- Check that `picocalcdisplay.show()` works in REPL
- Test with simple example:
  ```python
  import picocalcdisplay
  buffer = bytearray(320 * 320 * 2)
  picocalcdisplay.init()
  picocalcdisplay.show(buffer)
  ```

### Keyboard doesn't respond
- Check if `pico_keyboard` module exists
- Try typing directly in REPL
- Test keyboard with:
  ```python
  import pico_keyboard
  pico_keyboard.init()
  while True:
      key = pico_keyboard.get_key()
      if key:
          print(f"Key: {key}")
  ```

---

**Version:** 2.0 (Updated with existing module support)
**Date:** 2026-01-22
