"""
Keyboard input handler for Picocalc
Handles 67-key keyboard via I2C interface with STM32 co-processor
"""

import machine
import time

# Key codes for Picocalc keyboard
# These are common key definitions - adjust based on actual hardware
KEY_UP = 0x01
KEY_DOWN = 0x02
KEY_LEFT = 0x03
KEY_RIGHT = 0x04
KEY_ENTER = 0x0D
KEY_ESC = 0x1B
KEY_BACKSPACE = 0x08
KEY_TAB = 0x09
KEY_SPACE = 0x20

# Number keys
KEY_0 = ord('0')
KEY_1 = ord('1')
KEY_2 = ord('2')
KEY_3 = ord('3')
KEY_4 = ord('4')
KEY_5 = ord('5')
KEY_6 = ord('6')
KEY_7 = ord('7')
KEY_8 = ord('8')
KEY_9 = ord('9')

# Letter keys
KEY_A = ord('a')
KEY_Z = ord('z')


class Keyboard:
    """Keyboard input handler for Picocalc"""

    def __init__(self):
        """Initialize keyboard handler"""
        self._init_hardware()
        self.last_key = None
        self.key_buffer = []

    def _init_hardware(self):
        """Initialize I2C connection to keyboard controller"""
        try:
            # I2C setup for keyboard (STM32 co-processor)
            # Note: Adjust pins and address according to Picocalc schematic
            self.i2c = machine.I2C(
                0,
                scl=machine.Pin(5),
                sda=machine.Pin(4),
                freq=400000
            )

            # Keyboard controller I2C address (typical: 0x55 or similar)
            self.kbd_addr = 0x55

            # Try to communicate with keyboard
            try:
                self.i2c.scan()
                self.hardware_available = True
            except:
                self.hardware_available = False
                print("Keyboard hardware not detected - using simulation mode")

        except Exception as e:
            print(f"Keyboard init warning: {e}")
            self.i2c = None
            self.hardware_available = False

    def read_key(self):
        """Read a key press (non-blocking)"""
        if self.hardware_available and self.i2c:
            try:
                # Read key code from I2C
                data = self.i2c.readfrom(self.kbd_addr, 1)
                if data and data[0] != 0:
                    key = data[0]
                    if key != self.last_key:
                        self.last_key = key
                        return key
                else:
                    self.last_key = None
            except:
                pass

        # Simulation mode for testing without hardware
        # In real hardware, this would not be needed
        return None

    def wait_key(self, timeout=None):
        """Wait for a key press (blocking)"""
        start = time.ticks_ms()
        while True:
            key = self.read_key()
            if key:
                return key

            if timeout and time.ticks_diff(time.ticks_ms(), start) > timeout:
                return None

            time.sleep_ms(10)

    def get_char(self):
        """Get character input (blocking)"""
        key = self.wait_key()
        if key:
            # Convert key code to character
            if 32 <= key <= 126:  # Printable ASCII
                return chr(key)
            elif key == KEY_ENTER:
                return '\n'
            elif key == KEY_BACKSPACE:
                return '\b'
            elif key == KEY_SPACE:
                return ' '
        return None

    def input_text(self, prompt="", max_length=50):
        """Get text input from user"""
        text = ""
        print(prompt, end='')

        while True:
            key = self.wait_key()
            if not key:
                continue

            if key == KEY_ENTER:
                print()
                return text
            elif key == KEY_ESC:
                print(" [Cancelled]")
                return None
            elif key == KEY_BACKSPACE:
                if text:
                    text = text[:-1]
                    print('\b \b', end='')
            elif 32 <= key <= 126 and len(text) < max_length:
                char = chr(key)
                text += char
                print(char, end='')

            time.sleep_ms(10)

    def input_number(self, prompt="", min_val=None, max_val=None):
        """Get numeric input from user"""
        while True:
            text = self.input_text(prompt)
            if text is None:
                return None

            try:
                num = int(text)
                if min_val is not None and num < min_val:
                    print(f"Value must be >= {min_val}")
                    continue
                if max_val is not None and num > max_val:
                    print(f"Value must be <= {max_val}")
                    continue
                return num
            except ValueError:
                print("Invalid number, try again")

    def is_key_pressed(self, key_code):
        """Check if specific key is currently pressed"""
        current = self.read_key()
        return current == key_code

    def flush(self):
        """Clear key buffer"""
        self.key_buffer.clear()
        self.last_key = None
        # Read any pending keys
        while self.read_key():
            time.sleep_ms(10)


class SimulatedKeyboard(Keyboard):
    """Simulated keyboard for testing without hardware"""

    def __init__(self):
        """Initialize simulated keyboard"""
        self.last_key = None
        self.key_buffer = []
        self.hardware_available = False
        self.simulation_keys = []

    def simulate_key(self, key_code):
        """Simulate a key press"""
        self.simulation_keys.append(key_code)

    def simulate_text(self, text):
        """Simulate text input"""
        for char in text:
            self.simulation_keys.append(ord(char))
        self.simulation_keys.append(KEY_ENTER)

    def read_key(self):
        """Read simulated key"""
        if self.simulation_keys:
            return self.simulation_keys.pop(0)
        return None
