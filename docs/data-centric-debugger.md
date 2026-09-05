# Data-centric debugger implementation status

## Architecture inspected

`DebuggerSession` owns QProcess transport, MiStreamBuffer line framing, tokened
command scheduling, MI decoding, debugger state, variable trees and snapshots.
It exposes domain values to most views but also exposes raw commands. GDB and
LLDB/MI currently share this implementation; this is not an engine-neutral backend.
`RuntimeObjectGraph` is independent of graphics items and canonicalizes addresses
with type names. RuntimeGraphLayout and OrthogonalEdgeRouter already separate
layout/routing from graph semantics. GraphicalVariablesView remains responsible
for translating variable trees into the graph and managing scene items.

## Implemented in this change

* MiParser provides ordered, recursive MI values, preserving duplicate result
  names, nested tuples/lists and empty strings. It parses result, execution,
  status, notification and stream records, validates tokens, decodes C escapes
  in one pass, and bounds nesting to 128 levels.
* DebuggerSession validates protocol records before dispatch, decodes streams
  through the parser, and retrieves named results through parsed values instead
  of regular expressions. Existing tuple-body callers are supported during
  migration. Invalid records are logged and cannot complete an in-flight command;
  the existing timeout recovery remains responsible for that command.
* Legacy brace extraction now ignores braces inside escaped/quoted strings.
* Runtime graph differences report removed members as changed paths, allowing
  downstream consumers to invalidate disappearing values.

Parser is header-only and depends only on Qt Core. Line framing remains the
responsibility of MiStreamBuffer; incomplete lines must not be sent to MiParser.
Recursive compatibility field lookup prefers direct fields before descendants;
new consumers should use explicit field paths to avoid ambiguous nested names.

## Files

Added: src/MiParser.h, tests/MiParserTests.cpp, docs/data-centric-debugger.md.
Modified: src/DebugSession.cpp, src/RuntimeObjectGraph.cpp,
tests/RuntimeObjectGraphTests.cpp, tests/CMakeLists.txt.

## Validation

```
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt@5 -DBUILD_TESTING=ON
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

New parser tests cover realistic stack records, repeated names, escaped strings,
octal escapes, async/stream records, invalid syntax, token overflow and excessive
nesting. Graph tests now cover removed member detection. Existing fake-debugger
command correlation, framing, graph identity/cycles, layout, routing and stress
program tests remain in the suite. These are not live-GDB integration tests.

## Remaining work, in dependency order

This change is a foundation increment, not the complete requested migration.

1. Extract transport and GDB command adapter from DebuggerSession behind an
   engine-neutral asynchronous domain interface. Migrate raw-command UI consumers.
2. Move graph construction out of GraphicalVariablesView. Add lifetime/context
   identity, cautious type normalization, explicit variable bindings, reference
   and ownership edge kinds; distinguish unavailable/null values from objects.
3. Add debugger pretty-printer providers and semantic adapters for STL/Qt types.
4. Drive scene updates with persistent canonical IDs and graph deltas; profile
   hundreds of nodes and preserve manual layout during incremental updates.
5. Add stop/thread/frame-aware snapshot and history models with historical
   inspection independent from reverse execution, then numeric/table views.
6. Add domain memory reads and memory inspector, thread navigation, complete
   breakpoint/watchpoint domain models and synchronized UI.
7. Extend diagnostics to logging categories and add live-GDB fixtures covering
   aliases, references, allocation lifetimes, containers and multiple threads.

## Suggested logical commits

* feat: parse and validate structured GDB MI records
* fix: detect removed object members in graph differences
* test: cover malformed MI and disappearing graph members
* docs: document data-centric architecture and remaining migration
