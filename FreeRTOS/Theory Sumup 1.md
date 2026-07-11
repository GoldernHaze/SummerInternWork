# CubeSat Packet Pipeline — Progress Log

**Project:** SpaceLab UFSC — EDC/TTC Internship
**Team:** Hardik Singhal, Shivansh Gupta, Amrit Mishra (LNMIIT)
**Mentor:** Lucas Ryan Carneiro
**Environment:** macOS (native Terminal), GCC, FreeRTOS Kernel (POSIX simulator port)

---

## 1. Task Context

Lucas assigned a packet-handling pipeline task for the S-band TT&C system. The goal:

> Gather payload data from storage memory, split it into smaller chunks (since payloads can be large), assign metadata to each chunk (sequence number, occultation ID, data size), push chunks into a FIFO queue to preserve transmission order, then encapsulate each chunk into an NGHam frame for transmission.

Because the firmware runs on **FreeRTOS**, each stage of this pipeline is implemented as a **separate FreeRTOS task**:

| Task | Responsibility |
|------|----------------|
| **Task 1** | Payload Reader / Fragmenter — simulate reading payload memory, split into chunks, attach metadata |
| **Task 2** | FIFO Queue Manager — receive chunks, preserve order, forward downstream |
| **Task 3** | NGHam Encoder — wrap each chunk into an NGHam packet, "transmit" (simulated) |

**Lucas's recommendation:** Start with a simulated-memory, MCU-independent implementation (no real hardware yet) before porting to the MSP430.

---

## 2. Background Knowledge Covered Before Coding

### 2.1 NGHam Protocol (studied via PyNGHam docs + TTC 2.0 documentation)

- Packet structure: `Preamble (4B)` → `Sync Word (4B)` → `Size Tag (3B)` → `RS Block [Header(1B) + Payload(1-220B) + CRC(2B) + Padding(0-31B) + Parity(16/32B)]`
- 7 possible Size Tag configurations, each mapping to a specific Reed-Solomon scheme (`RS(47,31)` up to `RS(255,223)`) and max payload size (28B to 220B)
- CRC16-CCITT (poly `0x1021`, init `0xFFFF`, final XOR `0xFFFF`) used for fast error detection before falling back to Reed-Solomon correction
- Padding fills unused space in the RS block to match the fixed block size required by Reed-Solomon
- Scrambling (XOR with a fixed table per CCSDS 131.0-B-3) avoids long runs of 1s/0s on the RF link

### 2.2 Serial Port Protocol (SPP) — wraps NGHam packets for USB/serial link between computer and radio

- Structure: `Start Tag ('$')` → `CRC (2B, little endian)` → `Payload Type (1B)` → `Payload Length (1B)` → `Payload`
- 4 payload types: `0x00 RF RX`, `0x01 RF TX`, `0x02 Local`, `0x03 Command`
- RX packets include signal quality metadata: timestamp, noise floor, RSSI, corrected symbol count
- TX packets are simpler: just a flags byte + the NGHam packet to send

### 2.3 Extension Packets — optional sub-packets inside an NGHam payload (Data, ID, Status, Position, Time, Destination), each with a `Type + Length + Data` structure. Support in PyNGHam is partial; not required for the current task.

### 2.4 FreeRTOS Fundamentals

- **Task** — a C function with an infinite loop, created via `xTaskCreate()`, must call a blocking/delay function (e.g. `vTaskDelay()`) so other tasks get CPU time
- **Queue** — inter-task communication mechanism (`xQueueCreate`, `xQueueSend`, `xQueueReceive`); acts like a mailbox/conveyor belt between tasks
- **Delay** — `vTaskDelay(pdMS_TO_TICKS(ms))` pauses a task without blocking the whole system
- **Priority** — set at task creation; higher number = more CPU precedence
- **Scheduler** — `vTaskStartScheduler()` hands control to FreeRTOS; code after this call never runs under normal operation

---

## 3. Development Environment Decision

- **Target hardware (long-term):** MSP430F6659 (used on the TTC 2.0 board)
- **Problem:** No physical MSP430 board available; Code Composer Studio is Windows/Linux-only
- **Solution (adopted from teammate Amrit's approach, who used WSL):** Use the **FreeRTOS POSIX/Linux simulator port**, which lets FreeRTOS run as a normal process on a Unix-like OS — including **native macOS**, since macOS is POSIX-compliant
- **Benefit:** Test all pipeline logic (fragmentation, queues, NGHam wrapping) fully in software, with fast iteration, before ever touching real MCU hardware or Code Composer Studio

---

## 4. Project Structure Created

```
cubesat-pipeline/
├── FreeRTOS-Kernel/          (cloned from github.com/FreeRTOS/FreeRTOS-Kernel)
│   ├── tasks.c, queue.c, list.c, timers.c, event_groups.c
│   └── portable/
│       ├── MemMang/heap_3.c
│       └── ThirdParty/GCC/Posix/   (POSIX simulator port + utils)
├── src/
│   ├── FreeRTOSConfig.h      (FreeRTOS configuration for this project)
│   ├── main.c                (application entry point + tasks)
│   └── payload_chunk.h       (shared struct definition for pipeline data)
├── build/                    (compiled binary output)
└── Makefile
```

---

## 5. Setup Steps Performed (macOS)

1. Verified `gcc --version` and `make --version` available (Xcode Command Line Tools)
2. Cloned FreeRTOS Kernel: `git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git`
3. Created `src/` and `build/` folders
4. Wrote `FreeRTOSConfig.h` (heap size, tick rate, stack size, hook usage flags, etc.)
5. Wrote a minimal `main.c` with **two test tasks** (printing at 1s and 2s intervals) to validate the whole toolchain before building the real pipeline
6. Wrote a `Makefile` to compile the kernel + POSIX port + application into a single binary

---

## 6. Mac-Specific Build Issues Encountered & Fixes

| Issue | Cause | Fix |
|-------|-------|-----|
| `pthread_setname_np` undeclared | macOS's single-argument variant isn't exposed under strict `_POSIX_C_SOURCE` defines used by the Linux-oriented port code | Commented out the offending call in `port.c` (only affects debug thread naming, not functionality) |
| `ld: library 'rt' not found` | `-lrt` is a Linux-only library (POSIX timers); macOS provides equivalent functionality without a separate `librt` | Removed `-lrt` from `LDFLAGS` in the Makefile, kept `-lpthread` |
| `Undefined symbols: _vApplicationMallocFailedHook / _vApplicationStackOverflowHook` | `FreeRTOSConfig.h` enables `configUSE_MALLOC_FAILED_HOOK` and `configCHECK_FOR_STACK_OVERFLOW`, which require the application to define these hook functions | Added both hook functions to `main.c` |

---

## 7. First Successful Run ✅

Command:
```bash
make clean
make
./build/cubesat_pipeline
```

Output (excerpt):
```
=== FreeRTOS POSIX Test ===
[Task1] Hello from Task 1!
[Task2] Hello from Task 2!
[Task1] Hello from Task 1!
[Task1] Hello from Task 1!
[Task2] Hello from Task 2!
...
```

**Confirmed:** Task1 (1s delay) and Task2 (2s delay) run concurrently, managed by the FreeRTOS scheduler, entirely simulated on macOS — no MCU hardware required. Full toolchain (kernel + POSIX port + Makefile + config) is working correctly.

---


- [ ] Define shared `PayloadChunk_t` struct (`payload_chunk.h`) — **done, not yet integrated**
- [ ] Implement **Task 1**: simulated payload memory + fragmentation logic, sending chunks to Queue 1
- [ ] Implement **Task 2**: FIFO manager, receiving from Queue 1, forwarding to Queue 2 in order
- [ ] Implement **Task 3**: NGHam encoder, receiving from Queue 2, wrapping chunks into NGHam packets (leveraging existing NGHam protocol knowledge), simulated transmission (print/log)
- [ ] Test end-to-end with multiple simulated occultation events
- [ ] Review chunk size and header fields with Lucas
- [ ] (Later) Port working logic to MSP430F6659 + Code Composer Studio for real hardware validation

---

*Last updated: reflects progress through first successful FreeRTOS POSIX build on macOS, before pipeline task implementation begins.*
