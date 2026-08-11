# qddd — Qt Debugger Development Dashboard

qddd is an experimental debugger UI focused on
**visualizing complex runtime state**, not just stepping through code.

It is not a full IDE debugger replacement,
but a tool to **understand object graphs, pointers, and relationships**
when traditional variable views break down.

Instead of presenting variables as plain text,
qddd aims to provide a **structural and graphical view of runtime data**,
making it easier to understand relationships between objects,
pointers, and nested structures.

The project communicates with **GDB through the Machine Interface (MI)** and
is designed with a strict separation between the UI layer and the debugging backend.

> ⚠️ Work in progress / experimental project

---

## Why qddd?

Traditional debuggers are excellent for stepping through code,
but they often become hard to use when dealing with:

- deeply nested structures
- pointer-heavy data models
- complex object graphs
- runtime relationships between variables

qddd focuses on **understanding program state**, not only execution flow.

---

## ✨ Features

- Qt-based desktop UI
- Direct communication with **GDB / MI**
- Interactive debugger console
- Structured variable model
- Expandable hierarchical data visualization
- Graph-style rendering of related variables
- Clear separation between UI and debugger backend

---

## 📸 Screenshot

<p align="center">
  <img src="docs/screenshots/main.png" width="700">
</p>

*UI and layout are evolving as the project develops.*

---

## 🧩 Architecture Overview

The project is structured around a few core components:

### DebugSession
- Manages the GDB process lifecycle
- Handles MI protocol communication
- Translates debugger events into Qt signals

### ConsoleWidget
- Interactive MI command console
- Displays raw debugger input and output

### Variable Model
- Tree-based representation of debugger variables
- Designed to reflect real memory layouts
- Supports hierarchical expansion and future graph-based extensions

This architecture allows the UI to remain largely independent
from the underlying debugging engine.

### Runtime object graph

The graphical variables view represents pointer relationships by runtime
identity, rather than creating a separate object for every expression that
reaches it. A normalized address plus the concrete debugger type forms the
stable object key. Consequently, aliases share one node, cycles terminate when
an already-known key is encountered, and opening the same object through a
different pointer reuses its saved position. If an address is unavailable,
qddd uses an expression-based fallback key and treats it as less stable.

For example:

```cpp
struct Node {
    int value;
    Node *next;
};
```

If `head` and `selected` point to the same `Node`, the graph contains one
runtime object and two labeled references. A refresh is diffed by object,
member, and logical reference identity: new objects, changed scalar members,
and retargeted edges can be highlighted without rebuilding identity from UI
text. Recursive expansion is bounded (currently eight levels in the context
menu) and identity-based traversal provides cycle protection in the graph
model.

---

## 🛠️ Build Requirements

- Qt 5 or Qt 6
- C++17 compatible compiler
- GDB with MI support
- CMake

## 🎯 Remote targets (gdbserver / SEGGER J-Link)

qddd can connect to a remote target through GDB using `-target-select remote host:port`.

- `File → Settings… → Target`
  - `Type`: `Remote gdbserver` or `J-Link`
  - `Remote host` / `Remote port`: endpoint of your GDB server
- Start your GDB server separately (example, SEGGER):
  - `JLinkGDBServer -if SWD -speed auto -port 2331 -device <MCU>`
- Open your local ELF (`File → Open Program…`) to load symbols, then `Run` to continue execution on the target.

### MPLAB PICkit Basic / dsPIC

Hardware Debug profiles also support the MPLAB MDB command-line debugger used
by PICkit Basic and dsPIC targets. Select `MPLAB MDB / PICkit Basic`, then set:

- the `mdb.sh` (`mdb.bat` on Windows) executable installed with MPLAB X;
- the exact dsPIC device name;
- the hardware tool (`PICKitBasic` or `PICkit5`);
- an optional probe serial number and the XC16 ELF image.

QDDD launches MDB directly and maps Run, Continue, Halt, Step Into and Step Over
to documented MDB commands. The dsPIC target must use external power because
PICkit Basic does not power it.

---

## 🚀 Build & Run

```bash
git clone https://github.com/manux81/qddd.git
cd qddd
mkdir build && cd build
cmake ..
make
./qddd
