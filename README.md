# AI Reader

LLM-powered desktop reader for academic papers. See `SPEC.md` for the full design.

## Status

**Milestone 1 — Skeleton.** A Qt Quick application that opens PDFs (drag-drop or file picker) and renders them with `PdfMultiPageView`. Encrypted PDFs trigger a password dialog.

Subsequent milestones (translation pane, summary, TOC, chat, VL path, packaging) are not yet implemented.

## Prerequisites

Run `./install-deps.sh` once. Installs CMake, Ninja, GCC, Qt 6.4 (Core/Gui/Qml/Quick/QuickControls2/Pdf/PdfQuick/Network/Sql), QtKeychain, cmark-gfm, OpenSSL, libsecret via apt. Tested on Ubuntu 24.04.

## Build

```sh
cmake -B build -G Ninja
cmake --build build
```

## Run

```sh
./build/ai-reader
```

Then drag a `.pdf` into the window, or click **Open…**. For encrypted PDFs the app prompts for a password.

## Layout

```
ai-reader/
├── CMakeLists.txt
├── install-deps.sh
├── SPEC.md
├── src/
│   └── main.cpp
└── qml/
    ├── Main.qml
    └── PasswordDialog.qml
```
