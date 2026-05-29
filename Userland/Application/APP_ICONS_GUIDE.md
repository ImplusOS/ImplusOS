# Application Icons Guide - ImplusOS

This guide explains how to add custom PNG icons to your ImplusOS applications for display in the WindowManager start menu and taskbar.

## Overview

When the WindowManager initializes, it automatically scans the `Resource` folders of all registered applications (both SystemApps and UserApps) for a file named `App.png`. If found, the icon is displayed in:
- Start menu (32x32 pixels)
- Taskbar buttons (scaled appropriately)

If no `App.png` file is found, the WindowManager falls back to displaying emoji icons (as defined in the application list).

## Adding an Icon to Your Application

### 1. Create the Resource Directory

Ensure your application directory contains a `Resource` subdirectory:

```
Userland/UserApps/com_ImplusOS_yourapp/
├── Makefile
├── main.c
├── yourapp.c
├── yourapp.h
└── Resource/
    └── App.png
```

### 2. Prepare Your Icon File

- **Filename**: `App.png`
- **Format**: PNG (RGBA, 8-bit per channel)
- **Recommended Size**: 32×32 pixels (icons will be scaled to this if different)
- **Transparency**: Full alpha channel support (PNG RGBA)

### 3. Add Icon to Your Repository

Place the `App.png` file in your application's `Resource` folder:

```bash
cp your_icon.png Userland/UserApps/com_ImplusOS_yourapp/Resource/App.png
```

### 4. Rebuild

Simply rebuild the system:

```bash
make clean
make
```

The WindowManager will automatically detect and load your `App.png` during startup.

## Icon Display Locations

### Start Menu
- **Size**: 24×24 pixels (rendered in 32×32 space)
- **Location**: Left side of each app entry
- **Fallback**: Emoji icon if PNG not found

### Taskbar
- **Size**: 16×16 pixels
- **Location**: Left of window title in taskbar button
- **Fallback**: No icon (just window title text)

### Window Title Bar
- **Size**: 16×16 pixels
- **Location**: Left of window title
- **Source**: Window-specific icon (set via syscall), not App.png
- **Fallback**: Default icon if no window icon set

## Icon Creation Tips

### Design Guidelines
1. Use a square canvas (e.g., 64×64, 128×128, 256×256)
2. Add padding around your icon for visual balance
3. Use high-contrast colors for visibility
4. Test at the target display sizes (32×32, 16×16)
5. Ensure the alpha channel is properly configured for transparency

### Tools
- **GIMP**: Full-featured image editor with PNG support
- **ImageMagick**: Command-line image processing
  ```bash
  convert input.png -resize 32x32 -background none App.png
  ```
- **Inkscape**: Vector graphics editor with PNG export
- **Online tools**: Pixlr, Photopea

## Automatic Icon Scanning

The WindowManager performs automatic icon discovery during initialization (`wm_scan_app_icons()`):

1. Scans all applications defined in the start menu
2. For each app path, constructs the resource path:
   - SystemApps: `/Userland/SystemApps/<app_name>/Resource/App.png`
   - UserApps: `/Userland/UserApps/<app_name>/Resource/App.png`
3. Loads PNG files using stb_image
4. Scales icons to 32×32 if necessary (nearest-neighbor scaling)
5. Stores icon pixel data in the application entry

## Technical Details

### Icon Loading Process
- **File I/O**: Uses ImplusOS file syscalls
- **PNG Decoding**: Uses stb_image library
- **Memory**: Icons are allocated on heap and remain loaded during WM runtime
- **Alpha Blending**: Full RGBA support with alpha channel blending

### Fallback Behavior
- Missing `App.png`: Falls back to emoji icon
- Corrupt PNG: Silently fails, uses emoji
- Unsupported format: PNG only (not JPEG, BMP, etc.)

## Examples

### Minimal Example
```
Userland/UserApps/com_ImplusOS_myapp/
├── Makefile
├── main.c
└── Resource/
    └── App.png           # 32×32 PNG file
```

### Icon Scaling
If your source icon is 256×256:
```bash
# Using ImageMagick
convert source_icon.png -resize 32x32 \
  -background none -gravity center -extent 32x32 \
  Userland/UserApps/com_ImplusOS_myapp/Resource/App.png
```

## Troubleshooting

### Icon Not Appearing
1. Verify filename is exactly `App.png` (case-sensitive)
2. Ensure file is valid PNG format
3. Check file path matches pattern: `/Userland/{SystemApps|UserApps}/*/Resource/App.png`
4. Rebuild WindowManager or restart system
5. Check serial debug output for file loading errors

### Icon Appears Distorted
1. Ensure image is square (not rectangular)
2. Use PNG format with proper color space
3. Test with a simple solid-color image first
4. Verify alpha channel is not inverted

### Performance Issues
- Icons are loaded once at WM startup
- Memory usage: ~4KB per icon (32×32 RGBA)
- No runtime performance impact

## Example: Creating Icon with GIMP

1. Create new 32×32 image (RGB with alpha)
2. Design your icon
3. Export as PNG:
   - File → Export As
   - Set filename to `App.png`
   - PNG export options:
     - Interlacing: Off
     - Compression level: 9
     - Save background color: unchecked
   - Export
4. Place in `Resource/App.png`

## See Also

- [ImplusOS Architecture Documentation](../../Docs/Architecture/)
- [Userland Specification](../../Docs/Architecture/Userland_Specification.md)
- [WindowManager Source](WindowManager.c)
- [stb_image Documentation](https://github.com/nothings/stb)
