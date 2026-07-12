# Task 2 — FIFO Queue Manager — Full Documentation

**Project:** SpaceLab UFSC — EDC/TTC Internship (GOLDS-UFSC CubeSat Mission)
**Team:** Hardik Singhal, Shivansh Gupta, Amrit Mishra (LNMIIT, India)
**Mentor:** Lucas Ryan Carneiro
**Status:** Task 1 + Task 2 implemented and verified working together (macOS, FreeRTOS POSIX simulator)

---

## Table of Contents

1. What Task 2 Does and Why
2. Pipeline Architecture (Before vs. After Task 2)
3. Full `main.c` Source Code (Task 1 + Task 2 + temp consumer)
4. Line-by-Line Explanation of What Changed / Was Added
5. Verified Test Output & Correctness Check
6. What Is Real vs. Simulated Right Now
7. Next Steps (Task 3)

---

## 1. What Task 2 Does and Why

Recall the full pipeline goal (from Task 1's documentation):

```
[Payload Memory] -> TASK 1 -> TASK 2 -> TASK 3 -> [Radio Transmission]
                 (fragment)  (FIFO queue)  (NGHam wrap)
```

**Task 2's job — the FIFO Queue Manager:**

1. Receive chunks from **Queue 1** (the same queue Task 1 already produces into).
2. Forward each chunk, in the exact order it was received, into a **new Queue 2**.

**Why is this its own separate task, if a FreeRTOS queue is already FIFO by nature?**

Technically, a single queue already preserves order on its own. But Lucas's original request specifically called for the FIFO/ordering stage to be its **own independent FreeRTOS task**, separate from fragmentation and separate from NGHam encoding. This matters for the real firmware architecture because:

- It keeps each stage doing exactly **one job** (single responsibility) — easier to test, debug, and modify independently.
- It leaves room for **future logic** to be added inside Task 2 without touching Task 1 or Task 3 — for example, priority reordering, buffering during radio downtime, or dropping/retrying chunks — without redesigning the whole pipeline.
- It matches how the real TTC firmware is structured (separate FreeRTOS tasks per responsibility, communicating only through queues).

So functionally, in this current simple version, Task 2 is intentionally "just" a pass-through — but architecturally, it is now a real, independent, replaceable stage.

---

## 2. Pipeline Architecture (Before vs. After Task 2)

**Before (Task 1 only):**
```
Task 1 (Payload Reader) -> Queue 1 -> TempConsumer (placeholder, just prints)
```

**After (Task 1 + Task 2):**
```
Task 1 (Payload Reader) -> Queue 1 -> Task 2 (FIFO Manager) -> Queue 2 -> TempConsumer (placeholder, stands in for Task 3)
```

Two queues now exist:
- **`xQueue1_RawChunks`** — Task 1 (producer) to Task 2 (consumer)
- **`xQueue2_OrderedChunks`** — Task 2 (producer) to TempConsumer (consumer, standing in for future Task 3)

---

## 3. Full `main.c` Source Code (Task 1 + Task 2 + temp consumer)

```c
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "payload_chunk.h"

/* Queue 1: Task1 (producer) -> Task2 (FIFO manager) */
QueueHandle_t xQueue1_RawChunks;

/* Queue 2: Task2 (FIFO manager) -> TempConsumer (stand-in for Task3) */
QueueHandle_t xQueue2_OrderedChunks;

/* ---------- Simulated occultation "events" ---------- */
typedef struct {
    uint16_t occultation_id;
    uint16_t data_size;
} FakeEvent_t;

static const FakeEvent_t fake_events[] = {
    { .occultation_id = 1000, .data_size = 100 },
    { .occultation_id = 1001, .data_size = 45  },
    { .occultation_id = 1002, .data_size = 70  },
};

#define NUM_FAKE_EVENTS (sizeof(fake_events) / sizeof(fake_events[0]))

/* ---------- TASK 1: Payload Reader / Fragmenter ---------- */
void vTask1_PayloadReader(void *pvParameters)
{
    (void) pvParameters;

    for (int e = 0; e < (int)NUM_FAKE_EVENTS; e++)
    {
        uint16_t occ_id   = fake_events[e].occultation_id;
        uint16_t data_len = fake_events[e].data_size;

        uint8_t fake_payload[256];
        for (int i = 0; i < data_len; i++)
        {
            fake_payload[i] = (uint8_t)(0x10 + (e * 0x30) + i);
        }

        uint8_t total_chunks = (data_len + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE;

        printf("[Task1-PayloadReader] occultation %d: %d bytes -> %d chunk(s)\n",
               occ_id, data_len, total_chunks);
        fflush(stdout);

        int bytes_remaining = data_len;
        int offset = 0;

        for (uint8_t seq = 1; seq <= total_chunks; seq++)
        {
            PayloadChunk_t chunk;
            memset(&chunk, 0, sizeof(chunk));

            uint8_t this_chunk_size = (bytes_remaining < CHUNK_DATA_SIZE)
                                        ? (uint8_t)bytes_remaining
                                        : CHUNK_DATA_SIZE;

            chunk.occultation_id  = occ_id;
            chunk.sequence_number = seq;
            chunk.total_chunks    = total_chunks;
            chunk.data_size       = this_chunk_size;
            memcpy(chunk.data, &fake_payload[offset], this_chunk_size);

            /* Send into Queue 1 (was xChunkQueue in the Task 1-only version) */
            xQueueSend(xQueue1_RawChunks, &chunk, portMAX_DELAY);

            offset          += this_chunk_size;
            bytes_remaining -= this_chunk_size;

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    printf("[Task1-PayloadReader] all occultations fragmented. Task finished.\n");
    fflush(stdout);

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---------- TASK 2: FIFO Queue Manager ---------- */
void vTask2_FifoManager(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_forwarded = 0;

    printf("[Task2-FifoManager] started.\n");
    fflush(stdout);

    for (;;)
    {
        /* Wait for a chunk from Task 1 */
        if (xQueueReceive(xQueue1_RawChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_forwarded++;

            printf("[Task2-FifoManager] forwarding  occ=%d  seq=%d/%d  bytes=%d\n",
                   chunk.occultation_id,
                   chunk.sequence_number,
                   chunk.total_chunks,
                   chunk.data_size);
            fflush(stdout);

            /* Forward it, in order, into Queue 2 */
            xQueueSend(xQueue2_OrderedChunks, &chunk, portMAX_DELAY);
        }
        else
        {
            printf("[Task2-FifoManager] no more chunks arriving. Total forwarded: %d\n",
                   total_forwarded);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

/* ---------- TEMPORARY consumer (stand-in for Task 3 / NGHam encoder) ---------- */
void vTempConsumer(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t received;
    int total_received = 0;

    printf("[TempConsumer] started (placeholder - will become Task3/NGHam next).\n");
    fflush(stdout);

    for (;;)
    {
        if (xQueueReceive(xQueue2_OrderedChunks, &received, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_received++;
            printf("[TempConsumer] got chunk  occ=%d  seq=%d/%d  bytes=%d  first_byte=0x%02X\n",
                   received.occultation_id,
                   received.sequence_number,
                   received.total_chunks,
                   received.data_size,
                   received.data[0]);
            fflush(stdout);
        }
        else
        {
            printf("[TempConsumer] no more chunks arriving. Total chunks received: %d\n",
                   total_received);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

int main(void)
{
    printf("=== CubeSat Packet Pipeline - Task 1 + Task 2 Test ===\n\n");
    fflush(stdout);

    xQueue1_RawChunks      = xQueueCreate(10, sizeof(PayloadChunk_t));
    xQueue2_OrderedChunks  = xQueueCreate(10, sizeof(PayloadChunk_t));

    xTaskCreate(vTask1_PayloadReader, "Task1", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask2_FifoManager,   "Task2", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTempConsumer,        "TempConsumer", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;);
    return 0;
}

/* Required by FreeRTOSConfig.h for configASSERT */
void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    printf("ASSERT FAILED: %s, line %lu\n", pcFile, ulLine);
    fflush(stdout);
    for (;;);
}

/* Required because configUSE_MALLOC_FAILED_HOOK is 1 */
void vApplicationMallocFailedHook(void)
{
    printf("MALLOC FAILED! Out of heap memory.\n");
    fflush(stdout);
    for (;;);
}

/* Required because configCHECK_FOR_STACK_OVERFLOW is 2 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    fflush(stdout);
    for (;;);
}
```

*(Note: `payload_chunk.h`, `FreeRTOSConfig.h`, and `Makefile` are unchanged from Task 1 — see `task1.md` for their full content.)*

---

## 4. Line-by-Line Explanation of What Changed / Was Added

### 4.1 Renamed and added a second queue handle

```c
QueueHandle_t xQueue1_RawChunks;
QueueHandle_t xQueue2_OrderedChunks;
```

In the Task 1-only version, there was a single global queue called `xChunkQueue`. Now there are two, clearly named by their position in the pipeline:
- `xQueue1_RawChunks` — holds chunks fresh out of Task 1, before FIFO management
- `xQueue2_OrderedChunks` — holds chunks after Task 2 has processed/forwarded them

### 4.2 Task 1 — only one line changed

```c
xQueueSend(xQueue1_RawChunks, &chunk, portMAX_DELAY);
```

Task 1's entire logic is otherwise **identical** to the Task 1-only version — it doesn't know or care that Task 2 exists. It just sends into whatever queue is named `xQueue1_RawChunks` (previously `xChunkQueue`). This is exactly the benefit of the queue-based design: Task 1 needed **zero changes to its own logic** to support inserting Task 2 into the pipeline.

### 4.3 Task 2 — the new task, in full

```c
void vTask2_FifoManager(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_forwarded = 0;

    printf("[Task2-FifoManager] started.\n");
    fflush(stdout);

    for (;;)
    {
        if (xQueueReceive(xQueue1_RawChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_forwarded++;

            printf("[Task2-FifoManager] forwarding  occ=%d  seq=%d/%d  bytes=%d\n",
                   chunk.occultation_id, chunk.sequence_number,
                   chunk.total_chunks, chunk.data_size);
            fflush(stdout);

            xQueueSend(xQueue2_OrderedChunks, &chunk, portMAX_DELAY);
        }
        else
        {
            printf("[Task2-FifoManager] no more chunks arriving. Total forwarded: %d\n",
                   total_forwarded);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
```

Structurally, this is almost identical to `vTempConsumer` from Task 1's version — same "wait up to 3 seconds, otherwise declare done" pattern. The one key difference: instead of just printing and discarding the chunk, Task 2 **re-sends it** into `xQueue2_OrderedChunks` via `xQueueSend()`, so it can continue on to the next stage. This is what makes it a true "forwarding" stage rather than a dead-end consumer.

**Why `pdMS_TO_TICKS(3000)` (a 3-second timeout) instead of `portMAX_DELAY` (wait forever)?**

If Task 2 waited forever for the next chunk, it would have no way to know when Task 1 has permanently finished producing (as opposed to just being briefly slow). Using a timeout lets Task 2 declare "I'm done, no more chunks are coming" once 3 full seconds pass with nothing arriving — this is a simple approach that works fine for this testing/simulation stage, though a more robust real implementation might use an explicit "end of stream" signal instead of a timeout (worth considering for later).

### 4.4 TempConsumer — now reads from Queue 2, not Queue 1

```c
if (xQueueReceive(xQueue2_OrderedChunks, &received, pdMS_TO_TICKS(3000)) == pdTRUE)
```

The only change here versus the Task 1-only version is which queue it reads from. Everything else about `vTempConsumer` is unchanged — it's still just a placeholder proving that whatever arrives at the *end* of the current pipeline is correct, and will eventually be replaced by the real Task 3 (NGHam Encoder).

### 4.5 `main()` — creating both queues and all three tasks

```c
xQueue1_RawChunks      = xQueueCreate(10, sizeof(PayloadChunk_t));
xQueue2_OrderedChunks  = xQueueCreate(10, sizeof(PayloadChunk_t));

xTaskCreate(vTask1_PayloadReader, "Task1", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
xTaskCreate(vTask2_FifoManager,   "Task2", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
xTaskCreate(vTempConsumer,        "TempConsumer", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
```

Both queues are created before the scheduler starts (each can hold up to 10 `PayloadChunk_t` items). All three tasks are registered at the same priority level (`1`) — for this simulation/testing stage, strict priority ordering isn't critical yet, since the whole point is just to prove the data flows through correctly.

---

## 5. Verified Test Output & Correctness Check

**Commands run:**
```bash
make clean
make
./build/cubesat_pipeline
```

**Actual output obtained:**
```
=== CubeSat Packet Pipeline - Task 1 + Task 2 Test ===

[Task1-PayloadReader] occultation 1000: 100 bytes -> 4 chunk(s)
[Task2-FifoManager] started.
[Task2-FifoManager] forwarding  occ=1000  seq=1/4  bytes=32
[TempConsumer] started (placeholder - will become Task3/NGHam next).
[TempConsumer] got chunk  occ=1000  seq=1/4  bytes=32  first_byte=0x10
[Task2-FifoManager] forwarding  occ=1000  seq=2/4  bytes=32
[TempConsumer] got chunk  occ=1000  seq=2/4  bytes=32  first_byte=0x30
[Task2-FifoManager] forwarding  occ=1000  seq=3/4  bytes=32
[TempConsumer] got chunk  occ=1000  seq=3/4  bytes=32  first_byte=0x50
[Task2-FifoManager] forwarding  occ=1000  seq=4/4  bytes=4
[TempConsumer] got chunk  occ=1000  seq=4/4  bytes=4  first_byte=0x70
[Task1-PayloadReader] occultation 1001: 45 bytes -> 2 chunk(s)
[Task2-FifoManager] forwarding  occ=1001  seq=1/2  bytes=32
[TempConsumer] got chunk  occ=1001  seq=1/2  bytes=32  first_byte=0x40
[Task2-FifoManager] forwarding  occ=1001  seq=2/2  bytes=13
[TempConsumer] got chunk  occ=1001  seq=2/2  bytes=13  first_byte=0x60
[Task1-PayloadReader] occultation 1002: 70 bytes -> 3 chunk(s)
[Task2-FifoManager] forwarding  occ=1002  seq=1/3  bytes=32
[TempConsumer] got chunk  occ=1002  seq=1/3  bytes=32  first_byte=0x70
[Task2-FifoManager] forwarding  occ=1002  seq=2/3  bytes=32
[TempConsumer] got chunk  occ=1002  seq=2/3  bytes=32  first_byte=0x90
[Task2-FifoManager] forwarding  occ=1002  seq=3/3  bytes=6
[TempConsumer] got chunk  occ=1002  seq=3/3  bytes=6  first_byte=0xB0
[Task1-PayloadReader] all occultations fragmented. Task finished.
[Task2-FifoManager] no more chunks arriving. Total forwarded: 9
[TempConsumer] no more chunks arriving. Total chunks received: 9
```

**Correctness verification:**

| Occultation | Chunks produced by Task1 | Chunks forwarded by Task2 | Chunks received by TempConsumer | Order preserved? |
|---|---|---|---|---|
| 1000 | 4 (32,32,32,4) | 4 (32,32,32,4) | 4 (32,32,32,4) | Yes |
| 1001 | 2 (32,13) | 2 (32,13) | 2 (32,13) | Yes |
| 1002 | 3 (32,32,6) | 3 (32,32,6) | 3 (32,32,6) | Yes |

**Totals match exactly at every stage:** Task2 forwarded 9, TempConsumer received 9 — identical to the 9 chunks Task 1 originally produced. No chunk was lost, duplicated, or reordered across the two-hop journey (Queue1 -> Task2 -> Queue2 -> TempConsumer).

Notice also the interleaving pattern in the log: Task2 forwards a chunk, then immediately TempConsumer receives that same chunk, before Task2 forwards the next one. This is FreeRTOS's scheduler naturally alternating between the two downstream tasks as data flows through the pipeline in real time — exactly the concurrent "assembly line" behavior the architecture is meant to demonstrate.

---

## 6. What Is Real vs. Simulated Right Now

| Part | Real or Simulated? |
|---|---|
| FreeRTOS tasks/scheduling (all 3 tasks) | Real — same code will run unchanged on the real MSP430 later |
| Queue behavior (both Queue1 and Queue2) | Real — same code will run unchanged later |
| Payload data (`fake_payload[]`) | Simulated — invented bytes; later replaced with real payload memory reads |
| The 3 occultation events | Simulated — invented for testing |
| FIFO ordering logic in Task 2 | Real logic, currently simple pass-through; could be extended later (e.g., priority handling, buffering) without touching Task 1 or Task 3 |
| The microcontroller (Mac instead of MSP430) | Simulated — using FreeRTOS's POSIX port |
| NGHam wrapping | Not built yet — this is Task 3, still ahead |

---

## 7. Next Steps

- **Task 3 (NGHam Encoder):** Replace `vTempConsumer` with a real task that receives chunks from Queue 2, wraps each one into a full NGHam packet (per the NGHam protocol theory already studied — preamble, sync word, size tag, header, CRC, padding, parity, scrambling), and simulates transmission via print/log output.
- **End-to-end test:** Run all three real tasks together (Task1 -> Task2 -> Task3) with the same 3 simulated occultation events, and verify the final NGHam packets are structurally correct.
- **Review with Lucas:** Confirm chunk size (32 bytes) against actual NGHam Size Tag categories, and confirm this pipeline design matches his expectations before considering a port to real MSP430 hardware.

---

*This document reflects Task 1 + Task 2 implementation and verification. Task 3 (NGHam Encoder) is not yet implemented.*
