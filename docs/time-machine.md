# Time Machine UX

One coherent concept over execution history: the present, checkpoints, and
older history. Selecting a history point either inspects its recorded
snapshot or — with a seek-capable backend — replays the inferior there.
The UI never exposes recorder internals (`record full`, `btrace`, QEMU
replay) outside the Advanced backend-capabilities panel.

## Temporal modes

The pill text is explicit, never color-only:

- `LIVE` — views follow the present.
- `SNAPSHOT` — a recorded snapshot is inspected; the inferior keeps
  running elsewhere.
- `REPLAY` — a seek-capable backend moved the inferior to the selected
  point.

The pill doubles as the Return-to-Live control (click it while historic);
its tooltip carries the full `TIME MACHINE · HISTORIC · #n` wording plus
the no-rewind explanation.

The mapping lives in `DisplayedStateModel::temporalState()` and reuses the
backend `TemporalState` enum (`HistoryTypes.h`): Historic unless the active
backend advertises `Seek`.

## Command surfaces

`Program > Time Machine` is the single menu: Start/Stop Recording, Previous,
Next, Reverse Continue, Return to Present, Show Timeline, plus Advanced
(GDB reverse steps, Backend Capabilities). The timeline header keeps only
Previous / Next buttons; the pill itself returns to live. Every surface shares
`timeMachineActionState()` (`TimeMachineModel.h`), so enablement and the
why-tooltips can never disagree. All timeline navigation funnels through
`TimeTravelController`, the single authoritative selected position derived
from `HistorySession` (`Live` / `Snapshot` / `RecordedPosition`).

## Timeline model

`HistoryPointModel` (QtCore, unit-tested) exposes row 0 = present and
history points newest-first with sequence, location, function, thread, type,
checkpoint and snapshot-link roles. `HistoryPointDelegate` paints the
gutter. Future event types (IRQ, peripheral, …) only add rows and subtypes.

## No-leak rule

When a historic point is selected, every compatible view renders that
point's snapshot. Views without captured data (call stack, threads, memory,
disassembly) or with an unlinked snapshot show an explicit "not captured"
notice and disable live actions; they never silently show live values.
`DisplayedStateModel::displayedStateChanged` is the single fan-out signal.

## CODEX INTEGRATION NOTES

Consumed backend APIs (all from `HistoryBackend` / `HistorySession`, no raw
GDB from widgets):

- `HistorySession::historyPoints()` / `historyPoint(id)` — timeline rows.
- `seekToHistoryPoint(id)` — click semantics: seeks when the backend
  supports `Seek`, otherwise inspects the snapshot.
- `returnToPresent()` / `goLive()` — present row, Present button/menu.
- `startRecording()` / `stopRecording()` / `isRecording()` /
  `recordingChanged` — menu state; gated by `RecordReplay`.
- `reverseStep()` / `reverseNext()` / `reverseContinue()` / `reverseFinish()`
  — menu actions; gated by `ReverseStep` / `ReverseNext` /
  `ReverseContinue` / `ReverseFinish`. Advanced GDB reverse steps call the
  `DebuggerSession` reverse API directly (preserved behavior).
- `createCheckpoint(id)` / `checkpoint(id)` — timeline context menu; gated
  by `Checkpoints`.
- `temporalStateChanged` — observed; the session core currently emits only
  Live/Historic (never Replayed). The UI derives Replayed from the `Seek`
  capability instead; if the core later emits Replayed for real target
  moves, `DisplayedStateModel::temporalState()` should prefer that signal.
- `operationFinished(name, ok, message)` — async backend completions; not
  yet surfaced in UX (future: transient status). `returnToPresent()` on a
  `RecordReplay` backend is async; the display follows when the live
  snapshot arrives.
- `DebuggerSession::timeMachineRecordingSupported/Active`,
  `start/stopTimeMachineRecording`, `timeMachineRecordingStateChanged` —
  consumed exclusively inside `GdbRecordTimeMachineBackend`.
- Temporary adapters introduced: none. The UI needed no new backend API;
  `TimeMachineActionState`/`HistoryPointModel`/`temporalBadgeText` are
  presentation-only and backend-agnostic.

Mismatches found: none blocking. Minor: `ExecutionHistoryPoint::threadId`
is never populated by the session core, so the timeline falls back to the
snapshot thread id and then "—". `HistoryPointType` has no per-domain
subtypes yet (IRQ/CAN/… will need either subtypes or metadata).

## Visual verification

Rendered offscreen from the real `HistoryView` against a scripted session
(`QT_QPA_PLATFORM=offscreen` harness, kept outside the repo at
`/tmp/tm_harness.cpp` for reuse):

- `docs/screenshots/time-machine-live.png` — LIVE pill, NOW selected,
  live inspector, no historic residue.
- `docs/screenshots/time-machine-historic.png` — `#2 · SNAPSHOT`,
  strong function, dim location, inline changes with real old → new.
- `docs/screenshots/time-machine-captured.png` — collapsed-row expansion
  with filter and bounded value table.
- `docs/screenshots/time-machine-details.png` — collapsed diagnostics
  (timestamp, PC, symbol, frame, thread, backend, history ID).
- `docs/screenshots/time-machine-nochange.png` — middle stop with
  identical values: honest "No captured values changed" state.
- `docs/screenshots/time-machine-showall.png` — expanded 6-row change
  list with "Show fewer changes" toggle.

Compared against the original panel: the 7-row metadata form, the
50/50 splitter, the flow-prototype tree and the six-button technical
transport bar are gone; header/timeline/inspector is the whole panel.
