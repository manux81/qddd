# Execution History / Time-Travel Architecture

QDDD is evolving from a conventional Qt debugger frontend into a generic
**time-travel debugger and execution-history analyzer for emulated embedded
systems**. This document describes the QDDD-side architecture, what works
today, and what a future backend (e.g. QEMU deterministic record/replay)
must provide.

Scope rule: QDDD owns UX, the execution-history model, the session
abstraction, the timeline, event storage, navigation, source correlation and
backend interfaces. QDDD does **not** own machine emulation and this
repository contains **no QEMU/STM32 changes**.

## Concepts

```text
                    QDDD GUI (HistoryView, timeline, details)
                       |
                HistorySession (cursor, selection, store, signals)
                       |
        +--------------+---------------+
        |              |               |
   LiveDebugger   HistoryBackend    SymbolEngine
  (GDB/MI stops)  (capabilities)    (DWARF, existing)
```

- `HistorySession` (`src/history/HistorySession.h`) is GUI-independent. It
  owns the event store, the time cursor, the selection and the live/history
  mode. Widgets talk to it, never to GDB/QEMU objects.
- `HistoryBackend` (`src/history/HistoryBackend.h`) exposes whatever history
  the debugger/emulator interface can actually provide. Every operation
  declares itself through `HistoryCapabilities`; the UI disables what the
  backend cannot do instead of simulating it.
- `TraceEvent` / `ExecutionState` (`src/history/HistoryTypes.h`) are the
  generic models. They know CPU, interrupt, exception, memory-access and
  peripheral *concepts*, but no MCU, peripheral map or vector table.
- `TimelineTrackProvider` (`src/history/TimelineTrack.h`) makes the timeline
  extensible. Built-in tracks: CPU, Interrupts, Exceptions, Breakpoints,
  Watchpoints, Memory, Peripherals, User Events. Future domains (CAN, UART,
  SPI, GPIO, DMA, timers, RTOS tasks, malloc/free, SDIO, network) add
  providers; the timeline widget is never edited for a new domain.
- `TargetDescriptor` (`src/history/TargetDescriptor.h`) is the boundary for
  all target-specific decoding (interrupt names, peripheral names).
  `GenericTargetDescriptor` decodes nothing; `ArmCortexMTargetDescriptor`
  decodes only architecture-fixed ARM system exceptions (Reset … SysTick)
  and reports external vectors as `IRQ<n>`. SoC vector names (e.g. STM32
  `CAN1_RX0`) belong in an SoC-specific descriptor, never in core code.

## WORKING NOW

- **Stop recording.** `GdbStopHistoryBackend` translates every live GDB stop
  (`stoppedAt` / `stoppedAtAddress`) into a `TraceEvent` on the CPU track.
  No QEMU, QMP or protocol changes were needed.
- **Execution History dock.** Timeline with zoom (wheel), horizontal pan
  (drag), current-time cursor, event markers, click-to-select, click-to-seek,
  hover tooltips and keyboard navigation (Left/Right/Home/End).
- **Transport controls.** Jump to start / step back / step forward /
  continue back / continue forward / jump to end-live. With the current
  backend, steps navigate recorded stops and both continue buttons are
  disabled (no reverse-continue support); tooltips say so.
- **Event details.** Time, type, PC/address, symbol, source file:line,
  function and metadata for the selected event.
- **Source preview sync.** Selecting history shows the recorded source
  location in the existing source view. The live target is untouched; the
  next live stop navigates back. A banner in the History view states this.
- **Capability-driven reverse UX.** Program menu actions (Reverse Step Into /
  Over / Continue, Previous Breakpoint, Find Previous Write) enable only
  when supported. Reverse GDB execution still works exactly as before when
  the GDB session reports it.
- **Recording metadata.** `HistoryRecordingMetadata` is a small versioned
  JSON schema (`.qddd-history` concept) carrying format, version, target,
  backend, firmware path/build-id and capabilities. Backend-owned
  trace/checkpoint blobs are referenced, not embedded.
- **Comparison & flow models.** `findFirstDivergence()` compares two recorded
  event streams; `buildCallTree()` folds FunctionEnter/Exit events into a
  call tree rendered as a placeholder in the History view.

## ARCHITECTURE READY, BACKEND SUPPORT STILL REQUIRED

| Feature | State | Missing backend capability |
| --- | --- | --- |
| `seek()` to a historical state | Cursor/selection move only | `Seek` + state-restore transport |
| Reverse Step / Reverse Continue (timeline) | Disabled | `ReverseStep` / `ReverseContinue` |
| Instruction history | Model + CPU track ready | `InstructionHistory` event stream |
| Interrupt / peripheral events | Tracks + model ready | `InterruptEvents` / `PeripheralEvents` |
| Find Previous/Next Write, value history | Model + disabled menu actions | `MemoryWriteHistory` |
| Register snapshot sync in views | `ExecutionState` abstraction ready; source preview works | Backends must capture registers per event |
| Deterministic replay | `DeterministicReplay` flag defined | Replay transport with identical results |
| Run A vs Run B comparison engine | `ExecutionRecording`, `DivergencePoint`, first-divergence scan | Rich multi-run recordings |
| `.qddd-history` checkpoint blobs | Metadata schema only | Backend-owned checkpoint format |

Nothing above is faked: unsupported operations return `false`/`nullopt` and
the UI disables them with explanatory tooltips.

## Extension points (no GUI changes needed)

- New backend: subclass `HistoryBackend` (see `QemuHistoryBackend` sketch
  below), `HistorySession::setBackend()` it. Capabilities propagate via
  `capabilitiesChanged`.
- New track: implement `TimelineTrackProvider`, hand it to
  `HistoryTimelineWidget::setTracks()`.
- New target: implement `TargetDescriptor` (optionally a peripheral-event
  provider feeding `Peripheral` events).
- New views: consume `HistorySession::executionStateChanged` /
  `selectedEventChanged` / `currentTimeChanged`; they already carry the
  common `ExecutionState` abstraction shared by live and historical states.

```cpp
// Future deterministic backend connects here without touching the GUI.
class QemuHistoryBackend : public HistoryBackend {
    // capabilities(): Recording | Seek | ReverseStep | ReverseContinue |
    //                 InstructionHistory | MemoryWriteHistory |
    //                 InterruptEvents | PeripheralEvents | DeterministicReplay
};
```

## QEMU requirements (not implemented here)

True deterministic reverse debugging needs QEMU-side interfaces QDDD cannot
provide. The list below is complete to the best of current knowledge; each
item maps to one `HistoryCapability`:

1. **Deterministic record/replay session control** — start/stop recording,
   save/load a replay image, go to an exact point (`Seek`,
   `DeterministicReplay`).
2. **Reverse execution primitives** — reverse-step instruction, reverse-step
   line, reverse-continue, previous-breakpoint (`ReverseStep`,
   `ReverseContinue`).
3. **Instruction trace stream** — PC + disassembly + timestamp/cycle count
   per retired instruction, ring-buffered (`InstructionHistory`).
4. **Interrupt/exception notifications** — vector number, entry/exit, priority
   (`InterruptEvents`). QDDD decodes numbers via `TargetDescriptor`.
5. **Peripheral event stream** — MMIO read/write with address, size, value
   and initiator (`PeripheralEvents`); QDDD names them via `TargetDescriptor`.
6. **Memory watchpoints with old/new values** — watch range, trap with
   previous value, queryable write log (`MemoryWriteHistory`).
7. **Register/checkpoint snapshots** — full CPU + selected peripheral state
   at bookmarks for instant `seek()` (`Seek` at interactive speed).
8. **Stable time base** — guest cycle count or monotonic virtual time
   exposed per event so `TimePoint` becomes target time instead of
   wall-clock time.

Until these exist, QDDD records live GDB stops and navigates them honestly.
