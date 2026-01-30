"""
UI utilities for Picocalc PIM
Provides menu, dialog boxes, and other UI components
"""

import time
from lib.keyboard import KEY_UP, KEY_DOWN, KEY_ENTER, KEY_ESC


class Menu:
    """Menu widget for selection"""

    def __init__(self, display, keyboard, title="Menu", items=None):
        """
        Initialize menu

        Args:
            display: Display instance
            keyboard: Keyboard instance
            title: Menu title
            items: List of (label, callback) tuples
        """
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.items = items or []
        self.selected = 0
        self.scroll_offset = 0
        self.max_visible = 10  # Maximum items visible at once

    def add_item(self, label, callback):
        """Add menu item"""
        self.items.append((label, callback))

    def draw(self):
        """Draw menu"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(self.title, 10, 10, self.display.WHITE, size=1)

        # Draw menu items
        y = 40
        visible_start = self.scroll_offset
        visible_end = min(visible_start + self.max_visible, len(self.items))

        for i in range(visible_start, visible_end):
            label, _ = self.items[i]
            bg_color = self.display.BLUE if i == self.selected else self.display.BLACK
            fg_color = self.display.WHITE

            # Draw selection highlight
            if i == self.selected:
                self.display.rect(5, y - 2, self.display.WIDTH - 10, 20,
                                self.display.BLUE, fill=True)

            # Draw item text
            self.display.text(f"{i + 1}. {label}", 10, y, fg_color)
            y += 22

        # Draw scrollbar if needed
        if len(self.items) > self.max_visible:
            bar_height = (self.max_visible * 220) // len(self.items)
            bar_y = 40 + (self.scroll_offset * 220) // len(self.items)
            self.display.rect(self.display.WIDTH - 10, bar_y, 5, bar_height,
                            self.display.GRAY, fill=True)

        # Draw instructions
        instructions = "UP/DOWN: Select  ENTER: OK  ESC: Back"
        self.display.text(instructions, 10, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

    def show(self):
        """Show menu and handle input"""
        self.selected = 0
        self.scroll_offset = 0

        while True:
            self.draw()
            key = self.keyboard.wait_key(timeout=5000)

            if key == KEY_UP:
                if self.selected > 0:
                    self.selected -= 1
                    if self.selected < self.scroll_offset:
                        self.scroll_offset = self.selected
            elif key == KEY_DOWN:
                if self.selected < len(self.items) - 1:
                    self.selected += 1
                    if self.selected >= self.scroll_offset + self.max_visible:
                        self.scroll_offset = self.selected - self.max_visible + 1
            elif key == KEY_ENTER:
                if 0 <= self.selected < len(self.items):
                    _, callback = self.items[self.selected]
                    if callback:
                        result = callback()
                        if result == "exit":
                            return
            elif key == KEY_ESC:
                return

            time.sleep_ms(50)


class MessageBox:
    """Message box dialog"""

    def __init__(self, display, keyboard, title="Message", message=""):
        """Initialize message box"""
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.message = message

    def show(self):
        """Show message box"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(self.title, 10, 10, self.display.WHITE)

        # Draw message
        y = 50
        lines = self.message.split('\n')
        for line in lines:
            self.display.text(line, 10, y, self.display.WHITE)
            y += 15

        # Draw OK button
        btn_y = self.display.HEIGHT - 50
        self.display.rect(110, btn_y, 100, 30, self.display.GREEN, fill=True)
        self.display.text("OK", 145, btn_y + 10, self.display.WHITE)

        self.display.show()

        # Wait for key press
        self.keyboard.wait_key()


class InputDialog:
    """Input dialog for text entry"""

    def __init__(self, display, keyboard, title="Input", prompt="Enter text:",
                 default="", max_length=50):
        """Initialize input dialog"""
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.prompt = prompt
        self.text = default
        self.max_length = max_length
        self.cursor_pos = len(default)

    def draw(self):
        """Draw input dialog"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(self.title, 10, 10, self.display.WHITE)

        # Draw prompt
        self.display.text(self.prompt, 10, 50, self.display.WHITE)

        # Draw input box
        box_y = 80
        self.display.rect(10, box_y, self.display.WIDTH - 20, 30, self.display.WHITE, fill=True)
        self.display.text(self.text, 15, box_y + 10, self.display.BLACK)

        # Draw cursor
        cursor_x = 15 + len(self.text) * 8
        if cursor_x < self.display.WIDTH - 15:
            self.display.line(cursor_x, box_y + 8, cursor_x, box_y + 22,
                            self.display.RED)

        # Draw instructions
        instructions = "Type text  ENTER: OK  ESC: Cancel"
        self.display.text(instructions, 10, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

    def show(self):
        """Show input dialog and get text"""
        from lib.keyboard import KEY_BACKSPACE

        while True:
            self.draw()
            key = self.keyboard.wait_key(timeout=500)

            if key == KEY_ENTER:
                return self.text
            elif key == KEY_ESC:
                return None
            elif key == KEY_BACKSPACE:
                if self.text:
                    self.text = self.text[:-1]
                    self.cursor_pos = len(self.text)
            elif key and 32 <= key <= 126 and len(self.text) < self.max_length:
                self.text += chr(key)
                self.cursor_pos = len(self.text)

            time.sleep_ms(50)


class ConfirmDialog:
    """Confirmation dialog"""

    def __init__(self, display, keyboard, title="Confirm", message="Are you sure?"):
        """Initialize confirm dialog"""
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.message = message
        self.selected = 0  # 0 = Yes, 1 = No

    def draw(self):
        """Draw confirmation dialog"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.YELLOW, fill=True)
        self.display.text(self.title, 10, 10, self.display.BLACK)

        # Draw message
        y = 60
        lines = self.message.split('\n')
        for line in lines:
            self.display.text(line, 10, y, self.display.WHITE)
            y += 15

        # Draw buttons
        btn_y = self.display.HEIGHT - 60

        # Yes button
        yes_color = self.display.GREEN if self.selected == 0 else self.display.GRAY
        self.display.rect(60, btn_y, 80, 30, yes_color, fill=True)
        self.display.text("Yes", 85, btn_y + 10, self.display.WHITE)

        # No button
        no_color = self.display.RED if self.selected == 1 else self.display.GRAY
        self.display.rect(180, btn_y, 80, 30, no_color, fill=True)
        self.display.text("No", 210, btn_y + 10, self.display.WHITE)

        # Draw instructions
        instructions = "LEFT/RIGHT: Select  ENTER: OK"
        self.display.text(instructions, 50, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

    def show(self):
        """Show confirmation dialog"""
        from lib.keyboard import KEY_LEFT, KEY_RIGHT

        self.selected = 1  # Default to No

        while True:
            self.draw()
            key = self.keyboard.wait_key()

            if key == KEY_LEFT:
                self.selected = 0
            elif key == KEY_RIGHT:
                self.selected = 1
            elif key == KEY_ENTER:
                return self.selected == 0
            elif key == KEY_ESC:
                return False

            time.sleep_ms(50)


class TextAreaDialog:
    """Multi-line text area dialog for longer text entry"""

    def __init__(self, display, keyboard, title="Input", prompt="Enter text:",
                 default="", max_length=500):
        """Initialize text area dialog"""
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.prompt = prompt
        self.text = default
        self.max_length = max_length
        self.cursor_pos = len(default)
        self.scroll_offset = 0
        self.chars_per_line = (display.WIDTH - 30) // 8  # 8 pixels per char
        self.visible_lines = 10  # Number of lines visible in text area
        self.box_y = 50
        self.box_height = self.visible_lines * 15 + 10  # 15px per line + padding

    def _wrap_text(self, text):
        """Wrap text into lines that fit the text area"""
        lines = []
        words = text.split(' ')
        current_line = ""

        for word in words:
            # Handle empty words (multiple spaces)
            if not word:
                continue
            # Check if word itself is too long
            while len(word) > self.chars_per_line:
                if current_line:
                    lines.append(current_line)
                    current_line = ""
                lines.append(word[:self.chars_per_line])
                word = word[self.chars_per_line:]

            test_line = current_line + " " + word if current_line else word
            if len(test_line) <= self.chars_per_line:
                current_line = test_line
            else:
                if current_line:
                    lines.append(current_line)
                current_line = word

        if current_line:
            lines.append(current_line)

        return lines if lines else [""]

    def _get_cursor_line(self):
        """Get which line the cursor is on"""
        text_before_cursor = self.text[:self.cursor_pos]
        lines = self._wrap_text(text_before_cursor) if text_before_cursor else [""]
        return len(lines) - 1

    def draw(self):
        """Draw text area dialog"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(self.title, 10, 10, self.display.WHITE)

        # Draw prompt
        self.display.text(self.prompt, 10, 35, self.display.WHITE)

        # Draw text area box
        self.display.rect(10, self.box_y, self.display.WIDTH - 20,
                         self.box_height, self.display.WHITE, fill=True)

        # Draw border
        self.display.rect(10, self.box_y, self.display.WIDTH - 20,
                         self.box_height, self.display.GRAY)

        # Get wrapped lines
        lines = self._wrap_text(self.text) if self.text else [""]

        # Ensure cursor is visible by adjusting scroll
        cursor_line = self._get_cursor_line()
        if cursor_line < self.scroll_offset:
            self.scroll_offset = cursor_line
        elif cursor_line >= self.scroll_offset + self.visible_lines:
            self.scroll_offset = cursor_line - self.visible_lines + 1

        # Draw visible lines
        y = self.box_y + 5
        for i in range(self.scroll_offset, min(self.scroll_offset + self.visible_lines, len(lines))):
            self.display.text(lines[i], 15, y, self.display.BLACK)
            y += 15

        # Draw cursor
        if self.text:
            text_before_cursor = self.text[:self.cursor_pos]
            cursor_lines = self._wrap_text(text_before_cursor) if text_before_cursor else [""]
            cursor_line_idx = len(cursor_lines) - 1
            cursor_col = len(cursor_lines[-1]) if cursor_lines else 0
        else:
            cursor_line_idx = 0
            cursor_col = 0

        # Only draw cursor if visible
        if self.scroll_offset <= cursor_line_idx < self.scroll_offset + self.visible_lines:
            cursor_y = self.box_y + 5 + (cursor_line_idx - self.scroll_offset) * 15
            cursor_x = 15 + cursor_col * 8
            if cursor_x < self.display.WIDTH - 15:
                self.display.line(cursor_x, cursor_y, cursor_x, cursor_y + 12,
                                self.display.RED)

        # Draw scroll indicator if needed
        if len(lines) > self.visible_lines:
            total_lines = len(lines)
            bar_height = max(20, (self.visible_lines * self.box_height) // total_lines)
            bar_y = self.box_y + (self.scroll_offset * (self.box_height - bar_height)) // max(1, total_lines - self.visible_lines)
            self.display.rect(self.display.WIDTH - 18, bar_y, 4, bar_height,
                            self.display.BLUE, fill=True)

        # Draw character count
        count_text = f"{len(self.text)}/{self.max_length}"
        self.display.text(count_text, self.display.WIDTH - 80,
                         self.box_y + self.box_height + 5, self.display.GRAY)

        # Draw instructions
        instructions = "Type text  ENTER: OK  ESC: Cancel"
        self.display.text(instructions, 10, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

    def show(self):
        """Show text area dialog and get text"""
        from lib.keyboard import KEY_BACKSPACE, KEY_UP, KEY_DOWN

        while True:
            self.draw()
            key = self.keyboard.wait_key(timeout=500)

            if key == KEY_ENTER:
                return self.text
            elif key == KEY_ESC:
                return None
            elif key == KEY_BACKSPACE:
                if self.text and self.cursor_pos > 0:
                    self.text = self.text[:self.cursor_pos-1] + self.text[self.cursor_pos:]
                    self.cursor_pos -= 1
            elif key == KEY_UP:
                # Scroll up
                if self.scroll_offset > 0:
                    self.scroll_offset -= 1
            elif key == KEY_DOWN:
                # Scroll down
                lines = self._wrap_text(self.text) if self.text else [""]
                if self.scroll_offset < len(lines) - self.visible_lines:
                    self.scroll_offset += 1
            elif key and 32 <= key <= 126 and len(self.text) < self.max_length:
                self.text = self.text[:self.cursor_pos] + chr(key) + self.text[self.cursor_pos:]
                self.cursor_pos += 1

            time.sleep_ms(50)


class ListView:
    """List view widget for displaying items"""

    def __init__(self, display, keyboard, title="List", items=None):
        """Initialize list view"""
        self.display = display
        self.keyboard = keyboard
        self.title = title
        self.items = items or []
        self.selected = 0
        self.scroll_offset = 0
        self.max_visible = 12

    def draw(self):
        """Draw list view"""
        self.display.clear()

        # Draw title bar
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(self.title, 10, 10, self.display.WHITE)

        if not self.items:
            self.display.text("No items", 10, 50, self.display.GRAY)
            self.display.show()
            return

        # Draw list items
        y = 40
        visible_start = self.scroll_offset
        visible_end = min(visible_start + self.max_visible, len(self.items))

        for i in range(visible_start, visible_end):
            item = self.items[i]
            if i == self.selected:
                self.display.rect(5, y - 2, self.display.WIDTH - 10, 20,
                                self.display.BLUE, fill=True)

            # Truncate long items
            item_text = str(item)
            if len(item_text) > 38:
                item_text = item_text[:35] + "..."

            self.display.text(item_text, 10, y, self.display.WHITE)
            y += 22

        # Draw scrollbar if needed
        if len(self.items) > self.max_visible:
            bar_height = (self.max_visible * 260) // len(self.items)
            bar_y = 40 + (self.scroll_offset * 260) // len(self.items)
            self.display.rect(self.display.WIDTH - 10, bar_y, 5, bar_height,
                            self.display.GRAY, fill=True)

        self.display.show()

    def show(self):
        """Show list view and handle input"""
        while True:
            self.draw()
            key = self.keyboard.wait_key(timeout=5000)

            if key == KEY_UP:
                if self.selected > 0:
                    self.selected -= 1
                    if self.selected < self.scroll_offset:
                        self.scroll_offset = self.selected
            elif key == KEY_DOWN:
                if self.selected < len(self.items) - 1:
                    self.selected += 1
                    if self.selected >= self.scroll_offset + self.max_visible:
                        self.scroll_offset = self.selected - self.max_visible + 1
            elif key == KEY_ENTER:
                if self.items:
                    return self.selected
            elif key == KEY_ESC:
                return None

            time.sleep_ms(50)
