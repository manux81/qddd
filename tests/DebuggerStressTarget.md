# C++20 debugger stress target

`debugger_stress_target` is a deterministic executable designed for manual
debugging with qddd and for automatic execution through CTest.

Build it with:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target debugger_stress_target
```

Open `build/tests/debugger_stress_target` in qddd and put a breakpoint on
`stress::debuggerCheckpoint`. The function is reached in three phases:

1. `objects-ready`: containers, polymorphic objects and the cyclic graph exist.
2. `threads-waiting`: two worker threads remain alive on semaphores.
3. `final-state`: computed values and the final checksum are available.

Useful Data Display expressions include:

```text
snapshot.mutableCounter
snapshot.sensors[0]
snapshot.sensors[0].rawSamples[2]
snapshot.graphEntry
snapshot.graphEntry->next
snapshot.graphEntry->next->next->next
snapshot.shapes[0]
snapshot.activeCommand
snapshot.lastError
snapshot.oddSquares
snapshot.fibonacciValues
snapshot.workerResults
*snapshot.completedWorkers
snapshot.featureFlags
```

Try hexadecimal, decimal, binary and character formats on scalar fields. At
`final-state`, `snapshot.mutableCounter` is also a safe value to edit manually.
