# Cross-Platform Qt Terminal Widget Architecture

This document explains how to build a real terminal widget for a Qt/C++ application, similar in purpose to `QTermWidget`, but designed to work cross-platform on Linux, macOS, and Windows.

The goal is not just to show command output, but to create a real interactive terminal that can run shells and terminal programs like:

```bash
bash
zsh
cmd.exe
powershell.exe
python
node
git
nano
vim
htop
```

---

## 1. Big Picture

A real terminal widget has multiple layers.

```text
Your Qt Application
        |
        v
TerminalWidget
        |
        v
TerminalEmulator
        |
        v
PTY Backend
        |
        v
Shell Process
        |
        v
Programs: gcc, git, python, npm, vim, htop, etc.
```

The terminal widget is not just a `QTextEdit`.

A real terminal must support:

- keyboard input
- cursor movement
- colors
- bold/italic/underline text
- screen clearing
- scrolling
- alternate screen buffer
- resize events
- copy/paste
- shell interaction
- programs that redraw the screen, such as `vim`, `nano`, and `htop`

---

## 2. Terminal vs Shell

A common confusion is thinking the terminal itself executes commands.

It does not.

### Terminal

The terminal is the visual interface.

It handles:

- drawing text
- receiving keyboard input
- cursor display
- colors
- scrollback
- copy/paste
- mouse selection

Examples:

```text
GNOME Terminal
Konsole
Windows Terminal
Alacritty
QTermWidget
```

### Shell

The shell understands commands.

Examples:

```text
Linux/macOS:
  bash
  zsh
  fish

Windows:
  cmd.exe
  powershell.exe
  pwsh.exe
```

When the user types:

```bash
g++ main.cpp -o app
```

the terminal sends this input to the shell.

The shell executes the command and sends output back to the terminal.

---

## 3. Why QProcess Alone Is Not Enough

`QProcess` is good for simple output panels.

Example:

```cpp
QProcess *process = new QProcess(this);
process->start("g++", {"main.cpp", "-o", "app"});
```

This works for:

```text
compiler output
test output
basic program output
logs
errors
warnings
```

But it is not enough for a real terminal.

Programs like these expect a real terminal:

```bash
vim
nano
htop
python
node
git log
```

They use terminal control sequences to:

- move the cursor
- repaint old lines
- clear the screen
- change colors
- use full-screen mode
- detect terminal size

For this, we need a PTY backend and a terminal emulator/parser.

---

## 4. Required Stack

The recommended stack is:

```text
Qt Widget / QAbstractScrollArea
        |
        v
libvterm
        |
        v
Pty-Qt / libptyqt
        |
        v
Platform Backend
        |
        +-- Linux/macOS: PTY / openpty / forkpty
        |
        +-- Windows: ConPTY
        |
        v
Shell
```

### Practical Short Version

Use:

```text
Qt + libvterm + Pty-Qt
```

This is the smallest serious stack for a real cross-platform terminal widget.

---

## 5. Important Components

## 5.1 Qt Frontend Widget

This is your visual widget.

Recommended base class:

```cpp
class TerminalWidget : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};
```

Use `QAbstractScrollArea` instead of plain `QWidget` because a terminal needs scrollback and scrolling.

Responsibilities:

```text
draw terminal cells
draw cursor
draw selection
handle keyboard input
handle mouse selection
handle copy/paste
handle scrollback
handle resize
manage font metrics
manage visible rows/columns
```

---

## 5.2 Terminal Emulator

This layer wraps `libvterm`.

`libvterm` understands terminal escape sequences.

Examples of things it understands:

```text
move cursor
clear screen
set foreground color
set background color
bold text
underline text
alternate screen
scroll region
```

Suggested class:

```cpp
class TerminalEmulator
{
public:
    TerminalEmulator(int rows, int cols);
    ~TerminalEmulator();

    void resize(int rows, int cols);
    void feedOutput(const QByteArray &data);

    TerminalCell cellAt(int row, int col) const;
    QPoint cursorPosition() const;

private:
    VTerm *m_vterm = nullptr;
    VTermScreen *m_screen = nullptr;
};
```

This class owns:

```text
VTerm
VTermScreen
terminal screen state
dirty rows
cursor position
cell attributes
```

---

## 5.3 Terminal Session

This layer connects your terminal to the shell.

It should use `Pty-Qt` / `libptyqt`.

Suggested class:

```cpp
class TerminalSession : public QObject
{
    Q_OBJECT

public:
    explicit TerminalSession(QObject *parent = nullptr);

    void startShell();
    void writeInput(const QByteArray &data);
    void resizePty(int rows, int cols);
    void terminate();

signals:
    void outputReceived(const QByteArray &data);
    void processExited(int exitCode);
};
```

Responsibilities:

```text
start bash/zsh/cmd/powershell
read output from PTY
write keyboard input to PTY
resize PTY when widget resizes
terminate shell process
restart shell process
```

Platform behavior:

```text
Linux/macOS:
  Pty-Qt uses PTY APIs like openpty/forkpty.

Windows:
  Pty-Qt uses ConPTY or equivalent Windows terminal backend.
```

---

## 5.4 Terminal Cell

The terminal screen is a grid of cells.

Each cell contains:

```text
character
foreground color
background color
bold flag
italic flag
underline flag
inverse flag
```

Suggested structure:

```cpp
struct TerminalCell
{
    QChar character = QChar(' ');

    QColor foreground;
    QColor background;

    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool inverse = false;
};
```

The widget paints each cell using `QPainter`.

---

## 5.5 Terminal Theme

Separate theme from logic.

Suggested structure:

```cpp
struct TerminalTheme
{
    QColor background = QColor("#0d1117");
    QColor foreground = QColor("#d4d4d4");
    QColor cursor = QColor("#ffffff");
    QColor selection = QColor("#264f78");

    QColor black = QColor("#000000");
    QColor red = QColor("#cd3131");
    QColor green = QColor("#0dbc79");
    QColor yellow = QColor("#e5e510");
    QColor blue = QColor("#2472c8");
    QColor magenta = QColor("#bc3fbc");
    QColor cyan = QColor("#11a8cd");
    QColor white = QColor("#e5e5e5");

    QFont font = QFont("JetBrains Mono", 11);
};
```

Later, add built-in themes:

```text
Nord
Dracula
Monokai
One Dark
VS Code Dark
GitHub Dark
```

---

## 6. Data Flow

## 6.1 Shell Output Flow

When the shell prints output:

```text
bash / powershell
        |
        v
PTY backend
        |
        v
TerminalSession::outputReceived(data)
        |
        v
TerminalEmulator::feedOutput(data)
        |
        v
libvterm parses escape sequences
        |
        v
VTermScreen updates terminal cells
        |
        v
TerminalWidget repaints dirty area
```

Example:

```text
Shell outputs:
  "\x1b[31mError\x1b[0m"

libvterm interprets:
  text = "Error"
  color = red

TerminalWidget paints:
  red Error text
```

---

## 6.2 Keyboard Input Flow

When the user types:

```text
User presses key
        |
        v
TerminalWidget::keyPressEvent()
        |
        v
Convert key to terminal input bytes
        |
        v
TerminalSession::writeInput(data)
        |
        v
PTY backend
        |
        v
Shell receives input
```

Example:

```text
User presses Enter
        |
        v
Terminal sends "\r"
        |
        v
Shell executes current command
```

---

## 6.3 Resize Flow

When the terminal widget resizes:

```text
Widget resized
        |
        v
Calculate rows and columns from font metrics
        |
        v
TerminalEmulator::resize(rows, cols)
        |
        v
TerminalSession::resizePty(rows, cols)
        |
        v
Shell/program receives new terminal size
```

This is required for programs like:

```text
vim
nano
htop
less
top
```

---

## 7. Suggested Project Structure

```text
src/
  terminal/
    TerminalWidget.h
    TerminalWidget.cpp

    TerminalEmulator.h
    TerminalEmulator.cpp

    TerminalSession.h
    TerminalSession.cpp

    TerminalCell.h
    TerminalTheme.h

    TerminalKeyMapper.h
    TerminalKeyMapper.cpp

    TerminalSelection.h
    TerminalSelection.cpp

    TerminalScrollback.h
    TerminalScrollback.cpp

thirdparty/
  libvterm/
  ptyqt/
```

---

## 8. Module Responsibilities

## 8.1 TerminalWidget

The main Qt UI class.

Responsibilities:

```text
paint terminal screen
handle keyboard input
handle mouse selection
handle copy/paste
handle scrolling
own TerminalEmulator
own TerminalSession
connect session output to emulator
trigger repaint
```

---

## 8.2 TerminalEmulator

Wrapper around `libvterm`.

Responsibilities:

```text
create VTerm instance
create VTermScreen
feed output into libvterm
convert VTermScreen cells to TerminalCell
track cursor
track dirty rows
handle terminal resize
```

---

## 8.3 TerminalSession

Wrapper around PTY backend.

Responsibilities:

```text
start shell
stop shell
send input to shell
receive output from shell
resize shell terminal
emit outputReceived
```

---

## 8.4 TerminalKeyMapper

Converts Qt key events into terminal input bytes.

Examples:

```text
Enter      -> "\r"
Backspace  -> "\x7f"
Tab        -> "\t"
Escape     -> "\x1b"
Arrow Up   -> "\x1b[A"
Arrow Down -> "\x1b[B"
Arrow Right-> "\x1b[C"
Arrow Left -> "\x1b[D"
Ctrl+C     -> "\x03"
Ctrl+D     -> "\x04"
Ctrl+L     -> "\x0c"
```

Suggested API:

```cpp
class TerminalKeyMapper
{
public:
    static QByteArray mapKeyEvent(QKeyEvent *event);
};
```

---

## 8.5 TerminalSelection

Handles text selection.

Responsibilities:

```text
mouse drag selection
double click word selection
copy selected text
clear selection
paint selection background
```

---

## 8.6 TerminalScrollback

Stores old lines that have scrolled out of the visible terminal area.

Responsibilities:

```text
store historical lines
limit max scrollback lines
support mouse wheel scrolling
support scrollbar
support copy from scrollback
```

Initial scrollback limit suggestion:

```text
10,000 lines
```

---

## 9. Development Milestones

Do not try to build everything at once.

Build it in small stages.

---

## Milestone 1: Basic PTY Session

Goal:

```text
Start a shell and receive raw output.
```

Tasks:

```text
integrate Pty-Qt
start bash on Linux
start powershell/cmd on Windows
print received output using qDebug()
send simple input to shell
```

Success test:

```text
Your app starts bash/powershell and receives shell prompt output.
```

---

## Milestone 2: Basic libvterm Integration

Goal:

```text
Feed shell output into libvterm.
```

Tasks:

```text
integrate libvterm
create VTerm
create VTermScreen
feed PTY output to vterm_input_write()
read basic cells from VTermScreen
print screen cells using qDebug()
```

Success test:

```text
The app can parse normal shell output into terminal cells.
```

---

## Milestone 3: Basic Painting

Goal:

```text
Draw terminal text in a Qt widget.
```

Tasks:

```text
create TerminalWidget
calculate char width and char height
paint visible cells using QPainter
use monospace font
paint background
paint foreground text
```

Success test:

```text
Shell prompt appears inside your Qt widget.
```

---

## Milestone 4: Keyboard Input

Goal:

```text
User can type commands.
```

Tasks:

```text
handle keyPressEvent
map Qt keys to terminal bytes
send bytes to TerminalSession
support Enter, Backspace, Tab, arrows, Ctrl+C
```

Success test:

```text
User can run:
  ls
  pwd
  cd
  git status
```

---

## Milestone 5: Resize Support

Goal:

```text
Terminal correctly resizes with the widget.
```

Tasks:

```text
calculate rows and columns
resize libvterm
resize PTY
trigger repaint
```

Success test:

```text
Running programs adapt to terminal size.
```

---

## Milestone 6: Colors and Attributes

Goal:

```text
Support colored terminal output.
```

Tasks:

```text
read foreground/background colors from VTermScreen
support bold
support underline
support inverse
paint colors correctly
```

Success test:

```bash
ls --color=auto
```

or:

```bash
printf "\033[31mRed Text\033[0m\n"
```

---

## Milestone 7: Cursor

Goal:

```text
Draw the terminal cursor.
```

Tasks:

```text
read cursor position from libvterm
paint block cursor
support focused/unfocused cursor style
blink cursor optionally
```

Success test:

```text
Cursor appears at the correct input position.
```

---

## Milestone 8: Scrollback

Goal:

```text
Support scrolling old output.
```

Tasks:

```text
store old lines
connect vertical scrollbar
handle wheelEvent
paint visible scrollback area
limit memory usage
```

Success test:

```bash
seq 1 1000
```

User should be able to scroll up and down.

---

## Milestone 9: Copy/Paste

Goal:

```text
User can copy and paste terminal text.
```

Tasks:

```text
mouse selection
copy selected cells as text
paste clipboard text into shell
sanitize paste if needed
support Ctrl+Shift+C
support Ctrl+Shift+V
```

Success test:

```text
Select text from terminal and copy it.
Paste commands into terminal.
```

---

## Milestone 10: Alternate Screen

Goal:

```text
Support full-screen terminal apps.
```

Programs like these use alternate screen:

```text
vim
nano
htop
less
```

Tasks:

```text
ensure libvterm alternate screen support works
avoid mixing alternate screen with scrollback incorrectly
handle enter/exit alternate screen
```

Success test:

```bash
vim
nano
htop
```

---

## 10. What to Avoid in the Beginning

Avoid these early:

```text
GPU rendering
ligatures
emoji shaping
complex Unicode clusters
tabs with multiple terminals
custom shell profile system
terminal hyperlinks
image protocol support
sixels
Wayland-specific hacks
SSH integration
```

Build the basic terminal first.

---

## 11. Common Mistakes

## Mistake 1: Using QTextEdit as the terminal

Bad idea for a real terminal.

`QTextEdit` is okay for logs, but not for real terminal emulation.

A real terminal needs a cell grid.

Use:

```text
QAbstractScrollArea + QPainter
```

---

## Mistake 2: Implementing escape sequences manually

Do not manually parse every ANSI/VT escape sequence.

Use:

```text
libvterm
```

Manual parsing becomes a huge project.

---

## Mistake 3: Ignoring PTY resize

If you only resize the widget but not the PTY, programs will not know the terminal size changed.

Always resize both:

```text
libvterm
PTY backend
```

---

## Mistake 4: Mixing output panel and terminal

Your IDE should have both:

```text
Output panel:
  QProcess logs, compiler output, test results

Terminal:
  real interactive shell
```

Do not force everything into the terminal.

---

## 12. IDE Integration

Recommended bottom panel layout:

```text
BottomPanel
  |
  +-- Output
  |
  +-- Problems
  |
  +-- Terminal
```

### Output Tab

Use `QProcess`.

Good for:

```text
compiler output
run output
judge output
error logs
test results
```

### Problems Tab

Use parsed compiler diagnostics.

Good for:

```text
file path
line number
column number
error/warning message
```

### Terminal Tab

Use your real terminal widget.

Good for:

```text
manual commands
git
npm
python REPL
shell scripts
interactive tools
```

---

## 13. Shell Selection

Default shells:

```text
Linux:
  /bin/bash
  /bin/zsh if configured

macOS:
  /bin/zsh
  /bin/bash

Windows:
  powershell.exe
  cmd.exe
  pwsh.exe if installed
```

Suggested config:

```cpp
struct TerminalProfile
{
    QString name;
    QString shellPath;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;
};
```

---

## 14. CMake Dependency Plan

Possible layout:

```cmake
add_subdirectory(thirdparty/libvterm)
add_subdirectory(thirdparty/ptyqt)

add_library(NordTerminal
    src/terminal/TerminalWidget.cpp
    src/terminal/TerminalEmulator.cpp
    src/terminal/TerminalSession.cpp
    src/terminal/TerminalKeyMapper.cpp
    src/terminal/TerminalSelection.cpp
    src/terminal/TerminalScrollback.cpp
)

target_link_libraries(NordTerminal
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        libvterm
        ptyqt
)
```

Exact target names may differ depending on how you integrate `libvterm` and `Pty-Qt`.

---

## 15. First Prototype Goal

The first working prototype should be very simple.

Do not aim for a perfect terminal immediately.

Target:

```text
Linux first:
  start /bin/bash
  show prompt
  allow typing commands
  show output
  support Enter, Backspace, Ctrl+C
  support basic colors
```

After Linux works, add Windows:

```text
Windows:
  start powershell.exe or cmd.exe
  use ConPTY through Pty-Qt
  test command input/output
```

---

## 16. Recommended Build Order

```text
1. TerminalSession
2. TerminalEmulator
3. TerminalWidget painting
4. Keyboard input
5. Resize support
6. Colors
7. Cursor
8. Scrollback
9. Selection
10. Copy/paste
11. Alternate screen
12. Multiple terminal tabs
13. Settings UI
```

---

## 17. Minimal Class Connection

Example object relationship:

```cpp
TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    m_session = new TerminalSession(this);
    m_emulator = new TerminalEmulator(24, 80);

    connect(m_session, &TerminalSession::outputReceived,
            this, [this](const QByteArray &data) {
                m_emulator->feedOutput(data);
                viewport()->update();
            });

    m_session->startShell();
}
```

Keyboard input:

```cpp
void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    QByteArray data = TerminalKeyMapper::mapKeyEvent(event);

    if (!data.isEmpty()) {
        m_session->writeInput(data);
    }
}
```

Resize:

```cpp
void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);

    int cols = viewport()->width() / m_charWidth;
    int rows = viewport()->height() / m_charHeight;

    m_emulator->resize(rows, cols);
    m_session->resizePty(rows, cols);
}
```

---

## 18. Final Architecture Summary

The final architecture should look like this:

```text
NordTerminal
  |
  +-- TerminalWidget
  |     |
  |     +-- paints cells
  |     +-- handles input
  |     +-- handles mouse/scrolling
  |
  +-- TerminalEmulator
  |     |
  |     +-- wraps libvterm
  |     +-- owns VTerm/VTermScreen
  |     +-- exposes TerminalCell grid
  |
  +-- TerminalSession
  |     |
  |     +-- wraps Pty-Qt/libptyqt
  |     +-- starts shell
  |     +-- reads/writes PTY
  |
  +-- TerminalTheme
  |
  +-- TerminalKeyMapper
  |
  +-- TerminalSelection
  |
  +-- TerminalScrollback
```

Recommended dependency stack:

```text
Qt Widgets
libvterm
Pty-Qt / libptyqt
```

Platform backend:

```text
Linux/macOS:
  PTY / openpty / forkpty

Windows:
  ConPTY
```

Do not directly depend on platform APIs in your widget.

Hide platform details inside `TerminalSession`.

---

## 19. Simple Rule

Keep this rule in mind:

```text
TerminalSession = talks to shell
TerminalEmulator = understands terminal codes
TerminalWidget = draws the terminal
```

If you follow this separation, the project will stay clean and maintainable.
