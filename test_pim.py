"""
Test suite for Picocalc PIM
Tests logic without requiring actual hardware
"""

import sys
import os
import json
import time

# Mock machine module for testing
class MockPin:
    OUT = 1
    IN = 0

    def __init__(self, pin, mode=None):
        self.pin = pin
        self.mode = mode
        self._value = 0

    def value(self, val=None):
        if val is not None:
            self._value = val
        return self._value

class MockSPI:
    def __init__(self, id, baudrate=1000000, polarity=0, phase=0,
                 sck=None, mosi=None, miso=None):
        self.id = id

    def write(self, data):
        pass

class MockI2C:
    def __init__(self, id, scl=None, sda=None, freq=400000):
        self.id = id

    def scan(self):
        return []

    def readfrom(self, addr, nbytes):
        return bytearray(nbytes)

class MockMachine:
    Pin = MockPin
    SPI = MockSPI
    I2C = MockI2C

# Mock framebuf module
class MockFrameBuffer:
    RGB565 = 1

    def __init__(self, buffer, width, height, format):
        self.buffer = buffer
        self.width = width
        self.height = height

    def fill(self, color):
        pass

    def pixel(self, x, y, color=None):
        pass

    def line(self, x1, y1, x2, y2, color):
        pass

    def rect(self, x, y, w, h, color):
        pass

    def fill_rect(self, x, y, w, h, color):
        pass

    def text(self, text, x, y, color):
        pass

    def blit(self, fbuf, x, y, key=-1):
        pass

    def scroll(self, dx, dy):
        pass

class MockFramebuf:
    FrameBuffer = MockFrameBuffer
    RGB565 = 1

# Install mocks
sys.modules['machine'] = MockMachine()
sys.modules['framebuf'] = MockFramebuf()

# Now we can import our modules
from apps.calendar_app import CalendarApp
from apps.appointments import Appointment, AppointmentsApp
from apps.todos import TodoItem, TodosApp
from apps.notes import Note, NotesApp
from apps.journal import JournalEntry, JournalApp
from games.snake import SnakeGame
from games.tetris import TetrisGame

# Test results
test_results = []

def test(name, func):
    """Run a test function"""
    try:
        func()
        test_results.append((name, "PASS", None))
        print(f"✓ {name}")
        return True
    except Exception as e:
        test_results.append((name, "FAIL", str(e)))
        print(f"✗ {name}: {e}")
        return False

# Calendar Tests
def test_calendar_days_in_month():
    """Test days in month calculation"""
    from apps.calendar_app import CalendarApp
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    cal = CalendarApp(display, keyboard)

    assert cal.days_in_month(2024, 1) == 31, "January should have 31 days"
    assert cal.days_in_month(2024, 2) == 29, "2024 Feb should have 29 days (leap year)"
    assert cal.days_in_month(2023, 2) == 28, "2023 Feb should have 28 days"
    assert cal.days_in_month(2024, 4) == 30, "April should have 30 days"

def test_calendar_first_day():
    """Test first day of month calculation"""
    from apps.calendar_app import CalendarApp
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    cal = CalendarApp(display, keyboard)

    # Test known dates
    day = cal.first_day_of_month(2024, 1)  # January 1, 2024
    assert 0 <= day <= 6, "Day should be 0-6"

# Appointment Tests
def test_appointment_creation():
    """Test appointment creation"""
    appt = Appointment((2024, 1, 15), "14:30", "Meeting", "Discuss project")

    assert appt.date == (2024, 1, 15)
    assert appt.time == "14:30"
    assert appt.title == "Meeting"
    assert appt.description == "Discuss project"
    assert appt.id is not None

def test_appointment_serialization():
    """Test appointment to/from dict"""
    appt = Appointment((2024, 1, 15), "14:30", "Meeting", "Discuss project")
    data = appt.to_dict()

    appt2 = Appointment.from_dict(data)
    assert appt2.date == appt.date
    assert appt2.time == appt.time
    assert appt2.title == appt.title
    assert appt2.id == appt.id

# Todo Tests
def test_todo_creation():
    """Test todo item creation"""
    todo = TodoItem("Test task", priority=TodoItem.PRIORITY_HIGH)

    assert todo.title == "Test task"
    assert todo.priority == TodoItem.PRIORITY_HIGH
    assert todo.completed == False
    assert todo.id is not None

def test_todo_priority():
    """Test todo priority levels"""
    assert TodoItem.PRIORITY_LOW == 0
    assert TodoItem.PRIORITY_NORMAL == 1
    assert TodoItem.PRIORITY_HIGH == 2

def test_todo_serialization():
    """Test todo to/from dict"""
    todo = TodoItem("Test", priority=TodoItem.PRIORITY_HIGH)
    data = todo.to_dict()

    todo2 = TodoItem.from_dict(data)
    assert todo2.title == todo.title
    assert todo2.priority == todo.priority
    assert todo2.id == todo.id

# Note Tests
def test_note_creation():
    """Test note creation"""
    note = Note("Test Note", "This is content")

    assert note.title == "Test Note"
    assert note.content == "This is content"
    assert note.id is not None
    assert note.created is not None

def test_note_serialization():
    """Test note to/from dict"""
    note = Note("Test", "Content")
    data = note.to_dict()

    note2 = Note.from_dict(data)
    assert note2.title == note.title
    assert note2.content == note.content
    assert note2.id == note.id

# Journal Tests
def test_journal_creation():
    """Test journal entry creation"""
    entry = JournalEntry((2024, 1, 15), "Today was good", mood='good')

    assert entry.date == (2024, 1, 15)
    assert entry.content == "Today was good"
    assert entry.mood == 'good'
    assert entry.id is not None

def test_journal_moods():
    """Test journal mood options"""
    assert 'great' in JournalEntry.MOODS
    assert 'good' in JournalEntry.MOODS
    assert 'okay' in JournalEntry.MOODS
    assert 'bad' in JournalEntry.MOODS
    assert 'terrible' in JournalEntry.MOODS

def test_journal_serialization():
    """Test journal to/from dict"""
    entry = JournalEntry((2024, 1, 15), "Content", mood='great')
    data = entry.to_dict()

    entry2 = JournalEntry.from_dict(data)
    assert entry2.date == entry.date
    assert entry2.content == entry.content
    assert entry2.mood == entry.mood
    assert entry2.id == entry.id

# Snake Game Tests
def test_snake_initialization():
    """Test snake game initialization"""
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    game = SnakeGame(display, keyboard)

    assert game.grid_size > 0
    assert game.grid_width > 0
    assert game.grid_height > 0

def test_snake_reset():
    """Test snake game reset"""
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    game = SnakeGame(display, keyboard)
    game.reset_game()

    assert len(game.snake) == 3, "Snake should start with 3 segments"
    assert game.score == 0, "Score should be 0"
    assert game.game_over == False
    assert game.food is not None

# Tetris Game Tests
def test_tetris_initialization():
    """Test tetris game initialization"""
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    game = TetrisGame(display, keyboard)

    assert game.grid_width == 10
    assert game.grid_height == 20
    assert len(game.SHAPES) == 7  # 7 tetrominos

def test_tetris_reset():
    """Test tetris game reset"""
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    game = TetrisGame(display, keyboard)
    game.reset_game()

    assert game.score == 0
    assert game.level == 1
    assert game.game_over == False
    assert len(game.grid) == game.grid_height

def test_tetris_shapes():
    """Test tetris shapes are valid"""
    from lib.display import Display
    from lib.keyboard import Keyboard

    display = Display()
    keyboard = Keyboard()
    game = TetrisGame(display, keyboard)

    for shape in game.SHAPES:
        assert len(shape) > 0, "Shape must have rows"
        assert len(shape[0]) > 0, "Shape must have columns"

# Data Persistence Tests
def test_data_directory():
    """Test data directory handling"""
    try:
        os.makedirs('data', exist_ok=True)
        assert os.path.isdir('data')
    except:
        pass  # Might already exist

def test_json_persistence():
    """Test JSON file operations"""
    test_data = {'test': 'value', 'number': 123}

    # Write
    with open('data/test.json', 'w') as f:
        json.dump(test_data, f)

    # Read
    with open('data/test.json', 'r') as f:
        loaded = json.load(f)

    assert loaded == test_data

    # Cleanup
    try:
        os.remove('data/test.json')
    except:
        pass

# Run all tests
def run_all_tests():
    """Run all tests"""
    print("=" * 60)
    print("PICOCALC PIM TEST SUITE")
    print("=" * 60)
    print()

    print("CALENDAR TESTS")
    print("-" * 60)
    test("Days in month calculation", test_calendar_days_in_month)
    test("First day of month calculation", test_calendar_first_day)
    print()

    print("APPOINTMENT TESTS")
    print("-" * 60)
    test("Appointment creation", test_appointment_creation)
    test("Appointment serialization", test_appointment_serialization)
    print()

    print("TODO TESTS")
    print("-" * 60)
    test("Todo creation", test_todo_creation)
    test("Todo priority levels", test_todo_priority)
    test("Todo serialization", test_todo_serialization)
    print()

    print("NOTE TESTS")
    print("-" * 60)
    test("Note creation", test_note_creation)
    test("Note serialization", test_note_serialization)
    print()

    print("JOURNAL TESTS")
    print("-" * 60)
    test("Journal creation", test_journal_creation)
    test("Journal moods", test_journal_moods)
    test("Journal serialization", test_journal_serialization)
    print()

    print("SNAKE GAME TESTS")
    print("-" * 60)
    test("Snake initialization", test_snake_initialization)
    test("Snake reset", test_snake_reset)
    print()

    print("TETRIS GAME TESTS")
    print("-" * 60)
    test("Tetris initialization", test_tetris_initialization)
    test("Tetris reset", test_tetris_reset)
    test("Tetris shapes", test_tetris_shapes)
    print()

    print("DATA PERSISTENCE TESTS")
    print("-" * 60)
    test("Data directory", test_data_directory)
    test("JSON persistence", test_json_persistence)
    print()

    # Summary
    print("=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    passed = sum(1 for _, result, _ in test_results if result == "PASS")
    failed = sum(1 for _, result, _ in test_results if result == "FAIL")
    total = len(test_results)

    print(f"Total: {total}")
    print(f"Passed: {passed} ({100*passed//total}%)")
    print(f"Failed: {failed}")

    if failed > 0:
        print("\nFailed tests:")
        for name, result, error in test_results:
            if result == "FAIL":
                print(f"  - {name}: {error}")

    print()
    return failed == 0

if __name__ == '__main__':
    success = run_all_tests()
    sys.exit(0 if success else 1)
