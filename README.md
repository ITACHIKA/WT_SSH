# WT SSH Manager (`wtssh`)

A native **C++ Text UI** SSH session manager for Windows Terminal.

## What it does

- Save frequently used SSH targets (like PuTTY saved sessions).
- Open SSH sessions from an interactive terminal UI.
- Never store passwords.
- Build and distribute as a standalone binary (`wtssh` / `wtssh.exe`).

## Features

- Full-screen Text UI
- Keyboard controls:
  - `↑ / ↓` move selection
  - `A` add server
  - `D` delete server
  - `C` connect
  - `Q` quit
- ANSI color styling for readability
- Host data stored in:
  - `~/.wt_ssh_manager/hosts.db`

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

Binary location:
- Linux/macOS: `build/wtssh`
- Windows: `build/Release/wtssh.exe` (or `build/wtssh.exe`, depending on generator)

## Run

Linux/macOS:

```bash
./build/wtssh
```

Windows PowerShell:

```powershell
.\build\Release\wtssh.exe
```

If `wtssh.exe` is in your `PATH`, just run:

```powershell
wtssh
```

## Security notes

- Passwords are never stored.
- If configured, only the private key **path** is saved, not key content.
- Authentication is handled by your system `ssh` client.
