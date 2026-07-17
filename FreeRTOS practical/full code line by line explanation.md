
---

# `src/main.c` — INCLUDES

```c
#include <stdio.h>
```
Gives us `printf()` and `fflush()`. We need these purely for visibility — printing what's happening, since we have no real screen/debug interface yet (on real MSP430 this would become UART output).

```c
#include <string.h>
```
Gives us `memcpy()` (copying raw bytes) and `memset()` (zeroing out memory). Both are used constantly when moving chunk data around.

```c
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
```
Three separate FreeRTOS headers, not one big header — this is intentional in FreeRTOS's design: `FreeRTOS.h` has the core types/macros everything else depends on, `task.h` specifically brings in task-related functions (`xTaskCreate`, `vTaskDelay`), and `queue.h` brings in queue functions (`xQueueCreate`, `xQueueSend`). **Why does this modularity matter?** If a project doesn't use queues at all, it doesn't need to include `queue.h` — keeps compiled size down on tiny microcontrollers where every byte of flash matters.

```c
#include "payload_chunk.h"
#include "ngham.h"
```
Our own two files. Order matters here only in the sense that `ngham.h` doesn't actually depend on `payload_chunk.h` (I made them independent on purpose — `ngham_build_packet()` just takes raw bytes, it doesn't know what a `PayloadChunk_t` even is). **Why keep them independent?** So the NGHam encoding logic could be reused for a totally different data source later, without dragging along our chunk-specific struct.

---

# GLOBAL QUEUE HANDLES

```c
QueueHandle_t xQueue1_RawChunks;
QueueHandle_t xQueue2_OrderedChunks;
```
Declared as globals, outside any function. **Why global?** Because three separate tasks (functions) all need to access the same queue objects. If I declared them locally inside `main()`, only `main()` could see them — I'd have to pass them as parameters into each task, which FreeRTOS's `xTaskCreate()` does support (via the `pvParameters` argument), but for a small project like this, globals are simpler and perfectly acceptable. In a much larger, multi-file production project, you'd more likely wrap these in a "pipeline context" struct passed via `pvParameters` to avoid global-state issues — worth mentioning if asked about scaling this up.

---

# FAKE EVENT DATA

```c
typedef struct {
    uint16_t occultation_id;
    uint16_t data_size;
} FakeEvent_t;
```
A small, separate struct — just for testing, **not** part of the real pipeline design. It exists purely to describe "pretend event #1000 has 100 bytes of data" without yet generating those bytes.

```c
static const FakeEvent_t fake_events[] = {
    { .occultation_id = 1000, .data_size = 100 },
    { .occultation_id = 1001, .data_size = 45  },
    { .occultation_id = 1002, .data_size = 70  },
};
```
`static` means this array only exists inside `main.c` — it's not visible to other files even if they tried to `extern` it. `const` means the compiler can put this in read-only flash memory rather than RAM, since we never modify it — small but real optimization habit for embedded code. I deliberately chose 3 different sizes: one that's an exact multiple of 32 (100 isn't actually exact, but close), one smaller with an odd remainder (45), one in between (70) — specifically to exercise different edge cases in the fragmentation math.

```c
#define NUM_FAKE_EVENTS (sizeof(fake_events) / sizeof(fake_events[0]))
```
This is a very common C idiom for "how many elements are in this array," computed automatically rather than hardcoding `3`. **Why not just write `3`?** If I later add a 4th fake event to the array and forget to update a hardcoded `3`, my loop would silently only process 3 of 4 events — a classic, sneaky bug. This macro removes that whole class of mistake.

---

# TASK 1 — full walkthrough

```c
void vTask1_PayloadReader(void *pvParameters)
{
    (void) pvParameters;
```
Every FreeRTOS task function must match this exact signature: returns `void`, takes one `void*` parameter. I don't actually use any parameter here, so `(void) pvParameters;` is a standard trick to tell the compiler "yes, I know I'm not using this, don't warn me about it." Without this line, compiling with `-Wextra` (which our Makefile does) would print an "unused parameter" warning every time.

```c
    for (int e = 0; e < (int)NUM_FAKE_EVENTS; e++)
    {
```
Loop through each of our 3 fake events, one at a time. The `(int)` cast is needed because `NUM_FAKE_EVENTS` (from `sizeof`) is an unsigned type, and comparing signed `e` against unsigned could trigger compiler warnings — casting avoids that mismatch.

```c
        uint16_t occ_id   = fake_events[e].occultation_id;
        uint16_t data_len = fake_events[e].data_size;
```
Pull out the two fields for this event into local variables — purely for readability, so the rest of the function reads `occ_id`/`data_len` instead of the longer `fake_events[e].occultation_id` repeatedly.

```c
        uint8_t fake_payload[256];
        for (int i = 0; i < data_len; i++)
        {
            fake_payload[i] = (uint8_t)(0x10 + (e * 0x30) + i);
        }
```
Here we manufacture fake data. **Why a formula instead of just `rand()`?** Because I need the output to be **predictable and debuggable** — if I print `first_byte=0x10` in my log, I need to be able to look at that and instantly verify "yes, this is really the start of event 0's data," not just trust a random number. The `e * 0x30` offset means each event's data starts at a visually distinct value (event 0 starts near `0x10`, event 1 near `0x40`, event 2 near `0x70`), which is exactly what let us cross-check correctness earlier by eye.

```c
        uint8_t total_chunks = (data_len + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE;
```
The "ceiling division" trick, explained again for completeness: plain integer division in C truncates (rounds toward zero), so `45 / 32` gives `1`, silently discarding 13 leftover bytes. Adding `CHUNK_DATA_SIZE - 1` (31) before dividing pushes any remainder over the next whole-number boundary, so `45 + 31 = 76`, `76 / 32 = 2` — correctly accounting for that partial chunk.

```c
        printf("[Task1-PayloadReader] occultation %d: %d bytes -> %d chunk(s)\n",
               occ_id, data_len, total_chunks);
        fflush(stdout);
```
Just a status log. `fflush(stdout)` forces the output to actually appear immediately rather than sitting in an internal buffer — this matters a lot in a multi-threaded/multi-task program, because without it, printouts from different tasks could appear wildly out of order or delayed, making the log confusing to read.

```c
        int bytes_remaining = data_len;
        int offset = 0;
```
Two "cursor" variables that track our progress through this event's data as we slice it into chunks. `offset` = where we currently are in `fake_payload[]`; `bytes_remaining` = how much is left to process.

```c
        for (uint8_t seq = 1; seq <= total_chunks; seq++)
        {
```
Sequence numbers start at **1**, not 0. **Why?** Purely a convention choice — "chunk 1 of 4" reads more naturally to a human (or a ground station operator reviewing logs) than "chunk 0 of 4." NGHam/satellite telemetry systems commonly use 1-based sequence numbering for exactly this reason.

```c
            PayloadChunk_t chunk;
            memset(&chunk, 0, sizeof(chunk));
```
Declare a fresh chunk struct, then zero out its entire memory. **Why zero it first?** Because `chunk.data[]` is 32 bytes, but our actual chunk might only use, say, 4 of those bytes (the last chunk of an event). Without zeroing first, the unused bytes would contain whatever garbage was previously sitting in that stack memory — zeroing guarantees clean, predictable, reproducible output every time, which matters both for correctness and for debugging (no mystery garbage bytes in printouts).

```c
            uint8_t this_chunk_size = (bytes_remaining < CHUNK_DATA_SIZE)
                                        ? (uint8_t)bytes_remaining
                                        : CHUNK_DATA_SIZE;
```
A ternary (`? :`) doing exactly one job: "if less than a full chunk's worth of data remains, use only what's left; otherwise, take a full chunk." This is the line responsible for correctly producing the smaller final chunk of each event (like the `4` bytes at the end of occultation 1000).

```c
            chunk.occultation_id  = occ_id;
            chunk.sequence_number = seq;
            chunk.total_chunks    = total_chunks;
            chunk.data_size       = this_chunk_size;
            memcpy(chunk.data, &fake_payload[offset], this_chunk_size);
```
Filling out every field of our "envelope" struct. The `memcpy` is the one doing actual work — it copies exactly `this_chunk_size` bytes, starting at position `offset` in our fake payload, into the chunk's `data` array. **Why memcpy and not a manual for-loop copying byte by byte?** `memcpy` is a standard library function, typically implemented in highly optimized assembly for whatever CPU it's compiled for — faster and less error-prone than writing my own copy loop.

```c
            xQueueSend(xQueue1_RawChunks, &chunk, portMAX_DELAY);
```
This is the moment the chunk actually leaves Task 1 and enters the pipeline. `&chunk` — we pass the *address* of our local struct, because `xQueueSend` needs to know where in memory to copy the bytes *from*; it does not keep this pointer around afterward, it copies the data immediately into the queue's own internal storage. `portMAX_DELAY` — wait indefinitely if the queue happens to be full right now. Since our queue holds 10 items and downstream tasks consume quickly, this is safe here; in a design where a downstream task could genuinely get stuck, you'd want a finite timeout instead so Task 1 doesn't freeze forever.

```c
            offset          += this_chunk_size;
            bytes_remaining -= this_chunk_size;
```
Advance both cursors by however many bytes we just consumed, so the next loop iteration reads the *next* slice of data.

```c
            vTaskDelay(pdMS_TO_TICKS(50));
```
This is arguably the single most important line for the whole multitasking illusion to work. Without this, Task 1 would run its entire loop from start to finish, producing all 9 chunks back-to-back, **before FreeRTOS ever got a chance to switch to Task 2 or Task 3.** The 50ms delay forces Task 1 to voluntarily pause after every single chunk, handing control back to the scheduler — which is precisely why our log output shows Task 1, Task 2, and Task 3 messages *interleaved* rather than Task 1 finishing completely before anything else runs.

```c
        }
    }

    printf("[Task1-PayloadReader] all occultations fragmented. Task finished.\n");
    fflush(stdout);

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}
```
Once both loops finish (all events, all chunks sent), we print a final status message, then enter an infinite loop that just sleeps forever. **Why not just `return`?** In FreeRTOS, a task function is never supposed to return — the scheduler expects it to keep existing (even if idle) unless explicitly deleted with `vTaskDelete()`. Returning from a task function without deleting it is undefined behavior and can crash the system. Looping forever with a delay is the simplest safe way to let a task "finish its real work" while staying alive and consuming essentially zero CPU (since it's asleep 1000ms at a time).

---

# TASK 2 — full walkthrough

```c
void vTask2_FifoManager(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_forwarded = 0;
```
Same pattern as before — unused-parameter suppression, a local chunk variable to receive into, and a counter purely for the final summary log.

```c
    printf("[Task2-FifoManager] started.\n");
    fflush(stdout);

    for (;;)
    {
```
An infinite loop — this is the defining shape of *every* FreeRTOS task. Tasks are meant to run "forever" (from the program's perspective), continuously checking for work.

```c
        if (xQueueReceive(xQueue1_RawChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
```
This single line does a lot: it tries to pull the next item out of `xQueue1_RawChunks`, copying it into our local `chunk` variable. If the queue is currently empty, this call **blocks** (pauses Task 2, letting other tasks run) for up to 3000ms (`pdMS_TO_TICKS(3000)`), waiting for something to arrive. It returns `pdTRUE` if it successfully got something within that time, or `pdFALSE` if the 3 seconds ran out with nothing arriving.

```c
        {
            total_forwarded++;

            printf("[Task2-FifoManager] forwarding  occ=%d  seq=%d/%d  bytes=%d\n",
                   chunk.occultation_id, chunk.sequence_number,
                   chunk.total_chunks, chunk.data_size);
            fflush(stdout);
```
If we got a chunk, increment our counter and log what we received — useful both for our own debugging and for demonstrating correctness in a presentation.

```c
            xQueueSend(xQueue2_OrderedChunks, &chunk, portMAX_DELAY);
        }
```
This is the entire "forwarding" action — take what we just received, and immediately re-send it into the *next* queue in the pipeline. This is what makes Task 2 a genuine pipeline stage rather than a dead end: data flows *through* it.

```c
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
If the 3-second wait timed out (nothing new arrived), we assume Task 1 must be done producing, print our final tally, and go permanently idle — same "sleep forever" pattern as Task 1's ending.

---

# TASK 3 — full walkthrough (the most involved one)

```c
void vTask3_NGHamEncoder(void *pvParameters)
{
    (void) pvParameters;
    PayloadChunk_t chunk;
    int total_encoded = 0;

    printf("[Task3-NGHamEncoder] started.\n");
    fflush(stdout);

    for (;;)
    {
        if (xQueueReceive(xQueue2_OrderedChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
        {
            total_encoded++;
```
Identical structure to Task 2's receiving pattern, just pulling from `xQueue2_OrderedChunks` instead. This consistency is intentional — it's easier to reason about a pipeline when every stage follows the same "wait, process, log, or timeout" shape.

```c
            uint8_t ngham_payload[64];
            int p = 0;
```
A local buffer to build up our serialized bytes, and a running position counter `p` (same pattern used back in Task 1 with `offset`).

```c
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id >> 8);
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id & 0xFF);
```
This is manually splitting a 16-bit number into two 8-bit bytes. `>> 8` shifts the number right by 8 bits, which discards the lower 8 bits and leaves just the upper (high) byte. `& 0xFF` masks off everything except the lower 8 bits, keeping just the low byte. **Why do this manually instead of just casting the uint16_t as two bytes directly?** Because different CPU architectures store multi-byte numbers in memory in different orders internally (endianness) — doing it explicitly like this guarantees the byte order in our output is always "high byte first" (big-endian), regardless of what CPU compiled the code, which is essential for a communication protocol where sender and receiver must agree on byte order.

```c
            ngham_payload[p++] = chunk.sequence_number;
            ngham_payload[p++] = chunk.total_chunks;
            ngham_payload[p++] = chunk.data_size;
```
These three are already single bytes (`uint8_t`), so no splitting needed — direct copy.

```c
            memcpy(&ngham_payload[p], chunk.data, chunk.data_size);
            p += chunk.data_size;
```
Append the actual chunk data after the 5 metadata bytes. After this, `p` holds the total serialized length — 5 plus however many data bytes this particular chunk had (32 for a full chunk, fewer for the last "remainder" chunk of an event).

```c
            uint8_t ngham_packet[300];
            int packet_len = ngham_build_packet(ngham_payload, (uint8_t)p, ngham_packet);
```
Hand off to our NGHam encoder function (fully explained below), which does all the actual protocol work and returns how many bytes it wrote into `ngham_packet`.

```c
            if (packet_len < 0)
            {
                printf("[Task3-NGHamEncoder] ERROR: payload too large to encode!\n");
                fflush(stdout);
                continue;
            }
```
Defensive check: `ngham_build_packet` returns `-1` if the payload didn't fit any of NGHam's 7 size categories (i.e., bigger than 220 bytes). This should never actually trigger with our current 32-byte chunk size, but checking it means if someone later changes `CHUNK_DATA_SIZE` to something too large, we get a clear error message instead of the program silently writing garbage or crashing. `continue` skips the rest of this loop iteration and goes back to waiting for the next chunk.

```c
            printf("[Task3-NGHamEncoder] occ=%d seq=%d/%d -> NGHam packet (%d bytes): ",
                   chunk.occultation_id, chunk.sequence_number, chunk.total_chunks, packet_len);
            for (int i = 0; i < packet_len; i++) printf("%02X ", ngham_packet[i]);
            printf("\n");
            fflush(stdout);
```
Print the entire finished packet, byte by byte, as two-digit hex (`%02X` guarantees leading zeros, so `0x0A` prints as `0A` not just `A`, keeping the output aligned and easy to read). This is our stand-in for "transmit" — in a real system, this loop would instead be replaced with a call handing the bytes to the actual radio driver.

```c
        }
        else
        {
            printf("[Task3-NGHamEncoder] no more chunks arriving. Total encoded: %d\n",
                   total_encoded);
            fflush(stdout);
            for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
```
Same timeout/idle pattern as before.

---

# `main()` — full walkthrough

```c
int main(void)
{
    printf("=== CubeSat Packet Pipeline - Task 1 + 2 + 3 Test ===\n\n");
    fflush(stdout);
```
Just a banner so it's obvious in the terminal what program just started running.

```c
    xQueue1_RawChunks     = xQueueCreate(10, sizeof(PayloadChunk_t));
    xQueue2_OrderedChunks = xQueueCreate(10, sizeof(PayloadChunk_t));
```
Create both queues *before* any task exists. **Why does order matter here?** Because if a task started running and tried to send to a queue that hadn't been created yet (still `NULL`), that would be undefined behavior/a crash. Since `xTaskCreate` below doesn't immediately start running the tasks (they only actually begin executing once `vTaskStartScheduler()` is called), doing this first is safe, but it's still good practice to set up all shared resources before registering anything that depends on them. The `10` means each queue can hold up to 10 chunks waiting at once — chosen generously relative to our small 9-chunk test, so we never worry about the queue filling up in this simulation.

```c
    xTaskCreate(vTask1_PayloadReader,  "Task1", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask2_FifoManager,    "Task2", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask3_NGHamEncoder,   "Task3", configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);
```
Register all three tasks. Going argument by argument: the function pointer, a name string (purely for debugging/logging, FreeRTOS itself doesn't use this name for anything functional), the stack size (memory reserved for this task's local variables — Task 3 gets 4x the minimal size instead of 2x, because its local arrays like `ngham_packet[300]` are much bigger than Task 1/2's), `NULL` for parameters (we don't need to pass anything in), `1` for priority (all three equal — no task is more "urgent" than another at this stage), and `NULL` for the task handle (we don't need a reference to control/delete these tasks later).

```c
    vTaskStartScheduler();
```
This is the point of no return — from here on, FreeRTOS's own internal scheduling loop takes over completely, deciding which of our 3 tasks runs when, forever.

```c
    for (;;);
    return 0;
}
```
Code that should genuinely never execute under normal circumstances — `vTaskStartScheduler()` doesn't return unless something catastrophic happened (like running out of heap memory to even start the first task). This is just a safety net so the program doesn't do something undefined if that rare failure occurs.

---

# The 3 hook functions at the bottom

```c
void vAssertCalled(const char *pcFile, unsigned long ulLine) { ... }
void vApplicationMallocFailedHook(void) { ... }
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) { ... }
```
None of these are things *we* call directly — they're callbacks FreeRTOS itself invokes automatically if something goes wrong internally, **because we explicitly turned on the corresponding checks in `FreeRTOSConfig.h`** (`configASSERT`, `configUSE_MALLOC_FAILED_HOOK`, `configCHECK_FOR_STACK_OVERFLOW`). Since we enabled those checks, the linker *requires* these functions to exist somewhere, or the program won't even build — that's exactly the error we hit and fixed earlier in this project. They never actually fired during our real test runs (proof our stack sizes and memory usage were sufficient), but they exist as a safety net.

---
