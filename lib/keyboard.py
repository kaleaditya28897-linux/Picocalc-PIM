"""
Keyboard input handler for Picocalc
Uses sys.stdin.read(1) for key input
"""

import sys
import time

# Key codes
KEY_UP = 256
KEY_DOWN = 257
KEY_LEFT = 258
KEY_RIGHT = 259
KEY_ENTER = ord('\n')
KEY_ESC = 27
KEY_BACKSPACE = 127
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
        pass

    def wait_key(self, timeout=None):
        """Wait for a key press (blocking)"""
        c = sys.stdin.read(1)
        if not c:
            return None
        if c == '\x1b':
            c2 = sys.stdin.read(1)
            if c2 == '[':
                c3 = sys.stdin.read(1)
                if c3 == 'A':
                    return KEY_UP
                elif c3 == 'B':
                    return KEY_DOWN
                elif c3 == 'C':
                    return KEY_RIGHT
                elif c3 == 'D':
                    return KEY_LEFT
                else:
                    return ord(c3)
            else:
                return KEY_ESC
        return ord(c)

    def get_char(self):
        """Get character input (blocking)"""
        key = self.wait_key()
        if key:
            if 32 <= key <= 126:
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
            key = self.wait_key()
            if not key:
                continue

            if key == KEY_ENTER or key == ord('\n'):
                print()
                return text
            elif key == KEY_ESC:
                print(" [Cancelled]")
                return None
            elif key == KEY_BACKSPACE or key == 127:
                if text:
                    text = text[:-1]
                    print('\b \b', end='')
            elif 32 <= key <= 126 and len(text) < max_length:
                char = chr(key)
                text += char
                print(char, end='')

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
        current = self.wait_key()
        return current == key_code

    def flush(self):
        """Clear key buffer"""
        pass
