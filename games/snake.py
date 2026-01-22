"""
Snake game for Picocalc PIM
Classic snake game with score tracking
"""

import time
import random
from lib.keyboard import KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESC
from lib.ui import MessageBox


class SnakeGame:
    """Classic Snake game"""

    def __init__(self, display, keyboard):
        """Initialize Snake game"""
        self.display = display
        self.keyboard = keyboard

        # Game settings
        self.grid_size = 16  # Size of each grid cell
        self.grid_width = self.display.WIDTH // self.grid_size
        self.grid_height = (self.display.HEIGHT - 40) // self.grid_size

        # Game state
        self.snake = []
        self.direction = (1, 0)  # (dx, dy)
        self.food = None
        self.score = 0
        self.game_over = False
        self.speed = 200  # milliseconds per frame

    def reset_game(self):
        """Reset game state"""
        # Initialize snake in the middle
        mid_x = self.grid_width // 2
        mid_y = self.grid_height // 2
        self.snake = [
            (mid_x, mid_y),
            (mid_x - 1, mid_y),
            (mid_x - 2, mid_y)
        ]
        self.direction = (1, 0)
        self.score = 0
        self.game_over = False
        self.spawn_food()

    def spawn_food(self):
        """Spawn food at random location"""
        while True:
            x = random.randint(0, self.grid_width - 1)
            y = random.randint(0, self.grid_height - 1)
            if (x, y) not in self.snake:
                self.food = (x, y)
                break

    def update(self):
        """Update game state"""
        if self.game_over:
            return

        # Calculate new head position
        head_x, head_y = self.snake[0]
        dx, dy = self.direction
        new_head = (head_x + dx, head_y + dy)

        # Check collision with walls
        if (new_head[0] < 0 or new_head[0] >= self.grid_width or
            new_head[1] < 0 or new_head[1] >= self.grid_height):
            self.game_over = True
            return

        # Check collision with self
        if new_head in self.snake:
            self.game_over = True
            return

        # Add new head
        self.snake.insert(0, new_head)

        # Check if food eaten
        if new_head == self.food:
            self.score += 10
            self.spawn_food()
            # Speed up slightly
            self.speed = max(50, self.speed - 5)
        else:
            # Remove tail
            self.snake.pop()

    def handle_input(self):
        """Handle keyboard input"""
        key = self.keyboard.read_key()

        if key == KEY_UP and self.direction != (0, 1):
            self.direction = (0, -1)
        elif key == KEY_DOWN and self.direction != (0, -1):
            self.direction = (0, 1)
        elif key == KEY_LEFT and self.direction != (1, 0):
            self.direction = (-1, 0)
        elif key == KEY_RIGHT and self.direction != (-1, 0):
            self.direction = (1, 0)
        elif key == KEY_ESC:
            return False

        return True

    def draw(self):
        """Draw game state"""
        self.display.clear()

        # Draw score bar
        self.display.rect(0, 0, self.display.WIDTH, 30,
                         self.display.BLUE, fill=True)
        score_text = f"Score: {self.score}"
        self.display.text(score_text, 10, 10, self.display.WHITE)

        # Calculate game area offset
        game_y_offset = 35

        # Draw game border
        border_width = self.grid_width * self.grid_size
        border_height = self.grid_height * self.grid_size
        self.display.rect(0, game_y_offset, border_width, border_height,
                         self.display.WHITE)

        # Draw snake
        for i, (x, y) in enumerate(self.snake):
            px = x * self.grid_size + 2
            py = y * self.grid_size + game_y_offset + 2
            size = self.grid_size - 4

            # Head is brighter
            color = self.display.GREEN if i == 0 else self.display.CYAN
            self.display.rect(px, py, size, size, color, fill=True)

        # Draw food
        if self.food:
            fx, fy = self.food
            px = fx * self.grid_size + 2
            py = fy * self.grid_size + game_y_offset + 2
            size = self.grid_size - 4
            self.display.rect(px, py, size, size, self.display.RED, fill=True)

        # Draw game over message
        if self.game_over:
            msg_x = (self.display.WIDTH - 80) // 2
            msg_y = self.display.HEIGHT // 2
            self.display.rect(msg_x - 10, msg_y - 10, 100, 40,
                            self.display.BLACK, fill=True)
            self.display.rect(msg_x - 10, msg_y - 10, 100, 40,
                            self.display.RED)
            self.display.text("GAME OVER", msg_x, msg_y, self.display.RED)
            self.display.text("Press ENTER", msg_x - 10, msg_y + 15,
                            self.display.WHITE)

        self.display.show()

    def run(self):
        """Run Snake game"""
        # Show instructions
        msg = MessageBox(self.display, self.keyboard,
                        title="Snake Game",
                        message=(
                            "Use arrow keys to move\n"
                            "Eat food to grow\n"
                            "Don't hit walls or yourself!\n\n"
                            "Press any key to start"
                        ))
        msg.show()

        self.reset_game()
        last_update = time.ticks_ms()

        from lib.keyboard import KEY_ENTER

        while True:
            # Handle input
            if not self.handle_input():
                return

            # Update game
            current_time = time.ticks_ms()
            if time.ticks_diff(current_time, last_update) >= self.speed:
                if not self.game_over:
                    self.update()
                last_update = current_time

            # Draw
            self.draw()

            # Check for restart
            if self.game_over:
                key = self.keyboard.read_key()
                if key == KEY_ENTER:
                    self.reset_game()
                    last_update = time.ticks_ms()
                elif key == KEY_ESC:
                    return

            time.sleep_ms(10)
