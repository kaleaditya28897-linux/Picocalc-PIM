"""
Notes manager for Picocalc PIM
Create, view, edit, and organize notes
"""

import time
import json
from lib.ui import Menu, InputDialog, MessageBox, ConfirmDialog, ListView


class Note:
    """Note data class"""

    def __init__(self, title, content, created=None, modified=None, id=None):
        """Initialize note"""
        self.id = id or self._generate_id()
        self.title = title
        self.content = content
        self.created = created or time.time()
        self.modified = modified or self.created

    def _generate_id(self):
        """Generate unique ID"""
        return str(time.time()).replace('.', '')

    def to_dict(self):
        """Convert to dictionary"""
        return {
            'id': self.id,
            'title': self.title,
            'content': self.content,
            'created': self.created,
            'modified': self.modified
        }

    @classmethod
    def from_dict(cls, data):
        """Create from dictionary"""
        return cls(
            title=data['title'],
            content=data['content'],
            created=data.get('created'),
            modified=data.get('modified'),
            id=data.get('id')
        )

    def __str__(self):
        """String representation"""
        # Format timestamp
        t = time.localtime(self.modified)
        date_str = f"{t[0]}-{t[1]:02d}-{t[2]:02d}"
        return f"{self.title} ({date_str})"


class NotesApp:
    """Notes manager application"""

    DATA_FILE = "data/notes.json"

    def __init__(self, display, keyboard):
        """Initialize notes app"""
        self.display = display
        self.keyboard = keyboard
        self.notes = []
        self.load_notes()

    def load_notes(self):
        """Load notes from file"""
        try:
            with open(self.DATA_FILE, 'r') as f:
                data = json.load(f)
                self.notes = [Note.from_dict(n) for n in data]
        except:
            self.notes = []

    def save_notes(self):
        """Save notes to file"""
        try:
            data = [n.to_dict() for n in self.notes]
            with open(self.DATA_FILE, 'w') as f:
                json.dump(data, f)
        except Exception as e:
            msg = MessageBox(self.display, self.keyboard,
                           title="Error",
                           message=f"Failed to save:\n{str(e)}")
            msg.show()

    def add_note(self):
        """Add new note"""
        # Get title
        dlg = InputDialog(self.display, self.keyboard,
                         title="New Note",
                         prompt="Title:",
                         max_length=30)
        title = dlg.show()
        if not title:
            return

        # Get content (simplified for now - in real app might want multiline editor)
        dlg = InputDialog(self.display, self.keyboard,
                         title="New Note",
                         prompt="Content:",
                         max_length=200)
        content = dlg.show()
        if content is None:
            return

        # Create note
        note = Note(title, content)
        self.notes.append(note)
        self.notes.sort(key=lambda n: n.modified, reverse=True)
        self.save_notes()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="Note added!")
        msg.show()

    def view_notes(self):
        """View all notes"""
        if not self.notes:
            msg = MessageBox(self.display, self.keyboard,
                           title="Notes",
                           message="No notes")
            msg.show()
            return

        # Sort by modified date (newest first)
        sorted_notes = sorted(self.notes, key=lambda n: n.modified, reverse=True)
        items = [str(n) for n in sorted_notes]

        listview = ListView(self.display, self.keyboard,
                           title=f"Notes ({len(items)})",
                           items=items)
        selected = listview.show()

        if selected is not None:
            self._view_note(sorted_notes[selected])

    def _view_note(self, note):
        """View note details"""
        # Show note content
        self.display.clear()

        # Title bar
        self.display.rect(0, 0, self.display.WIDTH, 30,
                         self.display.BLUE, fill=True)
        self.display.text(note.title[:30], 10, 10, self.display.WHITE)

        # Content
        y = 40
        # Word wrap content
        words = note.content.split()
        line = ""
        for word in words:
            test_line = line + " " + word if line else word
            if len(test_line) * 8 > self.display.WIDTH - 20:
                self.display.text(line, 10, y, self.display.WHITE)
                y += 15
                line = word
                if y > self.display.HEIGHT - 60:
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
                self._edit_note(note)
                return
            elif key == KEY_D:
                self._delete_note(note)
                return
            elif key == KEY_ESC:
                return

    def _edit_note(self, note):
        """Edit note"""
        # Edit title
        dlg = InputDialog(self.display, self.keyboard,
                         title="Edit Note",
                         prompt="Title:",
                         default=note.title,
                         max_length=30)
        title = dlg.show()
        if title is None:
            return

        # Edit content
        dlg = InputDialog(self.display, self.keyboard,
                         title="Edit Note",
                         prompt="Content:",
                         default=note.content,
                         max_length=200)
        content = dlg.show()
        if content is None:
            return

        # Update note
        note.title = title
        note.content = content
        note.modified = time.time()
        self.save_notes()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="Note updated!")
        msg.show()

    def _delete_note(self, note):
        """Delete note"""
        dlg = ConfirmDialog(self.display, self.keyboard,
                          title="Confirm Delete",
                          message=f"Delete this note?\n{note.title}")
        if dlg.show():
            self.notes.remove(note)
            self.save_notes()

            msg = MessageBox(self.display, self.keyboard,
                           title="Success",
                           message="Note deleted")
            msg.show()

    def search_notes(self):
        """Search notes by keyword"""
        # Get search term
        dlg = InputDialog(self.display, self.keyboard,
                         title="Search Notes",
                         prompt="Search for:",
                         max_length=30)
        search_term = dlg.show()
        if not search_term:
            return

        # Find matching notes
        search_lower = search_term.lower()
        matches = [n for n in self.notes
                  if search_lower in n.title.lower() or
                     search_lower in n.content.lower()]

        if not matches:
            msg = MessageBox(self.display, self.keyboard,
                           title="Search Results",
                           message="No matches found")
            msg.show()
            return

        # Show results
        items = [str(n) for n in matches]
        listview = ListView(self.display, self.keyboard,
                           title=f"Found {len(matches)} note(s)",
                           items=items)
        selected = listview.show()

        if selected is not None:
            self._view_note(matches[selected])

    def run(self):
        """Run notes application"""
        menu = Menu(self.display, self.keyboard,
                   title="Notes",
                   items=[
                       ("Add Note", self.add_note),
                       ("View Notes", self.view_notes),
                       ("Search Notes", self.search_notes),
                       ("Back", lambda: "exit")
                   ])
        menu.show()
