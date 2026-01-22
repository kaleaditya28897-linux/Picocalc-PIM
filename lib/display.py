"""
Display driver wrapper for Picocalc
Supports ST7365P display controller (320x320)
"""

import machine
import framebuf
import time


class Display:
    """Display driver for Picocalc 320x320 IPS display"""

    # Display dimensions
    WIDTH = 320
    HEIGHT = 320

    # Colors (RGB565 format)
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
        """Initialize the display"""
        self._init_hardware()
        self.buffer = bytearray(self.WIDTH * self.HEIGHT * 2)  # RGB565
        self.fb = framebuf.FrameBuffer(
            self.buffer,
            self.WIDTH,
            self.HEIGHT,
            framebuf.RGB565
        )
        self.clear()

    def _init_hardware(self):
        """Initialize SPI and display hardware"""
        try:
            # SPI setup for display
            # Note: Adjust pins according to Picocalc schematic
            self.spi = machine.SPI(
                0,
                baudrate=40000000,
                polarity=0,
                phase=0,
                sck=machine.Pin(18),
                mosi=machine.Pin(19),
                miso=machine.Pin(16)
            )

            # Control pins
            self.dc = machine.Pin(20, machine.Pin.OUT)  # Data/Command
            self.cs = machine.Pin(17, machine.Pin.OUT)  # Chip Select
            self.rst = machine.Pin(21, machine.Pin.OUT)  # Reset

            # Reset display
            self._reset()
            self._init_display()

        except Exception as e:
            print(f"Display init warning: {e}")
            # Fallback to simulation mode for testing
            self.spi = None

    def _reset(self):
        """Hardware reset display"""
        if self.rst:
            self.rst.value(1)
            time.sleep_ms(5)
            self.rst.value(0)
            time.sleep_ms(20)
            self.rst.value(1)
            time.sleep_ms(150)

    def _write_cmd(self, cmd):
        """Write command to display"""
        if self.spi:
            self.dc.value(0)  # Command mode
            self.cs.value(0)
            self.spi.write(bytearray([cmd]))
            self.cs.value(1)

    def _write_data(self, data):
        """Write data to display"""
        if self.spi:
            self.dc.value(1)  # Data mode
            self.cs.value(0)
            if isinstance(data, int):
                self.spi.write(bytearray([data]))
            else:
                self.spi.write(data)
            self.cs.value(1)

    def _init_display(self):
        """Initialize display controller"""
        # Basic initialization sequence for ST7365P/ST7789-compatible displays
        commands = [
            (0x01, None, 150),      # Software reset
            (0x11, None, 500),      # Sleep out
            (0x3A, [0x55], 10),     # Interface pixel format (RGB565)
            (0x36, [0x00], 0),      # Memory data access control
            (0x29, None, 100),      # Display on
        ]

        for cmd_data in commands:
            cmd = cmd_data[0]
            data = cmd_data[1]
            delay = cmd_data[2] if len(cmd_data) > 2 else 0

            self._write_cmd(cmd)
            if data:
                for byte in data:
                    self._write_data(byte)
            if delay:
                time.sleep_ms(delay)

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
        if self.spi:
            # Set address window to full screen
            self._write_cmd(0x2A)  # Column address set
            self._write_data(0x00)
            self._write_data(0x00)
            self._write_data((self.WIDTH - 1) >> 8)
            self._write_data((self.WIDTH - 1) & 0xFF)

            self._write_cmd(0x2B)  # Row address set
            self._write_data(0x00)
            self._write_data(0x00)
            self._write_data((self.HEIGHT - 1) >> 8)
            self._write_data((self.HEIGHT - 1) & 0xFF)

            self._write_cmd(0x2C)  # Memory write
            self._write_data(self.buffer)

    def blit(self, fbuf, x, y, key=-1):
        """Blit framebuffer to display"""
        self.fb.blit(fbuf, x, y, key)

    def scroll(self, dx, dy):
        """Scroll display content"""
        self.fb.scroll(dx, dy)
