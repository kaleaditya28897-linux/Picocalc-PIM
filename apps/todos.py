"""
To-Do List manager for Picocalc PIM
Create, manage, and track to-do items with priorities
"""

import time
import json
from lib.ui import Menu, InputDialog, MessageBox, ConfirmDialog, ListView


class TodoItem:
    """To-Do item data class"""

    PRIORITY_LOW = 0
    PRIORITY_NORMAL = 1
    PRIORITY_HIGH = 2

    PRIORITY_NAMES = {
        PRIORITY_LOW: "Low",
        PRIORITY_NORMAL: "Normal",
        PRIORITY_HIGH: "High"
    }

    def __init__(self, title, priority=PRIORITY_NORMAL, completed=False,
                 created=None, id=None):
        """Initialize to-do item"""
        self.id = id or self._generate_id()
        self.title = title
        self.priority = priority
        self.completed = completed
        self.created = created or time.time()

    def _generate_id(self):
        """Generate unique ID"""
        return str(time.time()).replace('.', '')

    def to_dict(self):
        """Convert to dictionary"""
        return {
            'id': self.id,
            'title': self.title,
            'priority': self.priority,
            'completed': self.completed,
            'created': self.created
        }

    @classmethod
    def from_dict(cls, data):
        """Create from dictionary"""
        return cls(
            title=data['title'],
            priority=data.get('priority', cls.PRIORITY_NORMAL),
            completed=data.get('completed', False),
            created=data.get('created'),
            id=data.get('id')
        )

    def __str__(self):
        """String representation"""
        status = "[X]" if self.completed else "[ ]"
        priority_sym = {
            self.PRIORITY_LOW: " ",
            self.PRIORITY_NORMAL: "!",
            self.PRIORITY_HIGH: "!!!"
        }.get(self.priority, "")

        return f"{status} {self.title} {priority_sym}"


class TodosApp:
    """To-Do List application"""

    DATA_FILE = "data/todos.json"

    def __init__(self, display, keyboard):
        """Initialize to-do app"""
        self.display = display
        self.keyboard = keyboard
        self.todos = []
        self.load_todos()

    def load_todos(self):
        """Load to-dos from file"""
        try:
            with open(self.DATA_FILE, 'r') as f:
                data = json.load(f)
                self.todos = [TodoItem.from_dict(t) for t in data]
        except:
            self.todos = []

    def save_todos(self):
        """Save to-dos to file"""
        try:
            data = [t.to_dict() for t in self.todos]
            with open(self.DATA_FILE, 'w') as f:
                json.dump(data, f)
        except Exception as e:
            msg = MessageBox(self.display, self.keyboard,
                           title="Error",
                           message=f"Failed to save:\n{str(e)}")
            msg.show()

    def add_todo(self):
        """Add new to-do item"""
        # Get title
        dlg = InputDialog(self.display, self.keyboard,
                         title="New To-Do",
                         prompt="Task:",
                         max_length=50)
        title = dlg.show()
        if not title:
            return

        # Get priority
        priority_menu = Menu(self.display, self.keyboard,
                            title="Select Priority",
                            items=[
                                ("Low", lambda: TodoItem.PRIORITY_LOW),
                                ("Normal", lambda: TodoItem.PRIORITY_NORMAL),
                                ("High", lambda: TodoItem.PRIORITY_HIGH)
                            ])

        # Show priority selection
        self.display.clear()
        self.display.text("Select Priority", 80, 100, self.display.WHITE)
        self.display.text("1. Low", 100, 130, self.display.WHITE)
        self.display.text("2. Normal", 100, 150, self.display.WHITE)
        self.display.text("3. High", 100, 170, self.display.WHITE)
        self.display.show()

        from lib.keyboard import KEY_1, KEY_2, KEY_3
        key = self.keyboard.wait_key(timeout=10000)

        priority = TodoItem.PRIORITY_NORMAL
        if key == KEY_1:
            priority = TodoItem.PRIORITY_LOW
        elif key == KEY_2:
            priority = TodoItem.PRIORITY_NORMAL
        elif key == KEY_3:
            priority = TodoItem.PRIORITY_HIGH

        # Create to-do item
        todo = TodoItem(title, priority=priority)
        self.todos.append(todo)
        self.save_todos()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="To-Do added!")
        msg.show()

    def view_todos(self):
        """View all to-do items"""
        if not self.todos:
            msg = MessageBox(self.display, self.keyboard,
                           title="To-Do List",
                           message="No to-do items")
            msg.show()
            return

        # Sort: incomplete first, then by priority
        sorted_todos = sorted(self.todos,
                            key=lambda t: (t.completed, -t.priority, t.created))

        items = [str(t) for t in sorted_todos]

        # Count stats
        total = len(self.todos)
        completed = sum(1 for t in self.todos if t.completed)
        pending = total - completed

        title = f"To-Do ({pending}/{total})"

        listview = ListView(self.display, self.keyboard,
                           title=title,
                           items=items)
        selected = listview.show()

        if selected is not None:
            self._todo_actions(sorted_todos[selected])

    def _todo_actions(self, todo):
        """Show actions for a to-do item"""
        actions = []

        if todo.completed:
            actions.append(("Mark Incomplete", lambda: self._toggle_complete(todo)))
        else:
            actions.append(("Mark Complete", lambda: self._toggle_complete(todo)))

        actions.append(("Delete", lambda: self._delete_todo(todo)))
        actions.append(("Back", lambda: "exit"))

        menu = Menu(self.display, self.keyboard,
                   title=todo.title[:20],
                   items=actions)
        menu.show()

    def _toggle_complete(self, todo):
        """Toggle to-do completion status"""
        todo.completed = not todo.completed
        self.save_todos()

        status = "completed" if todo.completed else "incomplete"
        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message=f"Marked as {status}")
        msg.show()

    def _delete_todo(self, todo):
        """Delete a to-do item"""
        dlg = ConfirmDialog(self.display, self.keyboard,
                          title="Confirm Delete",
                          message=f"Delete this to-do?\n{todo.title}")
        if dlg.show():
            self.todos.remove(todo)
            self.save_todos()

            msg = MessageBox(self.display, self.keyboard,
                           title="Success",
                           message="To-Do deleted")
            msg.show()

    def delete_completed(self):
        """Delete all completed to-dos"""
        completed = [t for t in self.todos if t.completed]

        if not completed:
            msg = MessageBox(self.display, self.keyboard,
                           title="Delete Completed",
                           message="No completed items")
            msg.show()
            return

        dlg = ConfirmDialog(self.display, self.keyboard,
                          title="Confirm Delete",
                          message=f"Delete {len(completed)} completed items?")
        if dlg.show():
            self.todos = [t for t in self.todos if not t.completed]
            self.save_todos()

            msg = MessageBox(self.display, self.keyboard,
                           title="Success",
                           message=f"Deleted {len(completed)} items")
            msg.show()

    def stats(self):
        """Show to-do statistics"""
        if not self.todos:
            msg = MessageBox(self.display, self.keyboard,
                           title="Statistics",
                           message="No to-do items")
            msg.show()
            return

        total = len(self.todos)
        completed = sum(1 for t in self.todos if t.completed)
        pending = total - completed
        high_priority = sum(1 for t in self.todos
                          if t.priority == TodoItem.PRIORITY_HIGH and not t.completed)

        completion_rate = (completed * 100) // total if total > 0 else 0

        message = (
            f"Total Tasks: {total}\n"
            f"Completed: {completed}\n"
            f"Pending: {pending}\n"
            f"High Priority: {high_priority}\n"
            f"\nCompletion Rate: {completion_rate}%"
        )

        msg = MessageBox(self.display, self.keyboard,
                        title="To-Do Statistics",
                        message=message)
        msg.show()

    def run(self):
        """Run to-do list application"""
        menu = Menu(self.display, self.keyboard,
                   title="To-Do List",
                   items=[
                       ("Add To-Do", self.add_todo),
                       ("View To-Dos", self.view_todos),
                       ("Statistics", self.stats),
                       ("Delete Completed", self.delete_completed),
                       ("Back", lambda: "exit")
                   ])
        menu.show()
