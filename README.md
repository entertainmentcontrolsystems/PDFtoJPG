# ECS PDF Converter

A cross-platform desktop application for converting PDF pages to raster images and vector formats.

## Features

### Raster Output
- **JPEG** — adjustable quality (1-100)
- **PNG** — lossless, supports transparency
- **WebP** — modern format, lossy or lossless
- **TIFF** — archival quality
- **BMP** — uncompressed

### Vector Output
- **SVG (Vector)** — preserves vector geometry from PDF (via Poppler/pdftocairo)
- **SVG (Traced)** — traces raster images to vector paths (via Potrace/AutoTrace)
- **DXF** — CAD interchange format for AutoCAD, Vectorworks, LibreCAD, etc.

### Conversion Options
- DPI selection (72-600)
- JPEG/WebP quality (1-100)
- Page range selection (e.g. "1-5, 8, 11-13")
- Split multi-page PDFs (one file per page)
- Save to same folder as source or custom output directory
- Overwrite or skip existing files
- Open output folder when done
- Recursive folder scanning
- Drag-and-drop file input

## Building

### Prerequisites

**macOS:**
- Qt 6.6+ (`brew install qt`)
- CMake 3.21+ (`brew install cmake`)
- Xcode Command Line Tools
- Poppler (for vector output: `brew install poppler`)
- Potrace (for traced SVG: `brew install potrace`)

**Windows:**
- Qt 6.6+ (install via Qt Online Installer, include MSVC build)
- Visual Studio 2022+ (with C++ build tools)
- CMake 3.21+
- Poppler for Windows (for vector output)
- Inno Setup 6 (for installer: `brew install --cask inno-setup` or download from jrsoftware.org)

### Build (macOS)

```bash
cd PDFtoJPG
mkdir build && cd build
cmake ..
cmake --build .
```

The app bundle is at `build/ECS PDF Converter.app`.

### Build (Windows)

```cmd
cd PDFtoJPG
mkdir build && cd build
cmake -G "Visual Studio 17 2022" ..
cmake --build . --config Release
```

The executable is at `build/Release/ECS PDF Converter.exe` with Qt DLLs deployed alongside.

## Packaging

### macOS (.dmg)

```bash
cd build
macdeployqt "ECS PDF Converter.app" -dmg
```

Produces `ECS PDF Converter.dmg`.

### Windows (installer .exe)

```cmd
cd build\Release
windeployqt --release "ECS PDF Converter.exe"
:: Then compile installer\ecs_pdf_converter.iss with Inno Setup
```

## Technology

| Component | Technology |
|---|---|
| Language | C++20 |
| GUI framework | Qt6 Widgets |
| PDF rendering | Qt6 QPdfDocument (wraps PDFium) |
| Vector PDF→SVG | Poppler (pdftocairo CLI, called via QProcess) |
| Raster tracing | Potrace / AutoTrace (CLI, called via QProcess) |
| SVG→DXF | Custom C++ parser |
| Build system | CMake |

## License

This application is licensed under the MIT License.

Third-party libraries used:
- **Qt6** — LGPL v3 (dynamic linking) or commercial
- **Poppler** (pdftocairo) — GPL (called as separate CLI process)
- **Potrace** — GPL (called as separate CLI process)
- **AutoTrace** — LGPL v2.1 (called as separate CLI process)

All GPL-licensed tools are invoked as separate processes via QProcess to avoid
GPL contamination of the main application.

## History

v1.0 (2026-04) — Python + tkinter, PDF→JPG only, requires Poppler on PATH
v2.0 (2026-08) — C++/Qt6 rewrite, multi-format, standalone executable, vector output