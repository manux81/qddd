# qddd

qddd (Qt Debugger Development Dashboard) is a **Qt-based desktop application**
that provides a graphical interface to interact with **GDB via the Machine Interface (MI)**.

The project is primarily intended as a **development and learning tool**, focusing on
debugger internals, MI-based communication, and clean separation between UI and
debugging logic.

> ⚠️ Work in progress / experimental project

---

## ✨ Features

- Modern Qt-based graphical interface
- Interactive console for **GDB / MI** commands
- Debug session lifecycle management
- Real-time debugger output visualization
- Tree-based model for variables and structured data
- Architecture designed around UI / Debug backend separation

---

## 📸 Screenshot

<p align="center">
  <img src="docs/screenshots/main.png" width="700">
</p>

*Screenshot for illustrative purposes – UI is subject to change.*

---

## 🧩 Architecture Overview

The project is organized around a few core components:

- **DebugSession**
  - Manages the GDB process lifecycle
  - Handles MI communication
  - Emits Qt signals for output and debugger events

- **ConsoleWidget**
  - Interactive input/output console for GDB/MI commands
  - Displays raw debugger output

- **Variable Model**
  - Represents debugger variables as a tree structure
  - Supports hierarchical attachment of children nodes
  - Designed to reflect complex data layouts

This separation allows the UI to remain mostly independent from
the underlying debugging engine.

---

## 🛠️ Build Requirements

- Qt 5 or Qt 6
- C++17 compatible compiler
- GDB (with MI support)
- CMake (recommended)

---

## 🚀 Build & Run

Example build steps using CMake:

```bash
git clone https://github.com/manux81/qddd.git
cd qddd
mkdir build && cd build
cmake ..
make
./qddd


