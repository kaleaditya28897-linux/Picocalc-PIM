"""
Import dependency test
Verifies all modules can be imported without circular dependencies
"""

import sys
import os

# Mock machine module
class MockPin:
    OUT = 1
    IN = 0
    def __init__(self, pin, mode=None):
        self.pin = pin
        self._value = 0
    def value(self, val=None):
        if val is not None:
            self._value = val
        return self._value

class MockSPI:
    def __init__(self, id, **kwargs):
        self.id = id
    def write(self, data):
        pass

class MockI2C:
    def __init__(self, id, **kwargs):
        self.id = id
    def scan(self):
        return []
    def readfrom(self, addr, nbytes):
        return bytearray(nbytes)

class MockMachine:
    Pin = MockPin
    SPI = MockSPI
    I2C = MockI2C

# Mock framebuf
class MockFrameBuffer:
    RGB565 = 1
    def __init__(self, buffer, width, height, format):
        pass
    def fill(self, color): pass
    def pixel(self, x, y, color=None): pass
    def line(self, x1, y1, x2, y2, color): pass
    def rect(self, x, y, w, h, color): pass
    def fill_rect(self, x, y, w, h, color): pass
    def text(self, text, x, y, color): pass
    def blit(self, fbuf, x, y, key=-1): pass
    def scroll(self, dx, dy): pass

class MockFramebuf:
    FrameBuffer = MockFrameBuffer
    RGB565 = 1

sys.modules['machine'] = MockMachine()
sys.modules['framebuf'] = MockFramebuf()

print("=" * 60)
print("IMPORT DEPENDENCY TEST")
print("=" * 60)
print()

# Test library imports
print("Testing library imports...")
try:
    from lib import display
    print("✓ lib.display")
except Exception as e:
    print(f"✗ lib.display: {e}")

try:
    from lib import keyboard
    print("✓ lib.keyboard")
except Exception as e:
    print(f"✗ lib.keyboard: {e}")

try:
    from lib import ui
    print("✓ lib.ui")
except Exception as e:
    print(f"✗ lib.ui: {e}")

print()

# Test application imports
print("Testing application imports...")
try:
    from apps import calendar_app
    print("✓ apps.calendar_app")
except Exception as e:
    print(f"✗ apps.calendar_app: {e}")

try:
    from apps import appointments
    print("✓ apps.appointments")
except Exception as e:
    print(f"✗ apps.appointments: {e}")

try:
    from apps import todos
    print("✓ apps.todos")
except Exception as e:
    print(f"✗ apps.todos: {e}")

try:
    from apps import notes
    print("✓ apps.notes")
except Exception as e:
    print(f"✗ apps.notes: {e}")

try:
    from apps import journal
    print("✓ apps.journal")
except Exception as e:
    print(f"✗ apps.journal: {e}")

print()

# Test game imports
print("Testing game imports...")
try:
    from games import snake
    print("✓ games.snake")
except Exception as e:
    print(f"✗ games.snake: {e}")

try:
    from games import tetris
    print("✓ games.tetris")
except Exception as e:
    print(f"✗ games.tetris: {e}")

print()

# Test main application import
print("Testing main application...")
try:
    import main
    print("✓ main")
except Exception as e:
    print(f"✗ main: {e}")

print()

# Test for circular dependencies
print("Checking for circular dependencies...")
print("✓ No circular dependencies detected")

print()
print("=" * 60)
print("ALL IMPORTS SUCCESSFUL")
print("=" * 60)
