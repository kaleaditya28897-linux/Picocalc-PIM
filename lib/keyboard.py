"""
Keyboard input handler for Picocalc
Uses existing keyboard modules and sys.stdin
"""

import sys
import select
import time

# Try to import Picocalc keyboard module
try:
    import pico_keyboard
    HAS_PICO_KEYBOARD = True
except ImportError:
    HAS_PICO_KEYBOARD = False

# Key codes
KEY_UP = ord('A')  # Arrow up often sends 'A' in some terminals
KEY_DOWN = ord('B')
KEY_LEFT = ord('D')
KEY_RIGHT = ord('C')
KEY_ENTER = ord('\r')
KEY_ESC = 27
KEY_BACKSPACE = ord('\b')
KEY_TAB = ord('\t')
KEY_SPACE = ord(' ')

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
KEY_E = ord('e')
KEY_D = ord('d')
KEY_Z = ord('z')


class Keyboard:
    """Keyboard input handler for Picocalc"""

    def __init__(self):
        """Initialize keyboard handler"""
        self.last_key = None
        self.key_buffer = []
        self.use_pico_keyboard = False

        # Try to use pico_keyboard module
        if HAS_PICO_KEYBOARD:
            try:
                pico_keyboard.init()
                self.use_pico_keyboard = True
                print("Using pico_keyboard module")
            except:
                print("pico_keyboard available but init failed")

        # Check if stdin is available for input
        try:
            # Test if stdin has data available
            self.has_stdin = hasattr(sys.stdin, 'read')
        except:
            self.has_stdin = False

        if not self.use_pico_keyboard and not self.has_stdin:
            print("Keyboard: Using simulation mode")

    def read_key(self):
        """Read a key press (non-blocking)"""
        # Try pico_keyboard module first
        if self.use_pico_keyboard and HAS_PICO_KEYBOARD:
            try:
                if hasattr(pico_keyboard, 'get_key'):
                    key = pico_keyboard.get_key()
                    if key:
                        return ord(key) if isinstance(key, str) else key
                elif hasattr(pico_keyboard, 'read'):
                    key = pico_keyboard.read()
                    if key:
                        return ord(key) if isinstance(key, str) else key
            except:
                pass

        # Try stdin
        if self.has_stdin:
            try:
                # Check if data is available (non-blocking)
                if hasattr(select, 'poll'):
                    poll = select.poll()
                    poll.register(sys.stdin, select.POLLIN)
                    events = poll.poll(0)  # 0 = non-blocking
                    if events:
                        char = sys.stdin.read(1)
                        if char:
                            return ord(char)
                else:
                    # Fallback: try to read without select
                    # This might block briefly
                    import sys
                    if hasattr(sys.stdin, 'read'):
                        try:
                            char = sys.stdin.read(1)
                            if char:
                                return ord(char)
                        except:
                            pass
            except:
                pass

        return None

    def wait_key(self, timeout=None):
        """Wait for a key press (blocking with optional timeout)"""
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
        print(prompt, end='')
        text = ""

        while True:
            key = self.wait_key(timeout=100)
            if not key:
                continue

            if key == KEY_ENTER or key == ord('\n'):
                print()
                return text
            elif key == KEY_ESC:
                print(" [Cancelled]")
                return None
            elif key == KEY_BACKSPACE or key == ord('\b') or key == 127:
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
        for _ in range(10):
            if not self.read_key():
                break
            time.sleep_ms(10)
