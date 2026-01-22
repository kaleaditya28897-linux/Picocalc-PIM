"""
Appointments manager for Picocalc PIM
Create, view, edit, and delete appointments with persistence
"""

import time
import json
from lib.ui import Menu, InputDialog, MessageBox, ConfirmDialog, ListView
from lib.keyboard import KEY_ESC


class Appointment:
    """Appointment data class"""

    def __init__(self, date, time_str, title, description="", id=None):
        """Initialize appointment"""
        self.id = id or self._generate_id()
        self.date = date  # (year, month, day)
        self.time = time_str  # "HH:MM" format
        self.title = title
        self.description = description

    def _generate_id(self):
        """Generate unique ID"""
        return str(time.time()).replace('.', '')

    def to_dict(self):
        """Convert to dictionary"""
        return {
            'id': self.id,
            'date': self.date,
            'time': self.time,
            'title': self.title,
            'description': self.description
        }

    @classmethod
    def from_dict(cls, data):
        """Create from dictionary"""
        return cls(
            date=tuple(data['date']),
            time_str=data['time'],
            title=data['title'],
            description=data.get('description', ''),
            id=data.get('id')
        )

    def __str__(self):
        """String representation"""
        year, month, day = self.date
        return f"{year}-{month:02d}-{day:02d} {self.time} - {self.title}"


class AppointmentsApp:
    """Appointments manager application"""

    DATA_FILE = "data/appointments.json"

    def __init__(self, display, keyboard):
        """Initialize appointments app"""
        self.display = display
        self.keyboard = keyboard
        self.appointments = []
        self.load_appointments()

    def load_appointments(self):
        """Load appointments from file"""
        try:
            with open(self.DATA_FILE, 'r') as f:
                data = json.load(f)
                self.appointments = [Appointment.from_dict(a) for a in data]
        except:
            self.appointments = []

    def save_appointments(self):
        """Save appointments to file"""
        try:
            data = [a.to_dict() for a in self.appointments]
            with open(self.DATA_FILE, 'w') as f:
                json.dump(data, f)
        except Exception as e:
            msg = MessageBox(self.display, self.keyboard,
                           title="Error",
                           message=f"Failed to save:\n{str(e)}")
            msg.show()

    def add_appointment(self):
        """Add new appointment"""
        # Get date
        now = time.localtime()
        year = self._input_number("Year", now[0], 2020, 2100)
        if year is None:
            return

        month = self._input_number("Month", now[1], 1, 12)
        if month is None:
            return

        day = self._input_number("Day", now[2], 1, 31)
        if day is None:
            return

        # Get time
        hour = self._input_number("Hour (24h)", 9, 0, 23)
        if hour is None:
            return

        minute = self._input_number("Minute", 0, 0, 59)
        if minute is None:
            return

        time_str = f"{hour:02d}:{minute:02d}"

        # Get title
        dlg = InputDialog(self.display, self.keyboard,
                         title="New Appointment",
                         prompt="Title:",
                         max_length=30)
        title = dlg.show()
        if not title:
            return

        # Get description
        dlg = InputDialog(self.display, self.keyboard,
                         title="New Appointment",
                         prompt="Description (optional):",
                         max_length=100)
        description = dlg.show()
        if description is None:
            description = ""

        # Create appointment
        appointment = Appointment((year, month, day), time_str, title, description)
        self.appointments.append(appointment)
        self.appointments.sort(key=lambda a: (a.date, a.time))
        self.save_appointments()

        msg = MessageBox(self.display, self.keyboard,
                        title="Success",
                        message="Appointment added!")
        msg.show()

    def _input_number(self, prompt, default, min_val, max_val):
        """Helper to input a number"""
        dlg = InputDialog(self.display, self.keyboard,
                         title="New Appointment",
                         prompt=f"{prompt} ({min_val}-{max_val}):",
                         default=str(default),
                         max_length=4)
        result = dlg.show()
        if result is None:
            return None

        try:
            num = int(result)
            if min_val <= num <= max_val:
                return num
        except:
            pass

        msg = MessageBox(self.display, self.keyboard,
                        title="Error",
                        message=f"Invalid {prompt.lower()}")
        msg.show()
        return None

    def view_appointments(self):
        """View all appointments"""
        if not self.appointments:
            msg = MessageBox(self.display, self.keyboard,
                           title="Appointments",
                           message="No appointments")
            msg.show()
            return

        # Sort by date and time
        sorted_appts = sorted(self.appointments, key=lambda a: (a.date, a.time))
        items = [str(a) for a in sorted_appts]

        listview = ListView(self.display, self.keyboard,
                           title=f"Appointments ({len(items)})",
                           items=items)
        selected = listview.show()

        if selected is not None:
            self._show_appointment_details(sorted_appts[selected])

    def _show_appointment_details(self, appointment):
        """Show appointment details"""
        year, month, day = appointment.date
        message = (
            f"Date: {year}-{month:02d}-{day:02d}\n"
            f"Time: {appointment.time}\n"
            f"Title: {appointment.title}\n"
            f"\n{appointment.description}"
        )

        msg = MessageBox(self.display, self.keyboard,
                        title="Appointment Details",
                        message=message)
        msg.show()

    def delete_appointment(self):
        """Delete an appointment"""
        if not self.appointments:
            msg = MessageBox(self.display, self.keyboard,
                           title="Delete",
                           message="No appointments to delete")
            msg.show()
            return

        # Show list
        sorted_appts = sorted(self.appointments, key=lambda a: (a.date, a.time))
        items = [str(a) for a in sorted_appts]

        listview = ListView(self.display, self.keyboard,
                           title="Delete Appointment",
                           items=items)
        selected = listview.show()

        if selected is not None:
            appointment = sorted_appts[selected]

            # Confirm deletion
            dlg = ConfirmDialog(self.display, self.keyboard,
                              title="Confirm Delete",
                              message=f"Delete this appointment?\n{appointment.title}")
            if dlg.show():
                self.appointments.remove(appointment)
                self.save_appointments()

                msg = MessageBox(self.display, self.keyboard,
                               title="Success",
                               message="Appointment deleted")
                msg.show()

    def run(self):
        """Run appointments application"""
        menu = Menu(self.display, self.keyboard,
                   title="Appointments",
                   items=[
                       ("Add Appointment", self.add_appointment),
                       ("View Appointments", self.view_appointments),
                       ("Delete Appointment", self.delete_appointment),
                       ("Back", lambda: "exit")
                   ])
        menu.show()
