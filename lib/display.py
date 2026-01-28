"""
Display wrapper for Picocalc
Uses existing Picocalc display modules when available
"""

import framebuf

# Try to import existing Picocalc display modules
try:
    import picocalcdisplay
    HAS_PICOCALC_DISPLAY = True
except ImportError:
    HAS_PICOCALC_DISPLAY = False
    try:
        import picocalc
        HAS_PICOCALC = True
    except ImportError:
        HAS_PICOCALC = False


class Display:
    """Display wrapper using available Picocalc modules"""

    # Display dimensions
    WIDTH = 320
    HEIGHT = 320

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
        """Initialize display using available modules"""
        # Create framebuffer
        self.buffer = bytearray(self.WIDTH * self.HEIGHT * 2)  # RGB565
        self.fb = framebuf.FrameBuffer(
            self.buffer,
            self.WIDTH,
            self.HEIGHT,
            framebuf.RGB565
        )

        # Detect and initialize hardware
        self.use_hardware = False

        if HAS_PICOCALC_DISPLAY:
            try:
                picocalcdisplay.init()
                self.use_hardware = True
                print("Using picocalcdisplay module")
            except Exception as e:
                print(f"picocalcdisplay init failed: {e}")

        elif HAS_PICOCALC:
            try:
                # Some picocalc modules auto-initialize
                self.use_hardware = True
                print("Using picocalc module")
            except Exception as e:
                print(f"picocalc init failed: {e}")

        if not self.use_hardware:
            print("Display: Using framebuffer mode (no hardware)")

        self.clear()

    def clear(self, color=BLACK):
        """Clear display with color"""
        self.fb.fill(color)

    def pixel(self, x, y, color):
        """Set pixel at (x, y) to color"""
        if 0 <= x < self.WIDTH and 0 <= y < self.HEIGHT:
            self.fb.pixel(x, y, color)

    def line(self, x1, y1, x2, y2, color):
        """Draw line from (x1, y1) to (x2, y2)"""
        self.fb.line(x1, y1, x2, y2, color)

    def rect(self, x, y, w, h, color, fill=False):
        """Draw rectangle"""
        if fill:
            self.fb.fill_rect(x, y, w, h, color)
        else:
            self.fb.rect(x, y, w, h, color)

    def text(self, text, x, y, color=WHITE, size=1):
        """Draw text at (x, y)"""
        if size == 1:
            self.fb.text(text, x, y, color)
        else:
            # Scale up text for larger sizes
            for i, char in enumerate(text):
                self._draw_char_scaled(char, x + i * 8 * size, y, color, size)

    def _draw_char_scaled(self, char, x, y, color, scale):
        """Draw scaled character"""
        # Simple scaling by drawing each pixel as a square
        for dy in range(8):
            for dx in range(8):
                # Check if pixel is set in original 8x8 font
                # This is a simplified version - actual implementation would
                # need to read from font data
                if ord(char) >= 32:  # Printable character
                    for sy in range(scale):
                        for sx in range(scale):
                            px = x + dx * scale + sx
                            py = y + dy * scale + sy
                            if 0 <= px < self.WIDTH and 0 <= py < self.HEIGHT:
                                self.fb.pixel(px, py, color)

    def show(self):
        """Update display with buffer contents"""
        if not self.use_hardware:
            return  # Simulation mode

        try:
            if HAS_PICOCALC_DISPLAY:
                # Use picocalcdisplay C module
                picocalcdisplay.show(self.buffer)
            elif HAS_PICOCALC:
                # Try picocalc module methods
                if hasattr(picocalc, 'display_buffer'):
                    picocalc.display_buffer(self.buffer)
                elif hasattr(picocalc, 'show'):
                    picocalc.show(self.buffer)
        except Exception as e:
            print(f"Display show error: {e}")

    def blit(self, fbuf, x, y, key=-1):
        """Blit framebuffer to display"""
        self.fb.blit(fbuf, x, y, key)

    def scroll(self, dx, dy):
        """Scroll display content"""
        self.fb.scroll(dx, dy)
