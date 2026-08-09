#!/bin/bash
# build_macos.sh — Build ECS PDF Converter on macOS
# Produces: build/ECS PDF Converter.app  and  build/ECS PDF Converter.dmg

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
APP_NAME="ECS PDF Converter"

echo "============================================================"
echo "  ECS PDF Converter — macOS Build"
echo "============================================================"
echo ""

# ── 1. Check prerequisites ──────────────────────────────────────
echo "[1/5] Checking prerequisites..."

if ! command -v cmake &>/dev/null; then
    echo "ERROR: CMake not found. Install with: brew install cmake"
    exit 1
fi

if [ ! -d "/opt/homebrew/Cellar/qt" ]; then
    echo "ERROR: Qt6 not found. Install with: brew install qt"
    exit 1
fi

QT_VERSION=$(ls /opt/homebrew/Cellar/qt/ | head -1)
echo "  CMake: $(cmake --version | head -1)"
echo "  Qt: $QT_VERSION"
echo "  Compiler: $(clang++ --version | head -1)"
echo ""

# ── 2. Clean build ───────────────────────────────────────────────
echo "[2/5] Cleaning previous build..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
echo "  Done."
echo ""

# ── 3. CMake configure ───────────────────────────────────────────
echo "[3/5] Configuring..."
cd "$BUILD_DIR"
cmake .. 2>&1 | tail -5
echo ""

# ── 4. Build ────────────────────────────────────────────────────
echo "[4/5] Building..."
cmake --build . --config Release 2>&1
echo ""

# ── 5. Package .dmg ──────────────────────────────────────────────
echo "[5/5] Packaging..."

APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
DMG_PATH="$BUILD_DIR/$APP_NAME.dmg"

# Find macdeployqt
MACDEPLOYQT=$(find /opt/homebrew/Cellar/qt/$QT_VERSION/bin -name "macdeployqt" 2>/dev/null | head -1)
if [ -z "$MACDEPLOYQT" ]; then
    MACDEPLOYQT="/opt/homebrew/bin/macdeployqt"
fi

if [ -x "$MACDEPLOYQT" ]; then
    echo "  Running macdeployqt..."
    "$MACDEPLOYQT" "$APP_BUNDLE" -dmg 2>&1 || true
    echo "  DMG: $DMG_PATH"
else
    echo "  WARNING: macdeployqt not found. App bundle created but no .dmg."
    echo "  App: $APP_BUNDLE"
fi

echo ""
echo "============================================================"
echo "  BUILD SUCCEEDED"
echo ""
echo "  App: $APP_BUNDLE"
if [ -f "$DMG_PATH" ]; then
    echo "  DMG: $DMG_PATH"
fi
echo ""
echo "  To bundle CLI tools (pdftocairo, potrace) for vector output:"
echo "    mkdir -p '$APP_BUNDLE/Contents/Resources/tools'"
echo "    cp \$(which pdftocairo) '$APP_BUNDLE/Contents/Resources/tools/'"
echo "    cp \$(which potrace) '$APP_BUNDLE/Contents/Resources/tools/'"
echo "============================================================"