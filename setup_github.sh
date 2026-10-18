#!/bin/bash
# GitHub Release Setup Script
# Run this after creating a GitHub repository

set -e

REPO_NAME="VLZ"
GITHUB_USER="TryBuildMe"
VERSION="0.1.0"

echo "=== VLZ GitHub Release Setup ==="
echo ""

# Check if git is initialized
if [ ! -d ".git" ]; then
    echo "Initializing git repository..."
    git init
    git branch -m main
fi

# Add all files
echo "Adding files..."
git add .

# Create initial commit
echo "Creating commit..."
git commit -m "Release v${VERSION}: Initial release

- Embedded video playback in Qt6 GUI
- Drag & drop support
- Full keyboard shortcuts
- Dark theme
- C CLI (vlz) with queue-based playback
- AppImage and .deb packages
- REA-based VLC 3.0.24 inspection findings"

# Add remote (user needs to create repo first)
echo ""
echo "=== Next Steps ==="
echo "1. Create repository on GitHub: https://github.com/new"
echo "   Repository name: ${REPO_NAME}"
echo "   Owner: ${GITHUB_USER}"
echo "   Description: Minimal media player with embedded video (Qt6 + libvlc)"
echo "   Public/Private: Your choice"
echo "   DO NOT initialize with README, .gitignore, or license"
echo ""
echo "2. Run these commands:"
echo "   git remote add origin https://github.com/${GITHUB_USER}/${REPO_NAME}.git"
echo "   git push -u origin main"
echo ""
echo "3. Create release:"
echo "   git tag -a v${VERSION} -m \"Release v${VERSION}\""
echo "   git push origin v${VERSION}"
echo ""
echo "4. On GitHub, go to Releases → Create new release:"
echo "   Tag: v${VERSION}"
echo "   Title: VLZ Media Player v${VERSION}"
echo "   Upload assets:"
echo "     - dist/VLZ_0.1.0_amd64.deb"
echo "     - dist/VLZ-0.1.0-x86_64.AppImage"
echo "   Description: See RELEASE_NOTES.md"

# Create release notes
cat > RELEASE_NOTES.md << 'EOF'
# VLZ Media Player v0.1.0

## Features
- **Embedded video playback** - Video plays directly in the main window
- **Drag & drop** - Drop media files onto the window to play
- **Full keyboard shortcuts** - Space, arrows, Ctrl+arrows, M, F, F11
- **Modern dark theme** - Clean, minimal interface
- **Volume control** - Slider with mute toggle
- **Seek controls** - ±10s buttons
- **Fullscreen support** - Both video and interface
- **Multiple formats** - mp4, mkv, avi, webm, mp3, flac, wav, ogg, and more

## Installation

### AppImage (Recommended)
```bash
wget https://github.com/TryBuildMe/VLZ/releases/download/v0.1.0/VLZ-0.1.0-x86_64.AppImage
chmod +x VLZ-0.1.0-x86_64.AppImage
./VLZ-0.1.0-x86_64.AppImage
```

### Debian/Ubuntu
```bash
wget https://github.com/TryBuildMe/VLZ/releases/download/v0.1.0/VLZ_0.1.0_amd64.deb
sudo dpkg -i VLZ_0.1.0_amd64.deb
vlz-gui
```

## Usage
- Launch `vlz-gui` for the graphical interface
- Launch `vlz file.mp4` for CLI playback
- Drag & drop files onto the GUI window
- Keyboard shortcuts: Space (play/pause), arrows (seek), Ctrl+arrows (prev/next), M (mute), F (fullscreen)

## Requirements
- libvlc5 (≥ 3.0)
- Python 3.8+ with PyQt6
- X11 or Wayland (uses XWayland on Wayland)

## Changelog
- v0.1.0: Initial release
EOF

echo "Release notes created: RELEASE_NOTES.md"
echo ""
echo "Done! Run the git commands above to push to GitHub."