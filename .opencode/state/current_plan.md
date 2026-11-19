# Plan: Thread-Safe FSM Event Dispatcher for LaunchMissileSeq — COMPLETE

## Status: ✅ All tasks completed and reviewed

### Changes Summary
- **3 files changed**: `DataController.cpp` (1 line removed), `LaunchMissileSeq.h` (+18 lines), `LaunchMissileSeq.cpp` (+102, -23 lines)

### What was done
1. **Task 1**: Added dispatcher infrastructure — thread-safe event queue, mutex, condition variable, dispatch thread, `queueEvent()` method. Migrated `PrivateData*` to `std::unique_ptr<PrivateData>`.
2. **Task 2**: Migrated all 9 `onEv*` methods to push lambdas to the queue instead of calling `process_event()` directly.
3. **Review fixes**:
   - Removed `seq->initiate()` from `DataController.cpp` (double-initiate race)
   - Added `mStopping` check in `queueEvent()` to prevent pushes during destruction
   - Added `try/catch` around `task()` in `dispatchLoop()` for exception safety
   - Removed unused `mRunning` member

### Design
- **Non-blocking**: `onEv*` methods push to queue and return immediately
- **`initiate()` queued as first task**: Constructor queues it, not called directly
- **RAII**: `std::unique_ptr<PrivateData>` replaces raw pointer
- **Queue grows unbounded**: No max size
- **Single dispatch thread**: All `process_event()` calls serialized on one thread

## Investigation Report

### Conventions in scope
- **Programming Style**: "Prefer value semantics and RAII over raw pointers and manual memory management." (programming-style.md #1)
- **Code Style**: "Use RAII for all resource management — no raw new/delete" (code-styles.md #4). Note: current `StateMachine` uses raw `new PrivateData()` — out of scope to fix here but relevant.
- **Qt Guidelines**: "Never block the GUI thread. Use QThread, QtConcurrent, or async patterns for long-running work." (qt-guidelines.md Threading #1)
- **Error Handling**: "Always raise errors explicitly — never silently ignore them." (error-handling.md #1)

### Existing objects this change extends
- `src/app/data/ds/fsm/LaunchMissileSeq.h` — header-only boost::statechart FSM with all state/event definitions inline
- `src/app/data/ds/fsm/LaunchMissileSeq.cpp` — StateMachine constructor/destructor, all `onEv*` methods that call `process_event()` directly, all `on*()` callbacks
- `src/app/data/DataController.cpp` — caller of all `onEv*` methods, stores `std::shared_ptr<fsm::lms::StateMachine>` in `mLaunchMissileSeq` map

### Threading model (verified)
Events arrive from **multiple independent threads**:
1. **CDataProcessing thread** (`std::thread` via `IDataProcessing::eventloop()`) — decodes CCU network data, calls `recvRACCPrepareLaunchInfo`, `recvRACCLaunchMissileCmdInfo`, `recvRACCMissileLaunchedInfo`, `recvRACCCancelLaunchCmdInfo`
2. **Ros2PreLaunchAckDatabuf thread** (`std::thread` via `Ros2PubDatabuf` pattern) — calls `recvISPPrepareLaunchACK`
3. **MissileMgmt thread** (`std::thread` via `MissileMgmt::processFnc`) — calls `recvISPTerminateMissile`
4. **CentralControlService thread** (`boost::thread` via `BoostnetTcpServerService`) — TCP server callbacks

All of these eventually call `StateMachine::onEv*()` → `process_event()` on the **same** `StateMachine` instance (keyed by missileID).

### The problem
`boost::statechart::state_machine::process_event()` is **NOT thread-safe**. Concurrent calls from multiple threads cause data races on the FSM's internal state (current state pointer, state stack, event reaction dispatch). This is undefined behavior.

### Existing patterns in repo
- `core::ThreadSafeQueue<T>` (`src/app/module/common/ThreadSafeQueue.h`) — thread-safe queue with `push()`, `waitAndPop()`, `wakeAll()`
- `core::QueueMsgProcessor` (`src/app/module/service/bpipe/QueueMsgProcessor.h`) — queue + dedicated processing thread pattern
- `LockedFunc` macro in `DataController.cpp` — `std::lock_guard` wrapper
- `IDataProcessing` — base class with `std::thread` + `std::mutex` + `std::condition_variable` pattern

### Genuinely new surface
- Dispatcher infrastructure inside `StateMachine`: event queue, mutex, condition variable, processing thread, start/stop logic
- All `onEv*` methods need to change from direct `process_event()` to queue-push

### Stored vs derived vs projected
- **Stored**: The event queue (new) — stores pending events until the dispatch thread processes them
- **Derived**: N/A — no new derived state
- **Projected**: N/A — no new projected state

### Producers and consumers
- **Producer**: Each `onEv*` method (called from external threads) — produces queued event tasks
- **Consumer**: The dispatch thread (single thread) — consumes tasks and calls `process_event()` sequentially
- **Producer**: `StateMachine` constructor — produces the `initiate()` call
- **Consumer**: `boost::statechart` internal state — consumes `initiate()` to set up initial state

### Open questions for the user — RESOLVED
1. **Non-blocking**: `onEv*` methods push to queue and return immediately. ✅ Confirmed.
2. **`initiate()` queued as first task**: Not called in constructor; queued as the first dispatch task. ✅ Confirmed.
3. **RAII for PrivateData**: Migrate `PrivateData*` to `std::unique_ptr<PrivateData>`. ✅ Confirmed.
4. **Queue overflow**: Queue grows unbounded. ✅ Confirmed.

---

## Proposed Plan

### Design
Embed a dispatcher directly into `StateMachine`. Each `onEv*` method captures its parameters in a `std::function<void()>` lambda and pushes it to a thread-safe queue. A single dedicated `std::thread` (the dispatch thread) pops tasks from the queue and executes them one at a time, serializing all `process_event()` calls.

**Key decisions** (confirmed by user):
- **Non-blocking**: `onEv*` methods push to queue and return immediately
- **`initiate()` queued as first task**: Not called in constructor; queued as the first dispatch task
- **RAII for PrivateData**: Migrate `PrivateData*` to `std::unique_ptr<PrivateData>`
- **Queue overflow**: Queue grows unbounded (no max size)

### Task 1: Add dispatcher infrastructure to StateMachine

**Intent**: Add a thread-safe event queue and a dedicated dispatch thread to `StateMachine` so that all `process_event()` calls are serialized on a single thread.

**Files to edit**:
- `EDIT src/app/data/ds/fsm/LaunchMissileSeq.h` — add member variables: `std::queue<std::function<void()>>`, `std::mutex`, `std::condition_variable`, `std::thread`, `std::atomic<bool> mRunning`, `std::atomic<bool> mStopping`, private methods `dispatchLoop()` and `queueEvent(std::function<void()>)`
- `EDIT src/app/data/ds/fsm/LaunchMissileSeq.cpp` — implement `dispatchLoop()`, `queueEvent()`, modify constructor to start dispatch thread (constructor does NOT call `initiate()` — it will be queued as first task), modify destructor to signal stop, wake, and join thread. Migrate `PrivateData*` to `std::unique_ptr<PrivateData>`.

**Conventions that apply**:
- "Use RAII for all resource management — no raw new/delete" (code-styles.md #4) — migrate `PrivateData*` to `std::unique_ptr<PrivateData>`
- "Prefer value semantics and RAII over raw pointers" (programming-style.md #1)
- "Never block the GUI thread" (qt-guidelines.md Threading #1) — the dispatch thread is a background thread, not GUI

**Producer/consumer pair**:
- Producer: `queueEvent()` method (called from external threads via `onEv*` methods)
- Consumer: `dispatchLoop()` (runs in dedicated `std::thread`, calls `process_event()`)

**Failure modes to avoid**:
- Don't call `initiate()` in constructor — it must be queued as the first task so it runs on the dispatch thread
- Don't leave the dispatch thread running after destructor — must signal stop, wake, and join
- Don't push to queue after stop signal — check `mStopping` flag in `queueEvent()` and either drop or log error
- Don't capture references in lambdas that outlive the caller — all captures must be by value

### Task 2: Migrate all `onEv*` methods to use the dispatcher

**Intent**: Change every `onEv*` method to push a lambda to the event queue instead of calling `process_event()` directly, eliminating the data race.

**Files to edit**:
- `EDIT src/app/data/ds/fsm/LaunchMissileSeq.cpp` — rewrite all 9 `onEv*` methods to call `queueEvent()` with a lambda that captures event data by value and calls `process_event()` inside. The first queued task (from constructor) calls `initiate()`.

**Conventions that apply**:
- "Write simple single-purpose functions — no multi-mode behavior" (programming-style.md #11) — each `onEv*` remains a single-purpose method
- "Prefer const correctness" (code-styles.md #3) — lambda captures should be `const` where applicable

**Producer/consumer pair**:
- Producer: Each `onEv*` method (e.g., `onEvPrepareLaunch`) — creates and queues a task
- Consumer: `dispatchLoop()` — executes the task, which calls `process_event()` and triggers state callbacks

**Failure modes to avoid**:
- Don't capture `this` and then access `mData` members by reference in the lambda — the lambda runs on a different thread, so `mData` access is safe only because the dispatch thread is the sole consumer
- Don't forget to capture all needed parameters — each lambda must capture all data it needs (event structs, missileID, targetID)
- The `on*()` callbacks (`onPreparing()`, `onLaunching()`, etc.) are called from within `process_event()` which now runs on the dispatch thread — they still read `mData` safely because only the dispatch thread writes to `mData` now

---

## Deferred follow-ups (out of scope for this plan)
- `fsmLaunchMissileSeq.h` (the `fsmpp2`-based FSM) — separate implementation, not touched
- `DataController` callers — no changes needed, interface remains identical
- `PrivateData` raw pointer migration — included in Task 1 as part of RAII compliance
