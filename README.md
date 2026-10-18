# VLZ Media Player

A minimal, modern media player with embedded video playback, built with Qt6 and libvlc.

## Features

- **Embedded video playback** - Video plays directly in the main window (no separate window)
- **Drag & drop** - Drop media files onto the window to play
- **Full keyboard shortcuts** - Space (play/pause), arrows (seek), Ctrl+arrows (prev/next), M (mute), F (fullscreen)
- **Modern dark theme** - Clean, minimal interface
- **Volume control** - Slider with mute toggle
- **Seek controls** - ±10s buttons
- **Fullscreen support** - Both video and interface
- **Drag & drop support** - Drop files to add to queue
- **Multiple formats** - mp4, mkv, avi, webm, mp3, flac, wav, ogg, flv, and more

## Screenshots

![VLZ Media Player](docs/screenshot.png)

## Installation

### AppImage (Linux, recommended)

```bash
# Download and run
wget https://github.com/TryBuildMe/VLZ/releases/latest/download/VLZ-0.1.0-x86_64.AppImage
chmod +x VLZ-0.1.0-x86_64.AppImage
./VLZ-0.1.0-x86_64.AppImage
```

### Debian/Ubuntu (.deb)

```bash
wget https://github.com/TryBuildMe/VLZ/releases/latest/download/VLZ_0.1.0_amd64.deb
sudo dpkg -i VLZ_0.1.0_amd64.deb
vlz-gui
```

### From Source

```bash
# Build vlz CLI
cd src
make

# Run GUI
python3 VLZ-gui

# Or run CLI directly
./vlz file.mp4
```

## Requirements

- **libvlc5** (≥ 3.0) - Video playback backend
- **Python 3.8+** - For GUI
- **PyQt6** - GUI framework (`pip install PyQt6`)
- **libglib2.0-0** - For libvlc
- **X11 or Wayland** - Display server (uses XWayland on Wayland)

## Usage

### GUI

```bash
# Launch GUI
vlz-gui

# Open file via GUI
vlz-gui /path/to/video.mp4

# Drag & drop files onto the window
```

### CLI

```bash
# Play file
vlz file.mp4

# Play multiple files (queue)
vlz file1.mp4 file2.mp4

# Headless playback
vlz --intf=dummy --aout=dummy file.mp4

# With options
vlz --volume=50 --no-control file.mp4
```

### Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Space | Play/Pause |
| ← / → | Seek -10s / +10s |
| Ctrl+← / Ctrl+→ | Previous / Next track |
| ↑ / ↓ | Volume up / down |
| M | Mute toggle |
| F | Fullscreen video |
| F11 | Fullscreen interface |
| Ctrl+O | Open file |
| Ctrl+L | Open location (URL) |
| Ctrl+Q | Quit |

## Building from Source

### Build C CLI (`vlz`)

```bash
cd src
make
# Binary: ./vlz
```

### Build Python GUI

```bash
# Install dependencies
pip install PyQt6

# Run
python3 VLZ-gui
```

### Build AppImage

```bash
# Requires: appimagetool
wget https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage
chmod +x appimagetool-x86_64.AppImage

# Build
mkdir -p VLZ.AppDir/usr/bin
cp src/VLZ-gui VLZ.AppDir/usr/bin/
cp src/vlz VLZ.AppDir/usr/bin/
cp VLZ.png VLZ.AppDir/
# ... (see build script)
./appimagetool VLZ.AppDir VLZ-0.1.0-x86_64.AppImage
```

### Build .deb

```bash
# Requires: dpkg-dev
mkdir -p pkg/usr/local/bin pkg/usr/share/applications pkg/usr/share/icons/hicolor/256x256/apps
cp src/vlz pkg/usr/local/bin/
cp src/VLZ-gui pkg/usr/local/bin/
cp VLZ.desktop pkg/usr/share/applications/
cp VLZ.png pkg/usr/share/icons/hicolor/256x256/apps/
dpkg-deb --build pkg VLZ_0.1.0_amd64.deb
```

## Architecture

```
VLZ/
├── src/
│   ├── vlz.c          # C CLI player (libvlc direct)
│   ├── Makefile       # Build script
│   ├── mktestdata.sh  # Test media generator
│   └── VLZ-gui        # Python Qt6 GUI (embedded video)
├── dist/
│   ├── VLZ_0.1.0_amd64.deb
│   └── VLZ-0.1.0-x86_64.AppImage
└── docs/
    └── README.md
```

### Core Components

1. **vlz (C)** - Minimal libvlc wrapper, queue-based playback, stdin controls
2. **VLZ-gui (Python/Qt6)** - Embedded video via `libvlc_media_player_set_xwindow()`, direct libvlc ctypes bindings

## REA Inspection

The vlz CLI was reverse-engineered from `/usr/bin/vlc` (VLC 3.0.24) using Ghidra 12.1.4:

- Startup contract mirrored: SIGPIPE/SIG_IGN, unsetenv("DESKTOP_STARTUP_ID"), root refusal, setlocale, isatty banner, signal mask, --no-ignore-config argv injection, exit handler, app-id/user-agent
- Key finding: VLC 3.0's `libvlc_audio_get/set_volume` take `media_player_t*` not `instance_t*` (fixed in vlz declarations)
- Evidence IDs: `ev_fc3a691a...`, `ev_ba1d3441...`, `ev_13aacd96...`, `ev_d9220e89...`, `ev_9d2089a8...`

## License

MIT License - see [LICENSE](LICENSE) for details.

## Author

Developed by: **TryBuildMe**

## Changelog

### v0.1.0 (2026-10-18)
- Initial release
- Embedded video playback in GUI
- Drag & drop support
- Full keyboard shortcuts
- Dark theme
- AppImage and .deb packages
- C CLI with queue-based playback
- REA-based VLC 3.0.24 inspection