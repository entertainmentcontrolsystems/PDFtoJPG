# pdf_to_jpg.spec
# ----------------
# PyInstaller spec file for PDF → JPG Batch Converter.
#
# Build with:
#   build_windows.bat   (handles venv + dependency install automatically)
#
# Or manually:
#   pyinstaller pdf_to_jpg.spec
#
# Output:
#   dist\pdf_to_jpg\pdf_to_jpg.exe   (onedir bundle)

import sys
from pathlib import Path

block_cipher = None

# ---------------------------------------------------------------------------
# ANALYSIS — collect all imports and data files
# ---------------------------------------------------------------------------

a = Analysis(
    ["pdf_to_jpg.py"],
    pathex=[],
    binaries=[],
    datas=[
        # pdf2image ships helper scripts we need to bundle
        # (pdf2image locates poppler at runtime via PATH, not bundled here)
    ],
    hiddenimports=[
        # tkinter and its sub-modules are sometimes missed on Windows
        "tkinter",
        "tkinter.ttk",
        "tkinter.filedialog",
        "tkinter.messagebox",
        # pdf2image internals
        "pdf2image",
        "pdf2image.exceptions",
        "pdf2image.generators",
        # Pillow image format plugins used during JPEG save
        "PIL",
        "PIL.Image",
        "PIL.JpegImagePlugin",
        "PIL.JpegPresets",
        # Pillow feature detection
        "PIL.features",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        # Exclude heavy scientific libs that won't be imported
        "numpy",
        "pandas",
        "matplotlib",
        "scipy",
        "cv2",
        "torch",
        "tensorflow",
        # Exclude test frameworks
        "pytest",
        "unittest",
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

# ---------------------------------------------------------------------------
# PYZ — compressed pure-Python modules
# ---------------------------------------------------------------------------

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

# ---------------------------------------------------------------------------
# EXE — the launcher stub
# ---------------------------------------------------------------------------

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,          # onedir mode
    name="pdf_to_jpg",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,                  # no terminal window — GUI app
    disable_windowed_traceback=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    # icon="assets\\pdf_to_jpg.ico",   # uncomment + add .ico to enable
)

# ---------------------------------------------------------------------------
# COLLECT — assembles the onedir bundle in dist\pdf_to_jpg\
# ---------------------------------------------------------------------------

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[
        "vcruntime*.dll",
        "msvcp*.dll",
        "python*.dll",
        "*.pyd",
    ],
    name="pdf_to_jpg",
)
