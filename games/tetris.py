"""
Tetris game for Picocalc PIM
Classic Tetris with score and level tracking
"""

import time
import random
from lib.keyboard import KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESC
from lib.ui import MessageBox


class TetrisGame:
    """Classic Tetris game"""

    # Tetromino shapes (I, O, T, S, Z, J, L)
    SHAPES = [
        [[1, 1, 1, 1]],  # I
        [[1, 1], [1, 1]],  # O
        [[0, 1, 0], [1, 1, 1]],  # T
        [[0, 1, 1], [1, 1, 0]],  # S
        [[1, 1, 0], [0, 1, 1]],  # Z
        [[1, 0, 0], [1, 1, 1]],  # J
        [[0, 0, 1], [1, 1, 1]]  # L
    ]

    COLORS = [
        0x00FFFF,  # Cyan (I)
        0xFFFF00,  # Yellow (O)
        0xFF00FF,  # Purple (T)
        0x00FF00,  # Green (S)
        0xFF0000,  # Red (Z)
        0x0000FF,  # Blue (J)
        0xFF8000   # Orange (L)
    ]

    def __init__(self, display, keyboard):
        """Initialize Tetris game"""
        self.display = display
        self.keyboard = keyboard

        # Game settings
        self.block_size = 12
        self.grid_width = 10
        self.grid_height = 20
        self.game_x_offset = 100
        self.game_y_offset = 35

        # Game state
        self.grid = []
        self.current_piece = None
        self.current_x = 0
        self.current_y = 0
        self.current_shape = 0
        self.score = 0
        self.level = 1
        self.lines_cleared = 0
        self.game_over = False
        self.drop_speed = 800  # milliseconds

    def reset_game(self):
        """Reset game state"""
        self.grid = [[0 for _ in range(self.grid_width)]
                    for _ in range(self.grid_height)]
        self.score = 0
        self.level = 1
        self.lines_cleared = 0
        self.game_over = False
        self.drop_speed = 800
        self.spawn_piece()

    def spawn_piece(self):
        """Spawn new piece"""
        self.current_shape = random.randint(0, len(self.SHAPES) - 1)
        self.current_piece = [row[:] for row in self.SHAPES[self.current_shape]]
        self.current_x = self.grid_width // 2 - len(self.current_piece[0]) // 2
        self.current_y = 0

        # Check if game over
        if self.check_collision():
            self.game_over = True

    def rotate_piece(self):
        """Rotate current piece"""
        if not self.current_piece:
            return

        # Transpose and reverse rows
        rotated = list(zip(*self.current_piece[::-1]))
        rotated = [list(row) for row in rotated]

        # Check if rotation is valid
        old_piece = self.current_piece
        self.current_piece = rotated

        if self.check_collision():
            # Rotation invalid, revert
            self.current_piece = old_piece
        else:
            # Adjust position if needed
            if self.current_x + len(self.current_piece[0]) > self.grid_width:
                self.current_x = self.grid_width - len(self.current_piece[0])

    def check_collision(self):
        """Check if current piece collides with grid or boundaries"""
        if not self.current_piece:
            return False

        for y, row in enumerate(self.current_piece):
            for x, cell in enumerate(row):
                if cell:
                    new_x = self.current_x + x
                    new_y = self.current_y + y

                    # Check boundaries
                    if (new_x < 0 or new_x >= self.grid_width or
                        new_y >= self.grid_height):
                        return True

                    # Check grid collision
                    if new_y >= 0 and self.grid[new_y][new_x]:
                        return True

        return False

    def merge_piece(self):
        """Merge current piece into grid"""
        if not self.current_piece:
            return

        color_idx = self.current_shape + 1
        for y, row in enumerate(self.current_piece):
            for x, cell in enumerate(row):
                if cell:
                    grid_y = self.current_y + y
                    grid_x = self.current_x + x
                    if 0 <= grid_y < self.grid_height:
                        self.grid[grid_y][grid_x] = color_idx

    def clear_lines(self):
        """Clear completed lines"""
        lines_to_clear = []

        for y in range(self.grid_height):
            if all(self.grid[y]):
                lines_to_clear.append(y)

        if lines_to_clear:
            # Remove cleared lines
            for y in lines_to_clear:
                del self.grid[y]
                self.grid.insert(0, [0] * self.grid_width)

            # Update score
            num_lines = len(lines_to_clear)
            points = [0, 100, 300, 500, 800][min(num_lines, 4)]
            self.score += points * self.level
            self.lines_cleared += num_lines

            # Update level
            self.level = 1 + self.lines_cleared // 10
            self.drop_speed = max(100, 800 - (self.level - 1) * 50)

    def move_down(self):
        """Move piece down"""
        self.current_y += 1
        if self.check_collision():
            self.current_y -= 1
            self.merge_piece()
            self.clear_lines()
            self.spawn_piece()
            return False
        return True

    def move_horizontal(self, dx):
        """Move piece horizontally"""
        self.current_x += dx
        if self.check_collision():
            self.current_x -= dx

    def drop_piece(self):
        """Drop piece to bottom"""
        while self.move_down():
            self.score += 2

    def handle_input(self):
        """Handle keyboard input"""
        key = self.keyboard.read_key()

        if key == KEY_LEFT:
            self.move_horizontal(-1)
        elif key == KEY_RIGHT:
            self.move_horizontal(1)
        elif key == KEY_DOWN:
            if self.move_down():
                self.score += 1
        elif key == KEY_UP:
            self.rotate_piece()
        elif key == KEY_ESC:
            return False

        return True

    def draw(self):
        """Draw game state"""
        self.display.clear()

        # Draw score bar
        self.display.rect(0, 0, self.display.WIDTH, 30,
                         self.display.BLUE, fill=True)
        score_text = f"Score:{self.score} Lvl:{self.level}"
        self.display.text(score_text, 60, 10, self.display.WHITE)

        # Draw game border
        border_x = self.game_x_offset - 2
        border_y = self.game_y_offset - 2
        border_w = self.grid_width * self.block_size + 4
        border_h = self.grid_height * self.block_size + 4
        self.display.rect(border_x, border_y, border_w, border_h,
                         self.display.WHITE)

        # Draw grid
        for y in range(self.grid_height):
            for x in range(self.grid_width):
                if self.grid[y][x]:
                    px = self.game_x_offset + x * self.block_size
                    py = self.game_y_offset + y * self.block_size
                    color_idx = self.grid[y][x] - 1
                    color = self.COLORS[color_idx] if 0 <= color_idx < len(self.COLORS) else self.display.WHITE
                    self.display.rect(px, py, self.block_size - 1,
                                    self.block_size - 1, color, fill=True)

        # Draw current piece
        if self.current_piece and not self.game_over:
            color = self.COLORS[self.current_shape]
            for y, row in enumerate(self.current_piece):
                for x, cell in enumerate(row):
                    if cell:
                        px = self.game_x_offset + (self.current_x + x) * self.block_size
                        py = self.game_y_offset + (self.current_y + y) * self.block_size
                        if py >= self.game_y_offset:
                            self.display.rect(px, py, self.block_size - 1,
                                            self.block_size - 1, color, fill=True)

        # Draw game over message
        if self.game_over:
            msg_x = 80
            msg_y = self.display.HEIGHT // 2
            self.display.rect(msg_x - 10, msg_y - 10, 140, 40,
                            self.display.BLACK, fill=True)
            self.display.rect(msg_x - 10, msg_y - 10, 140, 40,
                            self.display.RED)
            self.display.text("GAME OVER", msg_x, msg_y, self.display.RED)
            self.display.text("ENTER: Restart", msg_x - 10, msg_y + 15,
                            self.display.WHITE)

        # Draw controls hint
        if not self.game_over:
            self.display.text("Arrows:Move", 10, 280, self.display.GRAY)
            self.display.text("UP:Rotate", 10, 295, self.display.GRAY)

        self.display.show()

    def run(self):
        """Run Tetris game"""
        # Show instructions
        msg = MessageBox(self.display, self.keyboard,
                        title="Tetris",
                        message=(
                            "Arrows: Move/Rotate\n"
                            "Down: Soft drop\n"
                            "Complete lines to score\n\n"
                            "Press any key to start"
                        ))
        msg.show()

        self.reset_game()
        last_drop = time.ticks_ms()

        from lib.keyboard import KEY_ENTER

        while True:
            # Handle input
            if not self.handle_input():
                return

            # Auto drop
            current_time = time.ticks_ms()
            if not self.game_over and time.ticks_diff(current_time, last_drop) >= self.drop_speed:
                self.move_down()
                last_drop = current_time

            # Draw
            self.draw()

            # Check for restart
            if self.game_over:
                key = self.keyboard.read_key()
                if key == KEY_ENTER:
                    self.reset_game()
                    last_drop = time.ticks_ms()
                elif key == KEY_ESC:
                    return

            time.sleep_ms(10)
