"""
Picocalc Personal Information Manager (PIM)
Main entry point for the PIM application

Features:
- Calendar
- Appointments
- To-do Lists
- Notes
- Journal
- Games (Snake, Tetris)
"""

import time
import machine
from lib.display import Display
from lib.keyboard import Keyboard
from lib.ui import Menu, MessageBox
from apps.calendar_app import CalendarApp
from apps.appointments import AppointmentsApp
from apps.todos import TodosApp
from apps.notes import NotesApp
from apps.journal import JournalApp
from games.snake import SnakeGame
from games.tetris import TetrisGame


class PIM:
    """Main PIM Application"""

    def __init__(self):
        """Initialize the PIM application"""
        self.display = Display()
        self.keyboard = Keyboard()

        # Initialize data directory
        self._init_data_dir()

        # Initialize applications
        self.apps = {
            'calendar': CalendarApp(self.display, self.keyboard),
            'appointments': AppointmentsApp(self.display, self.keyboard),
            'todos': TodosApp(self.display, self.keyboard),
            'notes': NotesApp(self.display, self.keyboard),
            'journal': JournalApp(self.display, self.keyboard),
            'snake': SnakeGame(self.display, self.keyboard),
            'tetris': TetrisGame(self.display, self.keyboard),
        }

        # Create main menu
        self.main_menu = Menu(
            self.display,
            self.keyboard,
            title="PIM Main Menu",
            items=[
                ("Calendar", self._run_calendar),
                ("Appointments", self._run_appointments),
                ("To-Do List", self._run_todos),
                ("Notes", self._run_notes),
                ("Journal", self._run_journal),
                ("Snake Game", self._run_snake),
                ("Tetris Game", self._run_tetris),
                ("About", self._show_about),
                ("Exit", self._exit)
            ]
        )

        self.running = True

    def _init_data_dir(self):
        """Create data directory if it doesn't exist"""
        try:
            import os
            try:
                os.mkdir('data')
            except OSError:
                pass  # Directory already exists
        except ImportError:
            pass  # os module not available

    def _run_calendar(self):
        """Run calendar application"""
        self.apps['calendar'].run()

    def _run_appointments(self):
        """Run appointments application"""
        self.apps['appointments'].run()

    def _run_todos(self):
        """Run to-do list application"""
        self.apps['todos'].run()

    def _run_notes(self):
        """Run notes application"""
        self.apps['notes'].run()

    def _run_journal(self):
        """Run journal application"""
        self.apps['journal'].run()

    def _run_snake(self):
        """Run Snake game"""
        self.apps['snake'].run()

    def _run_tetris(self):
        """Run Tetris game"""
        self.apps['tetris'].run()

    def _show_about(self):
        """Show about dialog"""
        msg = MessageBox(
            self.display,
            self.keyboard,
            title="About PIM",
            message=(
                "Picocalc PIM v1.0\n\n"
                "Personal Information Manager\n"
                "for ClockworkPi Picocalc\n\n"
                "Features:\n"
                "- Calendar & Appointments\n"
                "- To-Do Lists\n"
                "- Notes & Journal\n"
                "- Games: Snake & Tetris\n\n"
                "MicroPython v1.27.0\n"
                "RP2350 (Pico2W)"
            )
        )
        msg.show()

    def _exit(self):
        """Exit application"""
        self.running = False

    def run(self):
        """Main application loop"""
        # Show splash screen
        self._show_splash()

        # Main loop
        while self.running:
            try:
                self.main_menu.show()
            except KeyboardInterrupt:
                self.running = False

        # Cleanup
        self.display.clear()
        self.display.text("Goodbye!", 120, 150, color=0xFFFF)
        self.display.show()
        time.sleep(1)

    def _show_splash(self):
        """Show splash screen"""
        self.display.clear()
        self.display.text("Picocalc PIM", 90, 100, color=0xFFFF, size=2)
        self.display.text("Loading...", 120, 140, color=0x7BEF)
        self.display.show()
        time.sleep(1)


def main():
    """Main entry point"""
    try:
        pim = PIM()
        pim.run()
    except Exception as e:
        print(f"Error: {e}")
        import sys
        sys.print_exception(e)


if __name__ == '__main__':
    main()
