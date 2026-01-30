"""
Journal application for Picocalc PIM
Daily journal entries with mood tracking
"""

import time
import json
from lib.ui import Menu, InputDialog, MessageBox, ConfirmDialog, ListView, TextAreaDialog


class JournalEntry:
    """Journal entry data class"""

    MOODS = {
        'great': '😊',
        'good': '🙂',
        'okay': '😐',
        'bad': '😞',
        'terrible': '😢'
    }

    def __init__(self, date, content, mood='okay', id=None):
        """Initialize journal entry"""
        self.id = id or self._generate_id()
        self.date = date  # (year, month, day)
        self.content = content
        self.mood = mood
        self.timestamp = time.time()

    def _generate_id(self):
        """Generate unique ID"""
        return str(time.time()).replace('.', '')

    def to_dict(self):
        """Convert to dictionary"""
        return {
            'id': self.id,
            'date': self.date,
            'content': self.content,
            'mood': self.mood,
            'timestamp': self.timestamp
        }

    @classmethod
    def from_dict(cls, data):
        """Create from dictionary"""
        entry = cls(
            date=tuple(data['date']),
            content=data['content'],
            mood=data.get('mood', 'okay'),
            id=data.get('id')
        )
        entry.timestamp = data.get('timestamp', time.time())
        return entry

    def __str__(self):
        """String representation"""
        year, month, day = self.date
        mood_symbol = self.MOODS.get(self.mood, '?')
        preview = self.content[:30] + "..." if len(self.content) > 30 else self.content
        return f"{year}-{month:02d}-{day:02d} {mood_symbol} {preview}"


class JournalApp:
    """Journal application"""

    DATA_FILE = "data/journal.json"

    def __init__(self, display, keyboard):
        """Initialize journal app"""
        self.display = display
        self.keyboard = keyboard
        self.entries = []
        self.load_entries()

    def load_entries(self):
        """Load journal entries from file"""
        try:
            with open(self.DATA_FILE, 'r') as f:
                data = json.load(f)
                self.entries = [JournalEntry.from_dict(e) for e in data]
        except:
            self.entries = []

    def save_entries(self):
        """Save journal entries to file"""
        try:
            data = [e.to_dict() for e in self.entries]
            with open(self.DATA_FILE, 'w') as f:
                json.dump(data, f)
        except Exception as e:
            msg = MessageBox(self.display, self.keyboard,
                           title="Error",
                           message=f"Failed to save:\n{str(e)}")
            msg.show()

    def new_entry(self):
        """Create new journal entry"""
        # Default to today's date
        now = time.localtime()
        date = (now[0], now[1], now[2])

        # Check if entry already exists for today
        existing = [e for e in self.entries if e.date == date]
        if existing:
            dlg = ConfirmDialog(self.display, self.keyboard,
                              title="Entry Exists",
                              message="Entry exists for today.\nEdit existing entry?")
            if dlg.show():
                self._edit_entry(existing[0])
            return

        # Get mood
        mood = self._select_mood()
        if mood is None:
            return

        # Get content with multi-line text area
        dlg = TextAreaDialog(self.display, self.keyboard,
                            title="New Journal Entry",
                            prompt="What's on your mind?",
                            max_length=500)
        content = dlg.show()
        if not content:
            return

        # Create entry
        entry = JournalEntry(date, content, mood)
        self.entries.append(entry)
        self.entries.sort(key=lambda e: e.date, reverse=True)
        self.save_entries()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="Journal entry saved!")
        msg.show()

    def _select_mood(self):
        """Select mood for entry"""
        self.display.clear()

        # Title
        self.display.rect(0, 0, self.display.WIDTH, 30,
                         self.display.BLUE, fill=True)
        self.display.text("How are you feeling?", 50, 10, self.display.WHITE)

        # Mood options
        y = 60
        self.display.text("1. Great :)", 80, y, self.display.WHITE)
        y += 30
        self.display.text("2. Good :)", 80, y, self.display.WHITE)
        y += 30
        self.display.text("3. Okay :|", 80, y, self.display.WHITE)
        y += 30
        self.display.text("4. Bad :(", 80, y, self.display.WHITE)
        y += 30
        self.display.text("5. Terrible :((", 80, y, self.display.WHITE)

        self.display.text("ESC: Cancel", 100, self.display.HEIGHT - 20,
                         self.display.GRAY)

        self.display.show()

        # Wait for selection
        from lib.keyboard import KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_ESC

        while True:
            key = self.keyboard.wait_key(timeout=10000)

            if key == KEY_1:
                return 'great'
            elif key == KEY_2:
                return 'good'
            elif key == KEY_3:
                return 'okay'
            elif key == KEY_4:
                return 'bad'
            elif key == KEY_5:
                return 'terrible'
            elif key == KEY_ESC:
                return None

    def view_entries(self):
        """View all journal entries"""
        if not self.entries:
            msg = MessageBox(self.display, self.keyboard,
                           title="Journal",
                           message="No journal entries")
            msg.show()
            return

        # Sort by date (newest first)
        sorted_entries = sorted(self.entries, key=lambda e: e.date, reverse=True)
        items = [str(e) for e in sorted_entries]

        listview = ListView(self.display, self.keyboard,
                           title=f"Journal ({len(items)} entries)",
                           items=items)
        selected = listview.show()

        if selected is not None:
            self._view_entry(sorted_entries[selected])

    def _view_entry(self, entry):
        """View entry details"""
        year, month, day = entry.date
        mood_name = entry.mood.capitalize()

        # Show entry
        self.display.clear()

        # Title bar
        date_str = f"{year}-{month:02d}-{day:02d}"
        self.display.rect(0, 0, self.display.WIDTH, 30,
                         self.display.BLUE, fill=True)
        self.display.text(f"{date_str} - {mood_name}", 40, 10, self.display.WHITE)

        # Content with word wrap
        y = 50
        words = entry.content.split()
        line = ""
        for word in words:
            test_line = line + " " + word if line else word
            if len(test_line) * 8 > self.display.WIDTH - 20:
                self.display.text(line, 10, y, self.display.WHITE)
                y += 15
                line = word
                if y > self.display.HEIGHT - 60:
                    self.display.text("...", 10, y, self.display.GRAY)
                    break
            else:
                line = test_line

        if line and y <= self.display.HEIGHT - 60:
            self.display.text(line, 10, y, self.display.WHITE)

        # Instructions
        self.display.text("E: Edit  D: Delete  ESC: Back",
                         20, self.display.HEIGHT - 20, self.display.GRAY)

        self.display.show()

        # Handle input
        from lib.keyboard import KEY_E, KEY_D, KEY_ESC
        while True:
            key = self.keyboard.wait_key(timeout=5000)

            if key == KEY_E:
                self._edit_entry(entry)
                return
            elif key == KEY_D:
                self._delete_entry(entry)
                return
            elif key == KEY_ESC:
                return

    def _edit_entry(self, entry):
        """Edit journal entry"""
        # Edit mood
        mood = self._select_mood()
        if mood is None:
            return

        # Edit content with multi-line text area
        dlg = TextAreaDialog(self.display, self.keyboard,
                            title="Edit Entry",
                            prompt="Content:",
                            default=entry.content,
                            max_length=500)
        content = dlg.show()
        if content is None:
            return

        # Update entry
        entry.mood = mood
        entry.content = content
        entry.timestamp = time.time()
        self.save_entries()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="Entry updated!")
        msg.show()

    def _delete_entry(self, entry):
        """Delete journal entry"""
        year, month, day = entry.date
        date_str = f"{year}-{month:02d}-{day:02d}"

        dlg = ConfirmDialog(self.display, self.keyboard,
                          title="Confirm Delete",
                          message=f"Delete entry from\n{date_str}?")
        if dlg.show():
            self.entries.remove(entry)
            self.save_entries()

            msg = MessageBox(self.display, self.keyboard,
                           title="Success",
                           message="Entry deleted")
            msg.show()

    def mood_stats(self):
        """Show mood statistics"""
        if not self.entries:
            msg = MessageBox(self.display, self.keyboard,
                           title="Mood Stats",
                           message="No journal entries")
            msg.show()
            return

        # Count moods
        mood_counts = {}
        for entry in self.entries:
            mood_counts[entry.mood] = mood_counts.get(entry.mood, 0) + 1

        # Calculate percentages
        total = len(self.entries)
        stats_lines = [f"Total Entries: {total}\n"]

        for mood in ['great', 'good', 'okay', 'bad', 'terrible']:
            count = mood_counts.get(mood, 0)
            percent = (count * 100) // total if total > 0 else 0
            stats_lines.append(f"{mood.capitalize()}: {count} ({percent}%)")

        message = "\n".join(stats_lines)

        msg = MessageBox(self.display, self.keyboard,
                        title="Mood Statistics",
                        message=message)
        msg.show()

    def run(self):
        """Run journal application"""
        menu = Menu(self.display, self.keyboard,
                   title="Journal",
                   items=[
                       ("New Entry", self.new_entry),
                       ("View Entries", self.view_entries),
                       ("Mood Stats", self.mood_stats),
                       ("Back", lambda: "exit")
                   ])
        menu.show()
