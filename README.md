# Picocalc PIM - Personal Information Manager

A comprehensive Personal Information Manager (PIM) for the ClockworkPi Picocalc running MicroPython v1.27.0 on the RP2350 (Raspberry Pi Pico2W).

## Features

### Personal Information Management
- **Calendar**: Monthly calendar view with navigation
- **Appointments**: Create and manage appointments with date/time
- **To-Do Lists**: Task management with priorities (Low, Normal, High)
- **Notes**: Create, edit, and search notes
- **Journal**: Daily journal entries with mood tracking

### Games
- **Snake**: Classic snake game with score tracking
- **Tetris**: Full-featured Tetris with levels and line clearing

## Hardware Requirements

- **Device**: ClockworkPi Picocalc
- **Microcontroller**: Raspberry Pi Pico2W (RP2350)
- **Display**: 320x320 IPS display (ST7365P controller)
- **Input**: 67-key keyboard via I2C
- **Firmware**: MicroPython v1.27.0

## Installation

### 1. Install MicroPython

Ensure your Picocalc is running MicroPython v1.27.0. You can download it from the [MicroPython website](https://micropython.org/download/rp2-pico-w/).

### 2. Upload Files

Upload all files to your Picocalc using your preferred method:

**Using Thonny IDE:**
1. Connect your Picocalc via USB
2. Open Thonny IDE
3. Select "MicroPython (Raspberry Pi Pico)" as the interpreter
4. Upload all files maintaining the directory structure:
   ```
   Picocalc-PIM/
   ├── main.py
   ├── lib/
   │   ├── display.py
   │   ├── keyboard.py
   │   └── ui.py
   ├── apps/
   │   ├── calendar_app.py
   │   ├── appointments.py
   │   ├── todos.py
   │   ├── notes.py
   │   └── journal.py
   └── games/
       ├── snake.py
       └── tetris.py
   ```

**Using rshell:**
```bash
pip install rshell
rshell -p /dev/ttyUSB0  # Adjust port as needed
rsync -r . /pyboard/
```

**Using mpremote:**
```bash
pip install mpremote
mpremote connect /dev/ttyUSB0 fs cp -r . :
```

### 3. Create Data Directory

The application will automatically create a `data/` directory for storing persistent data, but you can create it manually if needed:

```python
import os
os.mkdir('data')
```

## Usage

### Starting the Application

Run the application by executing `main.py`:

```python
import main
main.main()
```

Or set it to run automatically on boot by renaming `main.py` to `boot.py`.

### Navigation

The PIM uses the Picocalc keyboard for navigation:

- **Arrow Keys**: Navigate menus and move in games
- **ENTER**: Select/Confirm
- **ESC**: Back/Cancel
- **Number Keys**: Quick menu selection
- **Letter Keys**: Text input
- **Special Keys**: Game-specific controls

### Main Menu

The main menu provides access to all features:

1. Calendar
2. Appointments
3. To-Do List
4. Notes
5. Journal
6. Snake Game
7. Tetris Game
8. About
9. Exit

## Features Guide

### Calendar
- View monthly calendar
- Navigate months/years with arrow keys
- Current day is highlighted
- Press ENTER to return to current month

### Appointments
- **Add Appointment**: Create appointments with date, time, title, and description
- **View Appointments**: Browse all appointments chronologically
- **Delete Appointment**: Remove appointments

Data is saved to `data/appointments.json`

### To-Do List
- **Add To-Do**: Create tasks with priorities (Low, Normal, High)
- **View To-Dos**: See all tasks sorted by status and priority
- **Mark Complete/Incomplete**: Toggle task status
- **Statistics**: View completion rate and task breakdown
- **Delete Completed**: Bulk delete completed tasks

Data is saved to `data/todos.json`

### Notes
- **Add Note**: Create notes with title and content
- **View Notes**: Browse all notes (sorted by modification date)
- **Edit Note**: Update note content
- **Search Notes**: Find notes by keyword
- **Delete Note**: Remove notes

Data is saved to `data/notes.json`

### Journal
- **New Entry**: Create daily journal entries with mood tracking
- **Mood Options**: Great, Good, Okay, Bad, Terrible
- **View Entries**: Browse past entries
- **Edit Entry**: Update existing entries
- **Mood Stats**: View mood statistics and trends

Data is saved to `data/journal.json`

### Snake Game
- Control the snake with arrow keys
- Eat food to grow and score points
- Avoid walls and your own tail
- Speed increases as you score more points
- Press ENTER to restart after game over

### Tetris Game
- **Arrow Keys**: Move pieces left/right/down
- **UP Arrow**: Rotate piece
- **Complete lines** to score points
- Score increases with level
- Speed increases every 10 lines

## Hardware Configuration

### Display Pins (SPI)
```python
SPI0:
- SCK:  Pin 18
- MOSI: Pin 19
- MISO: Pin 16
- CS:   Pin 17
- DC:   Pin 20
- RST:  Pin 21
```

### Keyboard (I2C)
```python
I2C0:
- SCL: Pin 5
- SDA: Pin 4
- Address: 0x55
```

**Note**: Adjust pin configurations in `lib/display.py` and `lib/keyboard.py` if your hardware differs.

## Customization

### Adjusting Display Settings

Edit `lib/display.py` to modify:
- Display resolution
- Color scheme
- SPI speed
- Pin assignments

### Modifying Game Settings

Edit game files in `games/` to adjust:
- **Snake**: Grid size, speed, colors
- **Tetris**: Block size, drop speed, colors

### Adding New Features

The modular structure makes it easy to add new applications:

1. Create a new file in `apps/` or `games/`
2. Import in `main.py`
3. Add to the main menu
4. Follow the existing app structure

Example:
```python
class MyNewApp:
    def __init__(self, display, keyboard):
        self.display = display
        self.keyboard = keyboard

    def run(self):
        # Your app logic here
        pass
```

## Data Persistence

All user data is stored in JSON format in the `data/` directory:

- `appointments.json`: Appointments
- `todos.json`: To-do items
- `notes.json`: Notes
- `journal.json`: Journal entries

You can backup these files to preserve your data.

## Troubleshooting

### Display Not Working
- Check SPI pin connections
- Verify display controller initialization
- Test with a simple framebuffer demo

### Keyboard Not Responding
- Check I2C connections
- Verify keyboard controller address
- Try scanning for I2C devices

### File System Errors
- Ensure `data/` directory exists
- Check available flash storage
- Verify file permissions

### Out of Memory
- Reduce buffer sizes
- Simplify graphics
- Clear unused data files

## Performance Tips

1. **Reduce display updates**: Only call `display.show()` when needed
2. **Optimize data files**: Periodically clean old entries
3. **Use efficient data structures**: Keep lists sorted for faster access
4. **Minimize allocations**: Reuse objects when possible

## Development

### Testing Without Hardware

The display and keyboard modules include simulation modes for testing without physical hardware:

```python
from lib.keyboard import SimulatedKeyboard

# Use simulated keyboard for testing
kbd = SimulatedKeyboard()
kbd.simulate_text("Hello")
```

### Debugging

Enable debug output:
```python
import sys
sys.print_exception(e)  # Print full exception trace
```

## License

This project is provided under the same license as the repository (see LICENSE file).

## Credits

- Built for ClockworkPi Picocalc
- MicroPython v1.27.0
- Raspberry Pi Pico2W (RP2350)

## Contributing

Contributions are welcome! Feel free to:
- Report bugs
- Suggest features
- Submit pull requests
- Share your customizations

## Resources

- [ClockworkPi Picocalc](https://www.clockworkpi.com/picocalc)
- [MicroPython Documentation](https://docs.micropython.org/)
- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [Picocalc GitHub](https://github.com/clockworkpi/PicoCalc)

## Version History

### v1.0.0 (Initial Release)
- Calendar with monthly view
- Appointments manager
- To-do list with priorities
- Notes with search
- Journal with mood tracking
- Snake game
- Tetris game
- Data persistence
- Full keyboard navigation

---

Made with ♥ for the Picocalc community
