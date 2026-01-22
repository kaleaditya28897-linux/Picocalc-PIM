"""
Calendar application for Picocalc PIM
Displays monthly calendar with navigation
"""

import time
from lib.keyboard import KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ENTER, KEY_ESC


class CalendarApp:
    """Calendar viewer application"""

    MONTH_NAMES = [
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    ]

    DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

    def __init__(self, display, keyboard):
        """Initialize calendar app"""
        self.display = display
        self.keyboard = keyboard

        # Get current date
        now = time.localtime()
        self.year = now[0]
        self.month = now[1]
        self.day = now[2]

        self.view_year = self.year
        self.view_month = self.month

    def days_in_month(self, year, month):
        """Get number of days in month"""
        if month in [1, 3, 5, 7, 8, 10, 12]:
            return 31
        elif month in [4, 6, 9, 11]:
            return 30
        else:  # February
            # Leap year check
            if (year % 4 == 0 and year % 100 != 0) or (year % 400 == 0):
                return 29
            return 28

    def first_day_of_month(self, year, month):
        """Get day of week for first day of month (0=Monday, 6=Sunday)"""
        # Zeller's congruence algorithm
        if month < 3:
            month += 12
            year -= 1

        q = 1
        m = month
        y = year % 100
        c = year // 100

        h = (q + ((13 * (m + 1)) // 5) + y + (y // 4) + (c // 4) - (2 * c)) % 7
        # Convert: Zeller's uses 0=Saturday, we want 0=Monday
        day_of_week = (h + 5) % 7
        return day_of_week

    def draw_calendar(self):
        """Draw calendar for current view month"""
        self.display.clear()

        # Draw title
        title = f"{self.MONTH_NAMES[self.view_month - 1]} {self.view_year}"
        title_x = (self.display.WIDTH - len(title) * 8) // 2
        self.display.rect(0, 0, self.display.WIDTH, 30, self.display.BLUE, fill=True)
        self.display.text(title, title_x, 10, self.display.WHITE)

        # Draw day headers
        y = 40
        x = 10
        cell_width = 44
        for day_name in self.DAY_NAMES:
            self.display.text(day_name[:2], x, y, self.display.YELLOW)
            x += cell_width

        # Draw calendar grid
        y = 60
        first_day = self.first_day_of_month(self.view_year, self.view_month)
        days_in_month = self.days_in_month(self.view_year, self.view_month)

        day_num = 1
        for week in range(6):  # Maximum 6 weeks in a month
            x = 10
            for day_of_week in range(7):
                # Calculate if we should draw this day
                cell_index = week * 7 + day_of_week
                if cell_index >= first_day and day_num <= days_in_month:
                    # Check if this is today
                    is_today = (day_num == self.day and
                              self.view_month == self.month and
                              self.view_year == self.year)

                    # Draw day number
                    color = self.display.WHITE
                    if is_today:
                        # Highlight today
                        self.display.rect(x - 2, y - 2, 20, 18,
                                        self.display.GREEN, fill=True)
                        color = self.display.BLACK

                    day_str = str(day_num)
                    self.display.text(day_str, x, y, color)
                    day_num += 1

                x += cell_width

            y += 20
            if day_num > days_in_month:
                break

        # Draw instructions
        instructions = "Arrows: Navigate  ESC: Back"
        self.display.text(instructions, 30, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

    def run(self):
        """Run calendar application"""
        while True:
            self.draw_calendar()
            key = self.keyboard.wait_key(timeout=5000)

            if key == KEY_LEFT:
                # Previous month
                self.view_month -= 1
                if self.view_month < 1:
                    self.view_month = 12
                    self.view_year -= 1
            elif key == KEY_RIGHT:
                # Next month
                self.view_month += 1
                if self.view_month > 12:
                    self.view_month = 1
                    self.view_year += 1
            elif key == KEY_UP:
                # Previous year
                self.view_year -= 1
            elif key == KEY_DOWN:
                # Next year
                self.view_year += 1
            elif key == KEY_ENTER:
                # Reset to current month
                self.view_year = self.year
                self.view_month = self.month
            elif key == KEY_ESC:
                return

            time.sleep_ms(50)
