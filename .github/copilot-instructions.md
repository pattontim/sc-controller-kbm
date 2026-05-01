# Copilot Instructions for SC Controller

## Project Overview
- **SC Controller** is a user-mode driver and input mapper for Steam Controller, DS4, and similar devices. The codebase is a work-in-progress C port targeting Windows, BSD, Linux, and Android. The current focus is on basic keyboard/mouse input emulation, with minimal GUI and OSD features.
- Major components:
  - `src/daemon/main.c`: Entry point for the daemon process. Handles command-line parsing, daemonization, and process control (start/stop/restart/debug).
  - `drivers/`: Contains platform-specific driver binaries required for device detection and input handling.
  - `python/`: Contains Python scripts and tests (see below).
  - `default_profiles/`, `profile_examples/`: Example and default controller profiles.
  - `glade/`: UI definitions (not currently active in WIP branch).
  - `scc/`: DO NOT WORK ON THIS CODE IT IS THE OLD PYTHON CODEBASE. Contains the Python implementation at the time when the C port was started

## Build & Run Workflows
- **Linux:**
  - `meson build; ninja -C build; ninja -C build scc-daemon`
- **Windows:**
  - Requires MinGW and dependencies (see README.md for full list).
  - `meson build; ninja -C build scc-daemon`
  - Copy driver DLLs from `make-win32-release.exe` output to `drivers/`.
- **Daemon Control:**
  - Run with: `scc-daemon [profile_name] {start,stop,restart,debug}`
  - On Windows, double-clicking the binary runs in debug mode.

## Testing
- Python tests use `pytest`.
  - Run all tests: `./python/run-py-tests.sh`

## Key Patterns & Conventions
- **Command-line parsing** uses `argparse` (see `main.c`). Profile names are positional arguments; only one profile is accepted.
- **Daemonization** (Linux/Unix) is handled via double-fork in `sccd_daemonize()`.
- **Process control** (start/stop/restart/debug) is managed in `main.c` via a `Mode` enum and switch statement.
- **Profile handling:** Profiles are loaded from `default_profiles/` or user-supplied files. Profile import is supported via drag-and-drop (see `profile_examples/README.md`).
- **Logging:** Uses custom macros (`LERROR`, `LOG`, `DEBUG`) from `scc/utils/logging.h`.
- **Windows-specific:** Uses `_spawnl` for process creation and `SCCClient` for daemon communication.

## Integration Points
- **External dependencies:**
  - MinGW toolchain (Windows)
  - Meson build system
  - Python (for tests and some scripts)
  - GTK3, libusb, and other libraries (see README.md)
- **Drivers:** All platform-specific drivers must be present in `drivers/` for device detection.

## Examples
- To start the daemon with a profile: `scc-daemon XBox Controller start`
- To stop the daemon: `scc-daemon stop`
- To run in debug mode: `scc-daemon debug`

## References
- See `README.md` for build instructions and platform notes.
- See `src/daemon/main.c` for daemon lifecycle and command-line handling.
- See `python/tests/README.md` for test workflow.
- See `profile_examples/README.md` for profile import instructions.

---
**For AI agents:**
- Always check for platform-specific logic (`#ifdef _WIN32`) in C files.
- When editing build or daemon logic, update both Linux and Windows code paths.
- Use the provided logging macros for diagnostics.
- Respect the single-profile convention in CLI parsing.
- When adding new drivers or profiles, update the relevant directories and document in README.md.
