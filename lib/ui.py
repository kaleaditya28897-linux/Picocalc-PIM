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
        self.display.rect(10, box_y, self.display.WIDTH - 20, 30, self.display.WHITE)
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
