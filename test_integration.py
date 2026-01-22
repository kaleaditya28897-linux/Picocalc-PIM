"""
Integration tests for Picocalc PIM
Tests realistic usage scenarios
"""

import sys
import os
import json
import time

# Setup mocks
class MockPin:
    OUT = 1
    IN = 0
    def __init__(self, pin, mode=None):
        self.pin = pin
        self._value = 0
    def value(self, val=None):
        if val is not None:
            self._value = val
        return self._value

class MockSPI:
    def __init__(self, id, **kwargs):
        pass
    def write(self, data):
        pass

class MockI2C:
    def __init__(self, id, **kwargs):
        pass
    def scan(self):
        return []
    def readfrom(self, addr, nbytes):
        return bytearray(nbytes)

class MockMachine:
    Pin = MockPin
    SPI = MockSPI
    I2C = MockI2C

class MockFrameBuffer:
    RGB565 = 1
    def __init__(self, buffer, width, height, format):
        pass
    def fill(self, color): pass
    def pixel(self, x, y, color=None): pass
    def line(self, x1, y1, x2, y2, color): pass
    def rect(self, x, y, w, h, color): pass
    def fill_rect(self, x, y, w, h, color): pass
    def text(self, text, x, y, color): pass
    def blit(self, fbuf, x, y, key=-1): pass
    def scroll(self, dx, dy): pass

class MockFramebuf:
    FrameBuffer = MockFrameBuffer
    RGB565 = 1

sys.modules['machine'] = MockMachine()
sys.modules['framebuf'] = MockFramebuf()

# Import modules
from apps.appointments import Appointment, AppointmentsApp
from apps.todos import TodoItem, TodosApp
from apps.notes import Note, NotesApp
from apps.journal import JournalEntry, JournalApp
from lib.display import Display
from lib.keyboard import Keyboard

print("=" * 60)
print("INTEGRATION TESTS")
print("=" * 60)
print()

# Ensure data directory exists
os.makedirs('data', exist_ok=True)

def cleanup_test_files():
    """Remove test data files"""
    test_files = [
        'data/test_appointments.json',
        'data/test_todos.json',
        'data/test_notes.json',
        'data/test_journal.json'
    ]
    for f in test_files:
        try:
            os.remove(f)
        except:
            pass

# Test 1: Appointments Workflow
print("Test 1: Appointments Workflow")
print("-" * 60)

# Create appointments
appt1 = Appointment((2024, 6, 15), "09:00", "Team Meeting", "Discuss Q2 goals")
appt2 = Appointment((2024, 6, 20), "14:30", "Dentist", "Regular checkup")
appt3 = Appointment((2024, 6, 18), "11:00", "Lunch with client", "New project discussion")

appointments = [appt1, appt2, appt3]

# Save to file
data = [a.to_dict() for a in appointments]
with open('data/test_appointments.json', 'w') as f:
    json.dump(data, f)
print(f"✓ Created {len(appointments)} appointments")

# Load from file
with open('data/test_appointments.json', 'r') as f:
    loaded_data = json.load(f)
loaded_appts = [Appointment.from_dict(a) for a in loaded_data]
print(f"✓ Loaded {len(loaded_appts)} appointments")

# Verify data integrity
assert len(loaded_appts) == len(appointments)
assert loaded_appts[0].title == appt1.title
assert loaded_appts[1].time == appt2.time
print("✓ Data integrity verified")

# Sort by date
sorted_appts = sorted(loaded_appts, key=lambda a: (a.date, a.time))
assert sorted_appts[0].date == (2024, 6, 15)
assert sorted_appts[-1].date == (2024, 6, 20)
print("✓ Sorting works correctly")
print()

# Test 2: To-Do List Workflow
print("Test 2: To-Do List Workflow")
print("-" * 60)

# Create todos with different priorities
todo1 = TodoItem("Fix bug in login", priority=TodoItem.PRIORITY_HIGH)
todo2 = TodoItem("Update documentation", priority=TodoItem.PRIORITY_LOW)
todo3 = TodoItem("Review pull requests", priority=TodoItem.PRIORITY_NORMAL)

todos = [todo1, todo2, todo3]

# Save to file
data = [t.to_dict() for t in todos]
with open('data/test_todos.json', 'w') as f:
    json.dump(data, f)
print(f"✓ Created {len(todos)} to-do items")

# Complete some tasks
todo1.completed = True
todo3.completed = True

# Save updated state
data = [t.to_dict() for t in todos]
with open('data/test_todos.json', 'w') as f:
    json.dump(data, f)

# Load and verify
with open('data/test_todos.json', 'r') as f:
    loaded_data = json.load(f)
loaded_todos = [TodoItem.from_dict(t) for t in loaded_data]

assert loaded_todos[0].completed == True
assert loaded_todos[1].completed == False
assert loaded_todos[2].completed == True
print("✓ Task completion tracking works")

# Calculate statistics
total = len(loaded_todos)
completed = sum(1 for t in loaded_todos if t.completed)
pending = total - completed
completion_rate = (completed * 100) // total

assert completion_rate == 66  # 2 out of 3 completed
print(f"✓ Statistics: {completed}/{total} completed ({completion_rate}%)")
print()

# Test 3: Notes Workflow
print("Test 3: Notes Workflow")
print("-" * 60)

# Create notes
note1 = Note("Meeting Notes", "Discussed project timeline and deliverables")
note2 = Note("Ideas", "New feature: dark mode, user preferences")
note3 = Note("Shopping List", "Milk, eggs, bread, coffee")

notes = [note1, note2, note3]

# Save to file
data = [n.to_dict() for n in notes]
with open('data/test_notes.json', 'w') as f:
    json.dump(data, f)
print(f"✓ Created {len(notes)} notes")

# Search functionality
search_term = "project"
matches = [n for n in notes if search_term.lower() in n.title.lower() or
           search_term.lower() in n.content.lower()]
assert len(matches) == 1
assert matches[0].title == "Meeting Notes"
print("✓ Search functionality works")

# Edit note
note1.content = "Updated: Discussed project timeline, deliverables, and budget"
note1.modified = time.time()

# Save and reload
data = [n.to_dict() for n in notes]
with open('data/test_notes.json', 'w') as f:
    json.dump(data, f)

with open('data/test_notes.json', 'r') as f:
    loaded_data = json.load(f)
loaded_notes = [Note.from_dict(n) for n in loaded_data]

assert "budget" in loaded_notes[0].content
print("✓ Note editing works")
print()

# Test 4: Journal Workflow
print("Test 4: Journal Workflow")
print("-" * 60)

# Create journal entries
entry1 = JournalEntry((2024, 6, 10), "Great day! Finished project early.", mood='great')
entry2 = JournalEntry((2024, 6, 11), "Long meeting, felt tired.", mood='okay')
entry3 = JournalEntry((2024, 6, 12), "Productive day, made good progress.", mood='good')
entry4 = JournalEntry((2024, 6, 13), "Stressful day with many issues.", mood='bad')

entries = [entry1, entry2, entry3, entry4]

# Save to file
data = [e.to_dict() for e in entries]
with open('data/test_journal.json', 'w') as f:
    json.dump(data, f)
print(f"✓ Created {len(entries)} journal entries")

# Calculate mood statistics
mood_counts = {}
for entry in entries:
    mood_counts[entry.mood] = mood_counts.get(entry.mood, 0) + 1

assert mood_counts['great'] == 1
assert mood_counts['good'] == 1
assert mood_counts['okay'] == 1
assert mood_counts['bad'] == 1
print("✓ Mood tracking works")

# Sort by date
sorted_entries = sorted(entries, key=lambda e: e.date)
assert sorted_entries[0].date == (2024, 6, 10)
assert sorted_entries[-1].date == (2024, 6, 13)
print("✓ Date sorting works")
print()

# Test 5: Data Persistence Across Reload
print("Test 5: Data Persistence")
print("-" * 60)

# Verify all test files exist
test_files = [
    'data/test_appointments.json',
    'data/test_todos.json',
    'data/test_notes.json',
    'data/test_journal.json'
]

for filepath in test_files:
    assert os.path.exists(filepath), f"{filepath} should exist"
    with open(filepath, 'r') as f:
        data = json.load(f)
        assert len(data) > 0, f"{filepath} should have data"
    print(f"✓ {filepath} persists correctly")

print()

# Test 6: Display and Keyboard Initialization
print("Test 6: Hardware Abstraction")
print("-" * 60)

display = Display()
keyboard = Keyboard()

# Test display properties
assert display.WIDTH == 320
assert display.HEIGHT == 320
assert display.BLACK == 0x0000
assert display.WHITE == 0xFFFF
print("✓ Display initialized with correct properties")

# Test display operations (mocked)
display.clear()
display.text("Test", 10, 10)
display.rect(0, 0, 100, 100, display.BLUE)
display.show()
print("✓ Display operations work without errors")

# Test keyboard operations (mocked)
key = keyboard.read_key()  # Should return None in simulation
assert key is None or isinstance(key, int)
print("✓ Keyboard operations work without errors")
print()

# Cleanup
print("Cleaning up test files...")
cleanup_test_files()
print("✓ Test files cleaned up")
print()

# Summary
print("=" * 60)
print("INTEGRATION TEST SUMMARY")
print("=" * 60)
print("✓ Appointments workflow: PASS")
print("✓ To-Do list workflow: PASS")
print("✓ Notes workflow: PASS")
print("✓ Journal workflow: PASS")
print("✓ Data persistence: PASS")
print("✓ Hardware abstraction: PASS")
print()
print("ALL INTEGRATION TESTS PASSED")
print("=" * 60)
