
# Task 3 — NGHam Encoder — Full Documentation

**Project:** SpaceLab UFSC -- EDC/TTC Internship (GOLDS-UFSC CubeSat Mission)
**Team:** Hardik Singhal, Shivansh Gupta, Amrit Mishra (LNMIIT, India)
**Mentor:** Lucas Ryan Carneiro
**Status:** Task 1 + Task 2 + Task 3 implemented and verified working end-to-end (macOS, FreeRTOS POSIX simulator). This completes the full 3-stage pipeline originally requested.

---

## Table of Contents

1. What Task 3 Does and Why
2. Final Pipeline Architecture (All 3 Tasks)
3. NGHam Theory Recap (What We're Actually Building)
4. Full Source Code — `src/ngham.h`
5. Full Source Code — `src/main.c` (all 3 tasks)
6. Line-by-Line Explanation of `ngham.h`
7. Line-by-Line Explanation of Task 3 in `main.c`
8. Deep Worked Example — One Real Chunk, Traced Byte-by-Byte
9. Verified Real Test Output (Full Run, All 9 Chunks)
10. Independent Verification (Cross-Checked Against Python Simulation)
11. What Is Real vs. Simulated / Approximated Right Now
12. Known Limitations and Open Questions for Lucas
13. Next Steps

---

## 1. What Task 3 Does and Why

Recall the full pipeline goal:

```
[Payload Memory] -> TASK 1 -> TASK 2 -> TASK 3 -> [Radio Transmission]
                 (fragment)  (FIFO queue)  (NGHam wrap)
```

**Task 3's job -- the NGHam Encoder:**

1. Receive a chunk from Queue 2 (already fragmented by Task 1, ordered by Task 2).
2. **Serialize** the chunk's metadata (occultation ID, sequence number, total chunks, data size) plus its raw data bytes into a single byte buffer -- this becomes the actual "payload" that will travel inside the NGHam packet.
3. Select the correct **NGHam Size Tag** (one of 7 categories) based on how big that serialized payload is.
4. Build the **Header** byte (encodes how much padding is needed).
5. Calculate the **CRC16-CCITT** checksum (over header + payload only).
6. Add **padding** zero bytes to fill the Reed-Solomon block to its required fixed size.
7. Run a real **Reed-Solomon encoder** (Galois Field GF(256) arithmetic) to compute parity/error-correction bytes.
8. Assemble the final packet: `Preamble(4) + SyncWord(4) + SizeTag(3) + [Header+Payload+CRC+Padding+Parity]`.
9. "Transmit" it -- in this simulation stage, by printing the complete packet as hex bytes to the terminal (a real implementation would hand this off to the radio driver instead).

**Why serialize metadata into the payload, rather than just sending raw data?**

Once a packet is transmitted and later received on the ground, the receiver has *no other way* to know which occultation event or which sequence position a given packet belongs to -- unless that information is embedded *inside* the NGHam payload itself. This directly satisfies Lucas's original requirement: *"For each chunk, we need to assign a sequence number, ID of the occultation... data size of the payload."* These fields must travel with the data, not just exist in our program's RAM.

---

## 2. Final Pipeline Architecture (All 3 Tasks)

```
TASK 1 (Payload Reader/Fragmenter)
   |  - simulates reading payload memory for 3 fake occultation events
   |  - splits each event into fixed 32-byte chunks
   |  - fills PayloadChunk_t: occultation_id, sequence_number, total_chunks, data_size, data[]
   v
QUEUE 1 (xQueue1_RawChunks)
   v
TASK 2 (FIFO Queue Manager)
   |  - receives chunks from Queue 1
   |  - forwards them, strictly in order, to Queue 2
   v
QUEUE 2 (xQueue2_OrderedChunks)
   v
TASK 3 (NGHam Encoder)          <-- THIS DOCUMENT
   |  - receives ordered chunks from Queue 2
   |  - serializes chunk metadata + data into an NGHam payload
   |  - builds a complete, error-correction-protected NGHam packet
   |  - "transmits" (prints hex) the final packet
   v
[Simulated Radio Transmission]
```

All three tasks run **concurrently** as independent FreeRTOS tasks, communicating only through the two queues -- exactly matching Lucas's requirement that each pipeline stage be implemented as a separate FreeRTOS task.

---

## 3. NGHam Theory Recap (What We're Actually Building)

Every NGHam packet has this structure (studied in full depth earlier, summarized here for reference):

```
Preamble(4B) | SyncWord(4B) | SizeTag(3B) | [Header(1B) + Payload(1-220B) + CRC(2B) + Padding(0-31B) + Parity(16 or 32B)]
```

- **Preamble** -- fixed `AA AA AA AA`, lets the receiver's radio hardware synchronize its clock to the incoming signal.
- **Sync Word** -- fixed `5D E6 2A 7E`, marks the exact start of real packet data.
- **Size Tag** -- one of 7 possible 3-byte codewords, telling the receiver which Reed-Solomon scheme and max payload size this packet uses.
- **Header** -- 1 byte; bits 4-0 record how many padding bytes were added.
- **Payload** -- the actual useful data (in our case: serialized chunk metadata + data).
- **CRC** -- CRC16-CCITT checksum over Header+Payload, for fast error detection.
- **Padding** -- zero bytes added so the data block matches the fixed size required by the chosen Reed-Solomon scheme.
- **Parity** -- Reed-Solomon error-correction bytes, letting the receiver fix corrupted bytes without needing retransmission.

The 7 Size Tag categories:

| Size # | Tag (decimal) | RS Config | Max Payload | Parity Bytes |
|---|---|---|---|---|
| 1 | 59, 73, 205   | RS(47,31)   | 28  | 16 |
| 2 | 77, 218, 87   | RS(79,63)   | 60  | 16 |
| 3 | 118,147,154   | RS(111,95)  | 92  | 16 |
| 4 | 155,180,174   | RS(159,127) | 124 | 32 |
| 5 | 160,253,99    | RS(191,159) | 156 | 32 |
| 6 | 214,110,249   | RS(223,191) | 188 | 32 |
| 7 | 237,39,52     | RS(255,223) | 220 | 32 |

---

## 4. Full Source Code -- `src/ngham.h`

```c
#ifndef NGHAM_H
#define NGHAM_H

#include <stdint.h>
#include <string.h>

/* ================= CRC16-CCITT (NGHam spec: poly 0x1021, init 0xFFFF, final XOR 0xFFFF) ================= */
static uint16_t ngham_crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; b++)
        {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

/* ================= Size Tag table (from NGHam protocol spec) ================= */
typedef struct {
    uint8_t tag[3];
    uint8_t rs_n;        /* total RS block size (data + parity) */
    uint8_t rs_k;        /* data bytes (header+payload+crc+padding) */
    uint8_t max_payload; /* max payload bytes for this size class  */
} NGHamSizeConfig_t;

static const NGHamSizeConfig_t ngham_size_table[7] = {
    { {59, 73, 205},   47,  31,  28 },
    { {77, 218, 87},   79,  63,  60 },
    { {118,147,154},  111,  95,  92 },
    { {155,180,174},  159, 127, 124 },
    { {160,253,99},   191, 159, 156 },
    { {214,110,249},  223, 191, 188 },
    { {237,39,52},    255, 223, 220 },
};

/* ================= GF(256) arithmetic for Reed-Solomon ================= */
/* NOTE: using the common demo primitive polynomial 0x11D for GF(256).
   The official NGHam spec uses field polynomial 0x187 with a specific
   generator root -- for byte-exact interop with the real PyNGHam/C
   reference implementation later, these constants would need to match
   exactly. For now, this produces a STRUCTURALLY correct, real, working
   Reed-Solomon systematic encoder to prove the pipeline architecture. */

static uint8_t gf_exp[512];
static uint8_t gf_log[256];
static int gf_initialized = 0;

static void gf_init(void)
{
    int x = 1;
    for (int i = 0; i < 255; i++)
    {
        gf_exp[i] = (uint8_t)x;
        gf_log[(uint8_t)x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_log[0] = 0;
    gf_initialized = 1;
}

static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}

#define NGHAM_MAX_PARITY 32

/* Build RS generator polynomial for 'nsym' parity symbols.
   gen[] ascending order, size (nsym+1), gen[nsym] is always 1 (monic). */
static void rs_generator_poly(int nsym, uint8_t *gen)
{
    memset(gen, 0, (size_t)(nsym + 1));
    gen[0] = 1;
    for (int i = 0; i < nsym; i++)
    {
        gen[i + 1] = 1;
        for (int j = i; j > 0; j--)
        {
            gen[j] = (uint8_t)(gen[j - 1] ^ gf_mul(gen[j], gf_exp[i]));
        }
        gen[0] = gf_mul(gen[0], gf_exp[i]);
    }
}

/* Systematic RS encode: given 'k' data bytes, produce 'nsym' parity bytes */
static void rs_encode(const uint8_t *data, int k, int nsym, uint8_t *parity)
{
    if (!gf_initialized) gf_init();

    uint8_t gen[NGHAM_MAX_PARITY + 1];
    rs_generator_poly(nsym, gen);

    memset(parity, 0, (size_t)nsym);

    for (int i = 0; i < k; i++)
    {
        uint8_t feedback = (uint8_t)(data[i] ^ parity[0]);
        if (feedback != 0)
        {
            for (int j = 1; j < nsym; j++)
            {
                parity[j - 1] = (uint8_t)(parity[j] ^ gf_mul(gen[nsym - j], feedback));
            }
            parity[nsym - 1] = gf_mul(gen[0], feedback);
        }
        else
        {
            for (int j = 1; j < nsym; j++) parity[j - 1] = parity[j];
            parity[nsym - 1] = 0;
        }
    }
}

/* ================= Full NGHam packet builder ================= */
/* Returns total packet length written into out_packet, or -1 on error. */
static int ngham_build_packet(const uint8_t *payload, uint8_t payload_len, uint8_t *out_packet)
{
    int idx = -1;
    for (int i = 0; i < 7; i++)
    {
        if (payload_len <= ngham_size_table[i].max_payload) { idx = i; break; }
    }
    if (idx < 0) return -1; /* payload too big for any NGHam size class */

    const NGHamSizeConfig_t *cfg = &ngham_size_table[idx];
    uint8_t k     = cfg->rs_k;
    uint8_t n     = cfg->rs_n;
    uint8_t nsym  = (uint8_t)(n - k);

    uint8_t padding_size = (uint8_t)(k - 3 - payload_len); /* k = header(1)+payload+crc(2)+padding */
    uint8_t header = (uint8_t)(padding_size & 0x1F);

    uint8_t data_block[256];
    int pos = 0;
    data_block[pos++] = header;
    memcpy(&data_block[pos], payload, payload_len);
    pos += payload_len;

    uint16_t crc = ngham_crc16(data_block, pos); /* CRC over header+payload only */
    data_block[pos++] = (uint8_t)(crc >> 8);
    data_block[pos++] = (uint8_t)(crc & 0xFF);

    for (int i = 0; i < padding_size; i++) data_block[pos++] = 0x00;
    /* pos should now equal k */

    uint8_t parity[NGHAM_MAX_PARITY];
    rs_encode(data_block, k, nsym, parity);

    int out_pos = 0;
    out_packet[out_pos++] = 0xAA; out_packet[out_pos++] = 0xAA;
    out_packet[out_pos++] = 0xAA; out_packet[out_pos++] = 0xAA;
    out_packet[out_pos++] = 0x5D; out_packet[out_pos++] = 0xE6;
    out_packet[out_pos++] = 0x2A; out_packet[out_pos++] = 0x7E;
    out_packet[out_pos++] = cfg->tag[0];
    out_packet[out_pos++] = cfg->tag[1];
    out_packet[out_pos++] = cfg->tag[2];
    memcpy(&out_packet[out_pos], data_block, k); out_pos += k;
    memcpy(&out_packet[out_pos], parity, nsym);  out_pos += nsym;

    return out_pos;
}

#endif /* NGHAM_H */
```

---

## 5. Full Source Code -- `src/main.c` (all 3 tasks)

```c
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "payload_chunk.h"
#include "ngham.h"

QueueHandle_t xQueue1_RawChunks;
QueueHandle_t xQueue2_OrderedChunks;

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

            xQueueSend(xQueue1_RawChunks, &chunk, portMAX_DELAY);

            offset          += this_chunk_size;
            bytes_remaining -= this_chunk_size;

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    printf("[Task1-PayloadReader] all occultations fragmented. Task finished.\n");
    fflush(stdout);

    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
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

/* ---------- TASK 3: NGHam Encoder ---------- */
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

            uint8_t ngham_payload[64];
            int p = 0;
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id >> 8);
            ngham_payload[p++] = (uint8_t)(chunk.occultation_id & 0xFF);
            ngham_payload[p++] = chunk.sequence_number;
            ngham_payload[p++] = chunk.total_chunks;
            ngham_payload[p++] = chunk.data_size;
            memcpy(&ngham_payload[p], chunk.data, chunk.data_size);
            p += chunk.data_size;

            uint8_t ngham_packet[300];
            int packet_len = ngham_build_packet(ngham_payload, (uint8_t)p, ngham_packet);

            if (packet_len < 0)
            {
                printf("[Task3-NGHamEncoder] ERROR: payload too large to encode!\n");
                fflush(stdout);
                continue;
            }

            printf("[Task3-NGHamEncoder] occ=%d seq=%d/%d -> NGHam packet (%d bytes): ",
                   chunk.occultation_id, chunk.sequence_number, chunk.total_chunks, packet_len);
            for (int i = 0; i < packet_len; i++) printf("%02X ", ngham_packet[i]);
            printf("\n");
            fflush(stdout);
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

int main(void)
{
    printf("=== CubeSat Packet Pipeline - Task 1 + 2 + 3 Test ===\n\n");
    fflush(stdout);

    xQueue1_RawChunks     = xQueueCreate(10, sizeof(PayloadChunk_t));
    xQueue2_OrderedChunks = xQueueCreate(10, sizeof(PayloadChunk_t));

    xTaskCreate(vTask1_PayloadReader,  "Task1", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask2_FifoManager,    "Task2", configMINIMAL_STACK_SIZE * 2, NULL, 1, NULL);
    xTaskCreate(vTask3_NGHamEncoder,   "Task3", configMINIMAL_STACK_SIZE * 4, NULL, 1, NULL);

    vTaskStartScheduler();

    for (;;);
    return 0;
}

void vAssertCalled(const char *pcFile, unsigned long ulLine)
{
    printf("ASSERT FAILED: %s, line %lu\n", pcFile, ulLine);
    fflush(stdout);
    for (;;);
}

void vApplicationMallocFailedHook(void)
{
    printf("MALLOC FAILED! Out of heap memory.\n");
    fflush(stdout);
    for (;;);
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void) xTask;
    printf("STACK OVERFLOW in task: %s\n", pcTaskName);
    fflush(stdout);
    for (;;);
}
```

*(Note: `payload_chunk.h`, `FreeRTOSConfig.h`, and `Makefile` are unchanged from Task 1/2 -- see `task1.md` for their full content. Task 3's stack size was increased to `configMINIMAL_STACK_SIZE * 4` because its local arrays -- `ngham_payload[64]`, `ngham_packet[300]`, and internal `data_block[256]`/`parity[32]` buffers inside `ngham_build_packet` -- need more stack space than Task 1/2 required.)*

---

## 6. Line-by-Line Explanation of `ngham.h`

### 6.1 CRC16-CCITT Function

```c
static uint16_t ngham_crc16(const uint8_t *data, int len)
{
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= ((uint16_t)data[i]) << 8;
        for (int b = 0; b < 8; b++)
        {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return (uint16_t)(crc ^ 0xFFFF);
}
```

This is the standard bit-by-bit CRC16-CCITT algorithm, configured exactly per the NGHam specification: initial value `0xFFFF`, polynomial `0x1021`, and a final XOR with `0xFFFF`. It processes one byte at a time, XORing it into the top of a 16-bit register, then shifting 8 times, XORing with the polynomial whenever the top bit is set. This produces a "fingerprint" of the input data that the receiver can recompute and compare, to detect (not correct) transmission errors quickly, before falling back to the slower Reed-Solomon correction.

### 6.2 Size Tag Table

```c
static const NGHamSizeConfig_t ngham_size_table[7] = { ... };
```

A direct C representation of the 7-row table from the NGHam specification (Section 3 of this document). Each entry bundles together the 3-byte tag codeword, the total/data/parity byte counts for that Reed-Solomon scheme, and the maximum payload size that fits in that category. Storing this as one array of structs (rather than several separate parallel arrays) keeps all the related numbers for a given "size class" together and reduces the chance of accidentally mismatching values between tables.

### 6.3 Galois Field GF(256) Setup

```c
static void gf_init(void)
{
    int x = 1;
    for (int i = 0; i < 255; i++)
    {
        gf_exp[i] = (uint8_t)x;
        gf_log[(uint8_t)x] = (uint8_t)i;
        x <<= 1;
        if (x & 0x100) x ^= 0x11D;
    }
    for (int i = 255; i < 512; i++) gf_exp[i] = gf_exp[i - 255];
    gf_log[0] = 0;
    gf_initialized = 1;
}
```

Reed-Solomon math doesn't work with ordinary integer arithmetic -- it operates in a special mathematical structure called a **Galois Field**, specifically GF(256) (256 possible byte values, 0-255). In this field, addition is just XOR, but multiplication is more involved and would be very slow to compute directly for every operation.

The standard trick (used here) is to precompute two lookup tables once, at startup:
- `gf_exp[i]` -- what byte value corresponds to "the field's generator element raised to the power i"
- `gf_log[x]` -- the inverse: given a byte value x, what power of the generator produces it

Once these tables exist, **multiplication becomes just an addition of exponents** (a well-known mathematical shortcut, exactly like how multiplying two numbers can be done by adding their logarithms):

```c
static uint8_t gf_mul(uint8_t a, uint8_t b)
{
    if (a == 0 || b == 0) return 0;
    return gf_exp[gf_log[a] + gf_log[b]];
}
```

`x ^= 0x11D` in the setup loop is the specific "reduction" step that keeps values inside the 256-element field, using `0x11D` as this implementation's field-defining polynomial (see Section 12 for the caveat about this versus NGHam's official `0x187`).

### 6.4 Reed-Solomon Generator Polynomial

```c
static void rs_generator_poly(int nsym, uint8_t *gen)
{
    memset(gen, 0, (size_t)(nsym + 1));
    gen[0] = 1;
    for (int i = 0; i < nsym; i++)
    {
        gen[i + 1] = 1;
        for (int j = i; j > 0; j--)
        {
            gen[j] = (uint8_t)(gen[j - 1] ^ gf_mul(gen[j], gf_exp[i]));
        }
        gen[0] = gf_mul(gen[0], gf_exp[i]);
    }
}
```

This builds a special polynomial (a mathematical expression, not just a number) that encodes exactly how many parity bytes we want (`nsym`). Conceptually, it's built by repeatedly multiplying together `nsym` small factors of the form `(x - root)`, where each `root` is a different power of the field's generator element. This is directly analogous to how, in ordinary algebra, you'd construct a polynomial like `(x-2)(x-3)(x-5)` to have specific roots at 2, 3, and 5 -- except here it's happening inside GF(256) arithmetic instead of ordinary numbers.

### 6.5 The Actual RS Encoding Step

```c
static void rs_encode(const uint8_t *data, int k, int nsym, uint8_t *parity)
{
    ...
    for (int i = 0; i < k; i++)
    {
        uint8_t feedback = (uint8_t)(data[i] ^ parity[0]);
        ...
    }
}
```

This performs what's called **systematic polynomial division** -- treating the `k` data bytes as coefficients of one giant polynomial, and computing the remainder after dividing that polynomial by the generator polynomial from Section 6.4. The remainder (which ends up being exactly `nsym` bytes) becomes the parity data. This particular loop structure is a classic "shift-register" style implementation, processing one input byte at a time and updating a small rolling `parity[]` buffer -- it never needs to hold the entire, huge intermediate polynomial in memory at once, which is efficient and matches how this kind of encoder would typically run on real embedded hardware.

### 6.6 `ngham_build_packet()` -- Tying It All Together

```c
static int ngham_build_packet(const uint8_t *payload, uint8_t payload_len, uint8_t *out_packet)
{
    int idx = -1;
    for (int i = 0; i < 7; i++)
        if (payload_len <= ngham_size_table[i].max_payload) { idx = i; break; }
    if (idx < 0) return -1;
    ...
}
```

This is the main entry point Task 3 actually calls. It:
1. Loops through the 7 Size Tag options, picking the **smallest** category that can still fit the given payload (this naturally minimizes wasted overhead per packet).
2. Computes the padding size needed to fill out the chosen Reed-Solomon block exactly.
3. Assembles the Header+Payload+CRC+Padding into one `data_block` buffer.
4. Runs `rs_encode()` on that buffer to get parity bytes.
5. Concatenates Preamble + Sync Word + Size Tag + data_block + parity into the final output buffer, returning its total length.

---

## 7. Line-by-Line Explanation of Task 3 in `main.c`

### 7.1 Serialization

```c
uint8_t ngham_payload[64];
int p = 0;
ngham_payload[p++] = (uint8_t)(chunk.occultation_id >> 8);
ngham_payload[p++] = (uint8_t)(chunk.occultation_id & 0xFF);
ngham_payload[p++] = chunk.sequence_number;
ngham_payload[p++] = chunk.total_chunks;
ngham_payload[p++] = chunk.data_size;
memcpy(&ngham_payload[p], chunk.data, chunk.data_size);
p += chunk.data_size;
```

`chunk.occultation_id` is a 16-bit value (`uint16_t`), but raw bytes/network protocols work with 8-bit chunks, so we split it into its high byte (`>> 8`) and low byte (`& 0xFF`) and store them separately -- this is called "big-endian" byte ordering (most-significant byte first), matching how NGHam's own multi-byte fields are documented. The remaining fields (`sequence_number`, `total_chunks`, `data_size`) are all single bytes already, so they're copied directly. Finally, the actual chunk data bytes are appended via `memcpy`. The result, `p`, tells us exactly how many bytes this serialized payload occupies (5 metadata bytes + however many actual data bytes were in this chunk).

### 7.2 Calling the Encoder and Handling Errors

```c
uint8_t ngham_packet[300];
int packet_len = ngham_build_packet(ngham_payload, (uint8_t)p, ngham_packet);

if (packet_len < 0)
{
    printf("[Task3-NGHamEncoder] ERROR: payload too large to encode!\n");
    fflush(stdout);
    continue;
}
```

`ngham_build_packet()` returns `-1` if the payload didn't fit any of the 7 defined size categories (i.e., larger than 220 bytes) -- this is defensive error handling in case a future chunk size change accidentally produces an oversized payload. In our current setup (`CHUNK_DATA_SIZE=32`, plus 5 metadata bytes = max 37 bytes), this branch should never actually trigger, but it's good practice to check the return value rather than assume success.

### 7.3 "Transmitting" (Printing) the Packet

```c
printf("[Task3-NGHamEncoder] occ=%d seq=%d/%d -> NGHam packet (%d bytes): ",
       chunk.occultation_id, chunk.sequence_number, chunk.total_chunks, packet_len);
for (int i = 0; i < packet_len; i++) printf("%02X ", ngham_packet[i]);
printf("\n");
```

Since we don't have a real radio to hand this off to yet, we simply print every byte of the finished packet in two-digit uppercase hex (`%02X`), separated by spaces -- this is a completely standard, human-readable way to display raw binary data, and lets us (and Lucas, if he reviews it) visually inspect the exact bytes that would be transmitted.

---

## 8. Deep Worked Example -- One Real Chunk, Traced Byte-by-Byte

We trace the **first chunk of occultation 1000** (`seq=1/4`, `data_size=32`, data bytes `0x10` through `0x2F`) completely through Task 3, with every number below independently verified (see Section 10).

**Step 0 -- Input chunk (from Task 1/2):**
```
occultation_id  = 1000
sequence_number = 1
total_chunks    = 4
data_size       = 32
data (hex)      = 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F
```

**Step 1 -- Serialization:**
```
payload length = 37 bytes
payload (hex)  = 03 E8 01 04 20 | 10 11 12 13 ... 2F
                 metadata(5B)     data(32B)
```
`1000` decimal = `0x03E8` hex, split as `03` (high byte) and `E8` (low byte) -- confirming occultation_id round-trips correctly into the byte stream.

**Step 2 -- Size Tag selection:**
```
payload_len = 37 <= max_payload = 60 for Size 2  -->  Size 2 selected
Tag bytes = (77, 218, 87) = 4D DA 57 in hex
RS config = RS(79, 63)  -->  nsym (parity bytes) = 16
```

**Step 3 -- Header / padding:**
```
padding_size = k - 3 - payload_len = 63 - 3 - 37 = 23
header = 23 = 0x17
```

**Step 4 -- CRC + full data block:**
```
CRC16-CCITT (over header+payload, 38 bytes) = 0x56EC
data_block (63 bytes) =
  17                                     <- header
  03 E8 01 04 20                         <- metadata
  10 11 ... 2F                           <- 32 data bytes
  56 EC                                  <- CRC
  00 00 ... 00  (23 zero bytes)          <- padding
```

**Step 5 -- Reed-Solomon parity:**
```
parity (16 bytes) = FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B
```

**Step 6 -- Final assembled packet (90 bytes total):**
```
AA AA AA AA                                     preamble
5D E6 2A 7E                                     sync word
4D DA 57                                        size tag
17 03 E8 01 04 20 10 11 12 13 14 15 16 17 18    header + metadata + data (part 1)
19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27    data (part 2)
28 29 2A 2B 2C 2D 2E 2F                         data (part 3)
56 EC                                            CRC
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00    padding (part 1)
00 00 00 00 00 00 00 00                          padding (part 2)
FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B  Reed-Solomon parity
```

---

## 9. Verified Real Test Output (Full Run, All 9 Chunks)

**Commands run:**
```bash
make clean
make
./build/cubesat_pipeline
```

**Actual output obtained (complete):**
```
=== CubeSat Packet Pipeline - Task 1 + 2 + 3 Test ===

[Task1-PayloadReader] occultation 1000: 100 bytes -> 4 chunk(s)
[Task2-FifoManager] started.
[Task2-FifoManager] forwarding  occ=1000  seq=1/4  bytes=32
[Task3-NGHamEncoder] started.
[Task3-NGHamEncoder] occ=1000 seq=1/4 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 E8 01 04 20 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 56 EC 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B
[Task2-FifoManager] forwarding  occ=1000  seq=2/4  bytes=32
[Task3-NGHamEncoder] occ=1000 seq=2/4 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 E8 02 04 20 30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F AD 36 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 CB B5 44 9A 28 DB 7B 36 FC 3C 92 98 45 AA 45 3F
[Task2-FifoManager] forwarding  occ=1000  seq=3/4  bytes=32
[Task3-NGHamEncoder] occ=1000 seq=3/4 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 E8 03 04 20 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D 5E 5F 60 61 62 63 64 65 66 67 68 69 6A 6B 6C 6D 6E 6F 37 46 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 B8 D4 7A BE 51 C8 FF 5A 3A 66 1B CA AE 87 C3 59
[Task2-FifoManager] forwarding  occ=1000  seq=4/4  bytes=4
[Task3-NGHamEncoder] occ=1000 seq=4/4 -> NGHam packet (58 bytes): AA AA AA AA 5D E6 2A 7E 3B 49 CD 13 03 E8 04 04 04 70 71 72 73 BB DB 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 11 7C 1B 71 21 31 57 AC 66 18 DC 21 DF C3 40 AF
[Task1-PayloadReader] occultation 1001: 45 bytes -> 2 chunk(s)
[Task2-FifoManager] forwarding  occ=1001  seq=1/2  bytes=32
[Task3-NGHamEncoder] occ=1001 seq=1/2 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 E9 01 02 20 40 41 42 43 44 45 46 47 48 49 4A 4B 4C 4D 4E 4F 50 51 52 53 54 55 56 57 58 59 5A 5B 5C 5D 5E 5F 09 82 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 7C 71 EB 39 92 F7 A4 E6 95 39 79 A0 86 54 BF B5
[Task2-FifoManager] forwarding  occ=1001  seq=2/2  bytes=13
[Task3-NGHamEncoder] occ=1001 seq=2/2 -> NGHam packet (58 bytes): AA AA AA AA 5D E6 2A 7E 3B 49 CD 0A 03 E9 02 02 0D 60 61 62 63 64 65 66 67 68 69 6A 6B 6C F3 69 00 00 00 00 00 00 00 00 00 00 0E 86 EC E5 66 07 7D 9C 4D 53 5C 91 22 53 43 FB
[Task1-PayloadReader] occultation 1002: 70 bytes -> 3 chunk(s)
[Task2-FifoManager] forwarding  occ=1002  seq=1/3  bytes=32
[Task3-NGHamEncoder] occ=1002 seq=1/3 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 EA 01 03 20 70 71 72 73 74 75 76 77 78 79 7A 7B 7C 7D 7E 7F 80 81 82 83 84 85 86 87 88 89 8A 8B 8C 8D 8E 8F 17 79 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 E0 59 05 98 0B D8 A8 74 42 A7 4E 3F 3B 28 E3 FD
[Task2-FifoManager] forwarding  occ=1002  seq=2/3  bytes=32
[Task3-NGHamEncoder] occ=1002 seq=2/3 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 EA 02 03 20 90 91 92 93 94 95 96 97 98 99 9A 9B 9C 9D 9E 9F A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 AA AB AC AD AE AF 06 08 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 83 B3 96 64 1A 2F B4 65 55 93 FC DD 0B 55 45 0B
[Task2-FifoManager] forwarding  occ=1002  seq=3/3  bytes=6
[Task3-NGHamEncoder] occ=1002 seq=3/3 -> NGHam packet (58 bytes): AA AA AA AA 5D E6 2A 7E 3B 49 CD 11 03 EA 03 03 06 B0 B1 B2 B3 B4 B5 A9 4D 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 B8 36 7C BF 21 8C 0E A5 9D BF 5F 60 16 54 6A 65
[Task1-PayloadReader] all occultations fragmented. Task finished.
[Task2-FifoManager] no more chunks arriving. Total forwarded: 9
[Task3-NGHamEncoder] no more chunks arriving. Total encoded: 9
```

**Summary table -- all 9 chunks, all 9 NGHam packets:**

| Occultation | Seq | data_size | Serialized payload len | Size class | Packet length |
|---|---|---|---|---|---|
| 1000 | 1/4 | 32 | 37 | Size 2 | 90 bytes |
| 1000 | 2/4 | 32 | 37 | Size 2 | 90 bytes |
| 1000 | 3/4 | 32 | 37 | Size 2 | 90 bytes |
| 1000 | 4/4 | 4  | 9  | Size 1 | 58 bytes |
| 1001 | 1/2 | 32 | 37 | Size 2 | 90 bytes |
| 1001 | 2/2 | 13 | 18 | Size 1 | 58 bytes |
| 1002 | 1/3 | 32 | 37 | Size 2 | 90 bytes |
| 1002 | 2/3 | 32 | 37 | Size 2 | 90 bytes |
| 1002 | 3/3 | 6  | 11 | Size 1 | 58 bytes |

Every full 32-byte chunk (serialized to 37 bytes) correctly lands in Size 2 (90-byte packets); every smaller "remainder" chunk correctly lands in Size 1 (58-byte packets). The encoder dynamically selects the right size class per chunk, exactly as designed. `Total forwarded: 9` (Task 2) and `Total encoded: 9` (Task 3) confirm no chunk was lost, duplicated, or skipped across the full 3-stage pipeline.

---

## 10. Independent Verification (Cross-Checked Against Python Simulation)

To confirm correctness beyond visual inspection, the exact same algorithm (serialization, size tag selection, header/padding math, CRC16-CCITT, Reed-Solomon generator polynomial and encoding) was independently re-implemented in Python and run against the same first chunk (occ=1000, seq=1/4).

**Result: 100% exact byte-for-byte match**, including the 16-byte Reed-Solomon parity tail (`FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B`), across all 90 bytes.

A second independent check was run against chunk 4 (occ=1000, seq=4/4, the smallest chunk, which falls into a different Size category than the others):

| Check | Independently computed | Actual program output | Match? |
|---|---|---|---|
| Size class | Size 1 | Size 1 (tag `3B 49 CD`) | Yes |
| Header byte | `0x13` | `13` | Yes |
| CRC | `0xBBDB` | `BB DB` | Yes |
| Total packet length | 58 bytes | "(58 bytes)" | Yes |

This double-checks that the Size Tag selection logic correctly and automatically adapts per-chunk (not just for the common case), and that the CRC and header math remain correct even in the smaller, differently-configured Size 1 category.

---

## 11. What Is Real vs. Simulated / Approximated Right Now

| Part | Real, Simulated, or Approximated? |
|---|---|
| FreeRTOS tasks/scheduling (all 3 tasks) | Real -- same code will run unchanged on the real MSP430 later |
| Queue behavior (both queues) | Real -- same code will run unchanged later |
| Payload data (`fake_payload[]`) | Simulated -- invented bytes; later replaced with real payload memory reads |
| The 3 occultation events | Simulated -- invented for testing |
| NGHam packet structure (preamble, sync, size tag, header, padding) | Real -- matches the official NGHam specification exactly |
| CRC16-CCITT | Real -- exact parameters match the official NGHam specification |
| Reed-Solomon *mechanism* (systematic encoding via generator polynomial division) | Real, genuine, working Reed-Solomon math |
| Reed-Solomon *exact field parameters* (GF polynomial `0x11D` vs. official NGHam's `0x187`) | Approximated -- see Section 12 |
| Scrambling (CCSDS XOR step) | Not yet implemented -- see Section 12 |
| The microcontroller (Mac instead of MSP430) | Simulated -- using FreeRTOS's POSIX port |
| "Transmission" (printing hex to terminal) | Simulated -- would be replaced with an actual radio driver call |

---

## 12. Known Limitations and Open Questions for Lucas

1. **Chunk size vs. Size Tag category:** our fixed 32-byte chunk size (plus 5 bytes of serialized metadata = 37 bytes) lands in NGHam Size 2 (max 60 bytes) rather than Size 1 (max 28 bytes). This means every full chunk uses a 90-byte packet even though the useful payload is small -- worth discussing whether a smaller chunk size (e.g., 23 bytes of data, so that 23+5=28 fits exactly in Size 1) would be more bandwidth-efficient, or whether 32 bytes was chosen deliberately for another reason.

2. **Reed-Solomon field parameters:** this implementation uses the common tutorial/demo GF(256) primitive polynomial `0x11D`. The official NGHam specification calls for field polynomial `0x187` with a specific first consecutive root (index 112) and primitive element (11). The Reed-Solomon *mechanism* implemented here is fully correct and functional, but for byte-exact compatibility with SpaceLab's actual ground station decoder (or the reference PyNGHam/C implementation), these specific constants would need to be matched exactly and validated against known reference test vectors.

3. **Scrambling not yet implemented:** the NGHam specification calls for one final step -- XOR-scrambling the RS block with a fixed table (per CCSDS 131.0-B-3) before transmission, to avoid long runs of identical bits on the radio link. This is not yet implemented in `ngham_build_packet()`; the packets produced so far are otherwise complete and correctly structured, but would need this final scrambling pass before being considered fully spec-compliant for real transmission.

4. **No decoding/verification loop yet:** we have only built the *encoder* direction. We have not yet written a decoder to confirm that feeding a (possibly corrupted) packet back through Reed-Solomon correction actually recovers the original data -- this would be a valuable next validation step before trusting this implementation further.

---

## 13. Next Steps

- **Address scrambling:** implement the CCSDS-style XOR scrambling step as the final stage of `ngham_build_packet()`.
- **Align Reed-Solomon parameters:** confirm the exact GF polynomial, generator root, and primitive element used by SpaceLab's reference NGHam implementation, and update `ngham.h` to match exactly.
- **Build a decoder for self-verification:** write a companion Reed-Solomon *decoder* to confirm packets can be correctly reconstructed even after simulating byte corruption -- proving the error-correction actually works, not just that parity bytes are produced.
- **Review with Lucas:** share this document and the verified output, discuss the open questions in Section 12, and confirm whether chunk size, RS parameters, or the serialization format need adjustment.
- **Port to real MSP430F6659 hardware** (via Code Composer Studio) once the above is confirmed, replacing simulated payload memory reads and the simulated "transmit" print statement with real hardware/radio driver calls.

---

*This document reflects the complete Task 1 + Task 2 + Task 3 pipeline as implemented and verified in the FreeRTOS POSIX simulator on macOS. This satisfies the core architecture Lucas requested: three independent FreeRTOS tasks (fragment -> FIFO -> NGHam encapsulate), communicating via queues, tested first in simulation before any real hardware involvement.*

