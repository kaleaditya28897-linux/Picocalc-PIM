"""
Display wrapper for Picocalc
Wraps the built-in picocalc.display object
"""

import picocalc


class Display:
    """Display wrapper around picocalc.display"""

    # RGB565 colors
    BLACK = 0x0000
    WHITE = 0xFFFF
    RED = 0xF800
    GREEN = 0x07E0
    BLUE = 0x001F
    YELLOW = 0xFFE0
    CYAN = 0x07FF
    MAGENTA = 0xF81F
    GRAY = 0x8410

    def __init__(self):
        """Initialize display using picocalc.display"""
        self._d = picocalc.display
        self.WIDTH = self._d.width
        self.HEIGHT = self._d.height
        self.clear()

    def clear(self, color=BLACK):
        """Clear display with color"""
        self._d.fill(color)

    def pixel(self, x, y, color):
        """Set pixel at (x, y) to color"""
        self._d.pixel(x, y, color)

    def line(self, x1, y1, x2, y2, color):
        """Draw line from (x1, y1) to (x2, y2)"""
        self._d.line(x1, y1, x2, y2, color)

    def rect(self, x, y, w, h, color, fill=False):
        """Draw rectangle"""
        if fill:
            self._d.fill_rect(x, y, w, h, color)
        else:
            self._d.rect(x, y, w, h, color)

    def text(self, text, x, y, color=WHITE, size=1):
        """Draw text at (x, y)"""
        if size == 1:
            self._d.text(text, x, y, color)
        else:
            for i, char in enumerate(text):
                self._draw_char_scaled(char, x + i * 8 * size, y, color, size)

    def _draw_char_scaled(self, char, x, y, color, scale):
        """Draw scaled character using temporary small framebuffer"""
        import framebuf
        buf = bytearray(8 * 8 * 2)
        tmp = framebuf.FrameBuffer(buf, 8, 8, framebuf.RGB565)
        tmp.text(char, 0, 0, color)
        for dy in range(8):
            for dx in range(8):
                if tmp.pixel(dx, dy) == color:
                    self._d.fill_rect(
                        x + dx * scale, y + dy * scale,
                        scale, scale, color
                    )

    def show(self):
        """Update display"""
        self._d.show()

    def blit(self, fbuf, x, y, key=-1):
        """Blit framebuffer to display"""
        self._d.blit(fbuf, x, y, key)

    def scroll(self, dx, dy):
        """Scroll display content"""
        self._d.scroll(dx, dy)

    def hline(self, x, y, w, color):
        """Draw horizontal line"""
        self._d.hline(x, y, w, color)

    def vline(self, x, y, h, color):
        """Draw vertical line"""
        self._d.vline(x, y, h, color)
