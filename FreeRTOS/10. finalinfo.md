# CubeSat Packet Pipeline — Complete Learning & Progress Documentation

**Project:** SpaceLab UFSC — EDC/TTC Internship (GOLDS-UFSC CubeSat Mission)
**Team:** Hardik Singhal, Shivansh Gupta, Amrit Mishra (LNMIIT, India)
**Mentor:** Lucas Ryan Carneiro
**Purpose of this document:** A from-zero, self-contained reference covering every concept, tool, and step involved in this task — written so that even someone with no prior background could read it and understand what we did and why.

---

# TABLE OF CONTENTS

1. Project Context — What Are We Actually Building?
2. Part A — NGHam Protocol (Full Theory)
3. Part B — Serial Port Protocol / SPP (Full Theory)
4. Part C — Extension Packets (Full Theory)
5. Part D — FreeRTOS (Full Theory, From Zero)
6. Part E — Why We Chose This Development Setup
7. Part F — Complete File Structure Explained
8. Part G — Step-by-Step Setup Log (What We Actually Did)
9. Part H — Where We Are Right Now
10. Part I — Next Steps

---

# 1. PROJECT CONTEXT — What Are We Actually Building?

SpaceLab UFSC builds real CubeSats (small satellites) as part of the GOLDS-UFSC mission. A satellite has several subsystems working together:

- **EPS** — Electrical Power System (batteries, solar panels)
- **OBDH** — On-Board Data Handling (the satellite's main "brain"/computer)
- **ACS** — Attitude Control System (keeps the satellite oriented correctly)
- **TTC / TT&C** — Telemetry, Tracking & Command (the satellite's radio — talks to Earth)
- **Payload** — the actual scientific instrument (in our case, related to an "occultation" experiment that generates data)

**Our specific assignment (from mentor Lucas):**

The payload instrument generates data (from an event called an "occultation"). This data sits in payload memory and is often **too large to transmit in a single radio packet**. So we need to build a pipeline that:

1. **Reads** the payload data from memory
2. **Splits (fragments)** it into small chunks, since each radio packet can only hold a limited number of bytes
3. **Tags** each chunk with metadata: which event it came from (occultation ID), which piece of the sequence it is (sequence number), how many total pieces exist, and how big the chunk is
4. **Queues** the chunks in the correct order (FIFO — First In, First Out) so nothing arrives at the radio out of sequence
5. **Wraps (encapsulates)** each chunk into an **NGHam packet** — the actual radio-ready format with error correction — ready for transmission

Because the real firmware runs on **FreeRTOS** (a real-time operating system for embedded devices), each of these steps (read/fragment, queue, encapsulate) must be built as a **separate, independently-running FreeRTOS task**, all working together like an assembly line.

Lucas's recommendation: **don't touch real satellite hardware yet.** First simulate everything in software (fake/simulated payload memory) to prove the logic works, and only port to the real microcontroller (MSP430) later.

This document captures everything we needed to learn to get here, and everything we've built so far.

---

# 2. PART A — NGHam PROTOCOL (Full Theory)

## 2.1 What Problem Does NGHam Solve?

When a satellite transmits data to Earth by radio, the signal travels through space and atmosphere and can get corrupted by noise, interference, or weak signal strength. A "packet" is simply a structured chunk of data with a beginning, an end, and some extra information to help the receiver understand and verify it.

**NGHam (Next Generation Ham Radio)** is a communication protocol — a strict set of rules for how data should be packaged before transmission — designed originally for the NUTS-1 CubeSat (Norwegian University of Science and Technology). It improves on an older protocol called AX.25 by adding **Forward Error Correction (FEC)**, which means: even if some bytes get corrupted during transmission, the receiver can often **reconstruct the original data** without needing to ask for a re-transmission.

Think of it like sending a puzzle through the mail with a few spare pieces included — if a couple of pieces get lost or damaged, you can still figure out what the picture was supposed to be.

## 2.2 The Full NGHam Packet Structure

Every NGHam packet is built from these fields, always in this order:

```
┌───────────┬────────────┬──────────┬────────────────────────────────────────────┐
│ PREAMBLE  │ SYNC WORD  │ SIZE TAG │           REED-SOLOMON (RS) BLOCK           │
│  4 bytes  │  4 bytes   │ 3 bytes  │  Header(1B) Payload(1-220B) CRC(2B)         │
│           │            │          │  Padding(0-31B) Parity(16 or 32B)           │
└───────────┴────────────┴──────────┴────────────────────────────────────────────┘
```

### 2.2.1 Preamble (4 bytes)

- **Value:** `0xAA 0xAA 0xAA 0xAA`
- **Binary pattern:** `10101010 10101010 10101010 10101010`
- **Purpose:** This alternating 1-0-1-0 pattern lets the receiver's radio hardware "wake up" and synchronize its clock/timing to the incoming signal, so it knows exactly when each bit starts and ends. Think of it as a drum-roll warning that says "something is about to arrive — get ready."

### 2.2.2 Sync Word (4 bytes)

- **Value:** `0x5D 0xE6 0x2A 0x7E` (32 bits total)
- **Purpose:** This is a fixed, unique bit pattern that marks the **exact start** of the real packet data. After detecting the preamble (which just says "something's coming"), the receiver now specifically watches for this Sync Word — once it sees this exact sequence, it knows "the packet data starts right after this."
- Why is this needed in addition to the preamble? The preamble is a repeating pattern (so its exact "start" is ambiguous — you could sync to any of the repeats). The Sync Word is unique and non-repeating, so there's zero ambiguity about where the packet actually begins.

### 2.2.3 Size Tag (3 bytes = 24 bits)

- **Purpose:** Before the receiver even starts decoding the payload, it needs to know: how big is this packet, and which Reed-Solomon error-correction scheme was used? The Size Tag encodes this in a specially chosen 24-bit codeword.
- **Why 24 bits for just "1 of 7 options"?** This seems wasteful (you'd only need 3 bits to represent 7 options), but it's intentional: the 7 codewords are chosen so that they are all very different from each other in terms of bit-patterns (a property called **Hamming distance** — here, kept at 13 bits). This means even if a few bits get flipped by noise during transmission, the receiver can still correctly guess which of the 7 size options was intended, because no combination of a few bit-flips could turn one valid codeword into another valid codeword. It's redundancy used specifically for **robustness**, not just information content.

The 7 possible Size Tag values and what they mean:

| Size # | Tag (3 bytes, decimal) | RS Configuration | Max Payload | Parity Bytes |
|--------|------------------------|-------------------|-------------|----------------|
| 1 | 59, 73, 205   | RS(47, 31)   | up to 28 bytes  | 16 |
| 2 | 77, 218, 87   | RS(79, 63)   | up to 60 bytes  | 16 |
| 3 | 118, 147, 154 | RS(111, 95)  | up to 92 bytes  | 16 |
| 4 | 155, 180, 174 | RS(159, 127) | up to 124 bytes | 32 |
| 5 | 160, 253, 99  | RS(191, 159) | up to 156 bytes | 32 |
| 6 | 214, 110, 249 | RS(223, 191) | up to 188 bytes | 32 |
| 7 | 237, 39, 52   | RS(255, 223) | up to 220 bytes | 32 |

**How to use this table:** if your actual payload is, say, 45 bytes, you pick the smallest size category that can still hold it — Size 2 (up to 60 bytes) — and use its exact tag bytes, RS scheme, and parity length for the rest of the packet construction.

### 2.2.4 The Reed-Solomon (RS) Block — the "body" of the packet

This is the main content area, and it's itself divided into 5 sub-fields: Header, Payload, CRC, Padding, Parity.

#### (a) Header (1 byte)

| Bits | Meaning |
|------|---------|
| 7–6  | Reserved (unused, always 0) |
| 5    | Extension flag (0 = normal packet, 1 = this packet contains NGHam Extension sub-packets — see Part C) |
| 4–0  | Padding size — how many zero-padding bytes were added (0 to 31) |

#### (b) Payload (1 to 220 bytes)

This is the **actual useful data** you want to send — in our project's case, one fragment/chunk of the payload instrument's data. The maximum any single NGHam packet can carry is 220 bytes; if you have more data than that, you must split it across **multiple separate packets** (which is exactly why Task 1 in our pipeline needs to fragment large payloads).

#### (c) CRC — Cyclic Redundancy Check (2 bytes)

- **Algorithm:** CRC16-CCITT
- **Configuration:** Polynomial `0x1021`, Initial value `0xFFFF`, Final XOR `0xFFFF`
- **Calculated over:** the Header byte + the Payload bytes (NOT the padding, NOT the parity)
- **Purpose:** A CRC is a mathematical "fingerprint" of the data. The sender calculates it and attaches it. The receiver recalculates the same fingerprint from the data it received and compares. If they match, the data almost certainly arrived correctly. If they don't match, something got corrupted.
- **Performance trick:** If the CRC matches, NGHam skips running the (much slower) Reed-Solomon correction algorithm entirely, since the data is already known to be correct. Reed-Solomon is only invoked as a "rescue" step when the CRC check fails.

#### (d) Padding (0 to 31 bytes, all zero bytes: `0x00`)

Reed-Solomon error correction math requires the data block to be an **exact fixed size** (determined by the RS scheme chosen, e.g. `RS(47,31)` expects exactly 31 data bytes). If your actual Header+Payload+CRC content is smaller than that fixed size, you pad the rest with zeros to fill it out exactly. The Header byte records exactly how many padding bytes were added, so the receiver knows to strip them back off after decoding.

#### (e) Parity Data (16 or 32 bytes, depending on Size Tag)

This is the actual **error-correction magic** — extra bytes calculated by the Reed-Solomon algorithm from the data bytes, which allow the receiver to detect AND fix corrupted bytes, without needing to re-request the transmission.

**Understanding RS(n, k) notation:**
- `n` = total number of bytes in the RS codeword (data + parity)
- `k` = number of actual data bytes
- `n - k` = number of parity bytes
- **General rule of thumb:** Reed-Solomon can correct up to `(n-k)/2` corrupted bytes. E.g., `RS(47,31)` has `47-31 = 16` parity bytes, so it can fix up to 8 corrupted bytes anywhere in that 47-byte block.

**Technical RS parameters used by NGHam** (you don't need to hand-calculate these — a library does it — but good to know they exist):
- Symbol size: 8 bits
- Galois Field (GF) polynomial: `0x187`
- First root of the generator polynomial: 112
- Primitive element: 11
- Number of roots (= parity bytes): 16 or 32, per the table above

## 2.3 Scrambling (applied right before transmission)

After the full RS Block (Header+Payload+CRC+Padding+Parity) is assembled, one more step happens: **scrambling**.

**Why?** Radio hardware and receivers work best when the transmitted signal has frequent transitions between 1s and 0s (this helps the receiver's clock recovery stay synchronized). If your actual data happens to contain long runs of the same bit (e.g., many zero bytes in a row, which is common with padding), this could confuse the receiver's timing.

**How it works:** Every byte of the RS Block is XORed with a byte from a pre-defined, fixed "scrambling table" (derived from a specific polynomial defined in the CCSDS 131.0-B-3 space communication standard). Because XOR is reversible, the receiver — which has the exact same table — simply XORs again with the same table to recover the original bytes perfectly. It's not encryption (anyone can descramble it, since the table is public and fixed) — it's purely to guarantee good bit-transition behavior on the radio link.

## 2.4 Complete Worked Example — Packing "Hello" into an NGHam Packet

Let's trace through building a real packet, step by step, for the payload `"Hello"` (5 ASCII bytes: `0x48 0x65 0x6C 0x6C 0x6F`).

**Step 1 — Choose Size Tag:**
5 bytes fits easily in Size 1 (max 28 bytes) → Tag = `59, 73, 205` → RS(47,31) → 16 parity bytes

**Step 2 — Calculate padding:**
Max data bytes for RS(47,31) = 31. We need: Header(1) + Payload(5) + CRC(2) + Padding(?) = 31
→ Padding = 31 - 1 - 5 - 2 = 23 bytes of `0x00`

**Step 3 — Build Header byte:**
Padding size = 23 = `0b10111` in the lower 5 bits, extension bit = 0, reserved bits = 0
→ Header = `0x17`

**Step 4 — Assemble Header + Payload:**
`0x17 0x48 0x65 0x6C 0x6C 0x6F` (6 bytes so far)

**Step 5 — Calculate CRC16-CCITT** over those 6 bytes → (example result) `0x1A 0x2B`

**Step 6 — Assemble Header + Payload + CRC + Padding (must total 31 bytes):**
`0x17 | 48 65 6C 6C 6F | 1A 2B | 00×23` = 31 bytes exactly ✓

**Step 7 — Run Reed-Solomon RS(47,31) encoder** on those 31 bytes → produces 16 parity bytes

**Step 8 — Full RS Block = 31 data bytes + 16 parity bytes = 47 bytes total**

**Step 9 — Scramble** all 47 bytes (XOR with the fixed table)

**Step 10 — Final complete packet:**
`Preamble(4) + SyncWord(4) + SizeTag(3) + ScrambledRSBlock(47)` = **58 bytes total**, ready to transmit.

---

# 3. PART B — SERIAL PORT PROTOCOL / SPP (Full Theory)

## 3.1 Why Does SPP Exist, If We Already Have NGHam?

NGHam (Part A) defines how data looks **over the air** (the radio-to-radio wireless link). But before data ever reaches the radio to be transmitted, it usually travels over a **wired serial connection** (like a USB cable) between a computer (or, in our case, the OBDH/onboard computer) and the radio transceiver module.

This wired link needs its own lightweight framing so the two devices (computer and radio) can tell each other things like: *"the following bytes are data you should transmit,"* or *"here's data I just received over the air, plus some signal-quality info,"* or *"this is a command for you, not data."*

**SPP (Serial Port Protocol)** is that framing — it **wraps** an NGHam packet (or a command, or status info) with a small header so the receiving device on the other end of the serial cable knows exactly what it's looking at and how to handle it.

Think of it this way: NGHam is the *sealed envelope* that travels through the air. SPP is the *shipping label and courier's instructions* used only for the local hand-off between your computer and the radio device sitting right next to it.

## 3.2 SPP Packet Structure

```
+-----------+---------+--------------+----------------+-------------+
| START TAG |  CRC    | PAYLOAD TYPE | PAYLOAD LENGTH |   PAYLOAD   |
|  1 byte   | 2 bytes |   1 byte     |    1 byte      |  n bytes    |
+-----------+---------+--------------+----------------+-------------+
```

Total size = `5 + n` bytes, where `n` is the payload length.

### 3.2.1 Start Tag (1 byte)

- **Always:** `0x24` (the ASCII character `'$'`)
- **Purpose:** marks the beginning of an SPP packet, so the receiving software can find the start of the frame in a continuous stream of serial bytes.

### 3.2.2 CRC (2 bytes)

- **Algorithm:** CRC16-CCITT, but with the polynomial used in **reversed** bit order compared to NGHam's own CRC (init `0xFFFF`, final XOR `0xFFFF`)
- **Calculated over:** everything in the packet **except** the Start Tag and the CRC field itself (i.e., over Payload Type + Payload Length + Payload)
- **CRITICAL DIFFERENCE from NGHam:** this CRC (and everything else at the SPP layer and above) is stored in **little-endian** byte order -- meaning the **least significant byte comes first**. For example, if the calculated CRC value is `0x1234`, it is stored in the packet as bytes `0x34, 0x12` (low byte first, high byte second) -- the reverse of what you might expect.

### 3.2.3 Payload Type (1 byte)

Tells the receiver what kind of content is in the Payload field:

| Value | Meaning |
|-------|---------|
| `0x00` | RF RX Packet -- data that was received over the air by the radio, being forwarded to the computer |
| `0x01` | RF TX Packet -- data from the computer that should be transmitted over the air by the radio |
| `0x02` | Local Packet -- status/telemetry generated by the radio itself (not received from the air) |
| `0x03` | Command Packet -- a text command being sent to control the radio |

### 3.2.4 Payload Length (1 byte)

A single byte stating exactly how many bytes follow in the Payload field (so the receiver knows where this packet ends).

### 3.2.5 Payload (variable size, depends on type -- detailed below)

## 3.3 The Four SPP Payload Types in Detail

### Type `0x00` -- RF RX Packet (length 4 to 223 bytes)

Data that arrived over the air, now being handed from the radio to the computer, WITH extra signal-quality metadata attached:

| Field | Size | Meaning |
|-------|------|---------|
| Time of hour (us) | 4 bytes | Timestamp within the current hour, in microseconds. Wraps back to 0 after 3,599,999,999. `0xFFFFFFFF` = not available |
| Noise floor | 1 byte | Background RF noise level. Actual dBm = (byte value) minus 200. E.g., byte `0x50` (80 decimal) -> 80-200 = **-120 dBm**. `0xFF` = not available |
| RSSI | 1 byte | Received Signal Strength Indicator -- how strong the actual signal was. Same formula: (byte value) minus 200 = dBm |
| Symbol errors | 1 byte | How many Reed-Solomon symbols had to be corrected in this packet (a health indicator of the radio link) |
| Flags | 1 byte | Bit 0: set to 1 if the data contains an NGHam Extension packet (see Part C); rest reserved |
| Data | remaining bytes (n-8) | The actual received NGHam packet content |

### Type `0x01` -- RF TX Packet (length 1 to 220 bytes)

Data the computer wants the radio to transmit over the air:

| Field | Size | Meaning |
|-------|------|---------|
| Flags | 1 byte | Bit 0: NGHam extension enabled flag |
| Data | remaining bytes (n-1) | The NGHam packet to transmit |

**This is the type our pipeline will primarily use** -- after Task 3 builds an NGHam packet, we'd wrap it as an SPP TX packet to hand it to the radio (conceptually -- in our current simulation stage we just "print"/log it instead of sending to real hardware).

### Type `0x02` -- Local Packet

Status information generated internally by the radio itself (e.g., "here's my current temperature" or "here's my current frequency setting") -- not received from the air.

| Field | Size | Meaning |
|-------|------|---------|
| Flags | 1 byte | Same as above |
| Data | remaining bytes (n-1) | Status/telemetry content |

### Type `0x03` -- Command Packet

A plain text command sent to control the radio directly (similar to typing into a command-line terminal), for example `"FREQ 144800000"` to change the operating frequency. Important detail: unlike typing in a real terminal, these commands are **not terminated** with a newline character (`\n`) or carriage return (`\r`) -- the Payload Length field alone tells the receiver where the command string ends.

## 3.4 Worked Example -- Wrapping an NGHam TX Packet in SPP

Suppose Task 3 already built a 58-byte NGHam packet (from our example in Part A) and we need to hand it to the radio for transmission:

**Step 1 -- Choose Payload Type:** `0x01` (RF TX Packet)

**Step 2 -- Build the Payload field:**
`Flags(1 byte, = 0x00) + NGHam packet (58 bytes)` = 59 bytes total payload

**Step 3 -- Payload Length field:** `0x3B` (59 in decimal)

**Step 4 -- Calculate CRC** over `[Type(0x01) + Length(0x3B) + Payload(59 bytes)]` -> e.g., result `0xABCD`

**Step 5 -- Convert CRC to little-endian:** low byte first -> `0xCD, 0xAB`

**Step 6 -- Assemble full SPP packet:**
`0x24 (start) | 0xCD 0xAB (CRC, little endian) | 0x01 (type) | 0x3B (length) | 0x00 (flags) | [58-byte NGHam packet]`

**Total SPP packet size:** `1 + 2 + 1 + 1 + 59 = 64 bytes`

---

# 4. PART C -- EXTENSION PACKETS (Full Theory)

## 4.1 What Are These For?

Sometimes you want to attach small pieces of **structured metadata** inside an NGHam payload -- things like GPS position, a callsign identifying the transmitting station, timing info, or generic statistics -- without inventing a whole new protocol. NGHam Extension Packets solve this: if the Extension flag (bit 5 of the NGHam Header, or bit 0 of the SPP Flags byte) is set, the Payload is interpreted as one or more of these small structured sub-packets, laid end-to-end.

**Note:** as documented in the official PyNGHam docs, this feature is only **partially implemented** -- some extension types aren't fully specified yet. It is **not required** for our current pipeline task, but understanding it is useful background.

## 4.2 Common Structure (all extension packet types share this)

```
+--------+---------------+--------------+
|  Type  |  Data Length  |     Data     |
| 1 byte |    1 byte     |   variable   |
+--------+---------------+--------------+
```

## 4.3 Types of Extension Packets

| Type ID | Name | Purpose | Notable Fields |
|---------|------|---------|-----------------|
| `0x00` | Data Packet | Generic arbitrary byte sequence | Just raw data |
| `0x01` | ID Packet | Identifies the transmitting station | Callsign (7B), SSID (1B), Sequence number (1B) -- always first in a packet unless relayed by another station |
| `0x02` | Status Packet | Health/telemetry snapshot | HW version, serial number, SW version, uptime, input voltage, temperature, RSSI, noise floor, packet counters (received/corrected/uncorrectable/sent) |
| `0x03` | Simple Digipeater Packet | (Not yet defined -- TODO in the spec) | -- |
| `0x04` | Position Packet | GPS-like location data | Latitude (4B), Longitude (4B), Altitude (4B), Speed over ground, Course over ground, HDOP |
| `0x05` | Time Information Packet | Precise timing reference | Time-of-hour in microseconds (4B), validity flag (1B) |
| `0x06` | Destination Packet | Where this packet is ultimately intended to go | Callsign (7B), SSID (1B) |
| `0x07`+ | Command Request / Command Reply / Request | (Not yet defined -- TODO in the spec) | -- |

**Relevance to us:** not used in the current task, but if the mission later wants to attach GPS or timing metadata to occultation data packets, this mechanism already exists and could be reused.

---

# 5. PART D -- FREERTOS (Full Theory, From Zero)

## 5.1 The Core Problem FreeRTOS Solves

Imagine a normal microcontroller program, written the simple way, with no operating system at all ("bare-metal" code):

```c
while (1) {
    read_sensor();     // takes 50ms
    blink_led();        // takes 10ms
    check_radio();       // takes 100ms
}
```

This runs strictly one step after another, forever, in a loop. Every function has to finish before the next one starts. If `check_radio()` gets stuck waiting for something (say, waiting for a slow hardware response), everything else -- including blinking the LED -- freezes too. There's no way for these jobs to happen independently or "at the same time."

In our CubeSat pipeline, we have **three genuinely independent jobs**:
1. Continuously read/fragment payload data
2. Continuously manage the FIFO queue of chunks
3. Continuously encode chunks into NGHam packets and "transmit" them

Each of these needs to keep running, on its own schedule, without waiting for the others to finish. **FreeRTOS** is a **Real-Time Operating System (RTOS)** -- a very lightweight piece of software (not like Windows/macOS, but a tiny scheduler) that runs on a microcontroller and lets you write each independent job as a separate **task**, then automatically switches between them so fast (many times per second) that they all appear to run simultaneously -- even on a chip with only a single processor core.

## 5.2 What Exactly Is a "Task"?

A FreeRTOS task is just a normal C function, with two required characteristics:

1. It contains an **infinite loop** (`for(;;)` or `while(1)`) -- because a task is meant to keep doing its job forever, not run once and return.
2. Somewhere inside that loop, it must **voluntarily give up the CPU** for a moment (usually via a delay function) -- so that FreeRTOS's scheduler gets a chance to switch to another task.

```c
void vMyTaskFunction(void *pvParameters)
{
    for (;;)
    {
        /* ... do some work here ... */

        vTaskDelay(pdMS_TO_TICKS(1000));  /* sleep 1 second, let others run */
    }
}
```

**Why the delay matters so much:** if you forget it, this one task will run forever without ever pausing, and it will "hog" the entire CPU -- every other task will simply never get a chance to run (this is called **starvation**). The delay is what makes cooperative multitasking actually work.

## 5.3 Creating and Starting Tasks

Writing the function above doesn't make it run automatically -- you must **register** it with FreeRTOS using `xTaskCreate()`, and then start the whole scheduling engine.

```c
xTaskCreate(
    vMyTaskFunction,          /* 1. Pointer to the task function      */
    "MyTaskName",             /* 2. Human-readable name (debug only)  */
    configMINIMAL_STACK_SIZE, /* 3. Stack size (memory for this task) */
    NULL,                     /* 4. Parameters passed into the task   */
    1,                        /* 5. Priority (higher = more urgent)   */
    NULL                      /* 6. Task handle (optional, for later) */
);
```

After creating all the tasks you need, you hand control over to FreeRTOS with:

```c
vTaskStartScheduler();
```

**Critical fact:** any code written *after* this call normally never executes -- `vTaskStartScheduler()` takes over the program permanently, continuously switching between your tasks. It only returns in rare failure situations (like running out of memory during startup).

## 5.4 Queues -- How Tasks Talk to Each Other

Tasks are independent, but our whole pipeline depends on them **passing data to each other in order** (Task 1 produces chunks -> Task 2 manages ordering -> Task 3 encodes them). Since tasks don't share variables safely on their own (this can cause serious bugs called race conditions), FreeRTOS provides **queues** as the safe, built-in mechanism for this.

**Analogy:** think of a queue as a conveyor belt or a mailbox between two workers. Worker A puts a finished item on the belt and moves on to the next item, without waiting for Worker B to pick it up. Worker B, whenever it's ready, takes the next item off the belt. Neither worker needs to directly synchronize with the other -- the belt (queue) handles the handoff safely.

### 5.4.1 Creating a Queue

```c
QueueHandle_t myQueue = xQueueCreate(
    10,                  /* how many items the queue can hold at once */
    sizeof(MyDataType_t) /* the size, in bytes, of each item          */
);
```

### 5.4.2 Sending Data Into a Queue

```c
MyDataType_t item = { /* ... fill in fields ... */ };
xQueueSend(myQueue, &item, portMAX_DELAY);
```

- `&item` -- FreeRTOS copies the data pointed to here into the queue's internal storage (it does NOT just store the pointer -- it makes an actual copy of the bytes)
- `portMAX_DELAY` -- if the queue is currently full, wait as long as necessary for space to free up (in our simulation, with reasonable queue sizes, this is basically never an issue)

### 5.4.3 Receiving Data From a Queue

```c
MyDataType_t received;
xQueueReceive(myQueue, &received, portMAX_DELAY);
/* 'received' now holds a full copy of the data that was sent */
```

This call will **block** (pause) the calling task until something is available in the queue -- which is exactly the behavior we want: Task 2 should simply wait patiently until Task 1 has produced something, rather than constantly checking in a wasteful loop.

## 5.5 Structs -- Defining What a "Chunk" of Data Looks Like

Since each fragment/chunk in our pipeline needs multiple pieces of information together (which event it's from, which piece of the sequence, how big it is, and the actual bytes), we bundle all of that into a single C `struct` -- essentially a custom data type combining several fields into one unit. This struct is exactly what gets sent through our queues.

```c
typedef struct {
    uint16_t occultation_id;      /* which event generated this data      */
    uint8_t  sequence_number;     /* which chunk number this is (1-based) */
    uint8_t  total_chunks;        /* how many chunks exist for this event */
    uint8_t  data_size;           /* how many valid bytes are in 'data'   */
    uint8_t  data[32];            /* the actual chunk bytes               */
} PayloadChunk_t;
```

This "envelope" travels as a single unit through `xQueueSend()` / `xQueueReceive()` between our three tasks.

## 5.6 Delays and Timing

```c
vTaskDelay(pdMS_TO_TICKS(500));   /* pause this task for 500 milliseconds */
```

Always use the `pdMS_TO_TICKS()` macro to convert a millisecond value into FreeRTOS's internal "tick" unit, rather than using raw numbers -- this keeps your code portable even if the underlying tick rate configuration changes.

## 5.7 Task Priority

```c
xTaskCreate(taskFunc, "Name", stackSize, NULL, 1, NULL);  /* priority 1 (lower)  */
xTaskCreate(taskFunc, "Name", stackSize, NULL, 3, NULL);  /* priority 3 (higher) */
```

Higher priority number = FreeRTOS will favor running that task more urgently whenever it's ready to run. For our current simulation-stage pipeline, we can keep all three tasks at the **same, low priority** -- since we're just proving the logic works, not yet worrying about strict real-time guarantees.

## 5.8 Putting It All Together -- Mental Model of Our 3-Task Pipeline

```
+---------------------------+
|  TASK 1: Payload Reader   |
|  - simulate reading data  |
|  - split into chunks       |
|  - fill PayloadChunk_t     |
|  - xQueueSend() -> Queue1  |
+-------------+-------------+
              |
              v
        +-----------+
        |  QUEUE 1  |
        +-----------+
              |
              v
+---------------------------+
|  TASK 2: FIFO Manager     |
|  - xQueueReceive(Queue1)   |
|  - (optionally reorder)    |
|  - xQueueSend() -> Queue2  |
+-------------+-------------+
              |
              v
        +-----------+
        |  QUEUE 2  |
        +-----------+
              |
              v
+---------------------------+
|  TASK 3: NGHam Encoder    |
|  - xQueueReceive(Queue2)   |
|  - wrap chunk in NGHam     |
|  - "transmit" (print/log)  |
+---------------------------+
```

All three tasks run **concurrently**, each waking up and processing whenever data becomes available in its input queue, and FreeRTOS's scheduler handles all of the switching between them automatically.

---

# 6. PART E -- WHY WE CHOSE THIS DEVELOPMENT SETUP

## 6.1 The Target Hardware (Long-Term Goal)

The real TTC firmware runs on an **MSP430F6659** microcontroller (a 16-bit chip from Texas Instruments), on the TTC 2.0 board designed by SpaceLab. Eventually, our pipeline code needs to run on this real chip.

## 6.2 The Immediate Problem

- We do **not** have a physical MSP430F6659 board available right now.
- The official development tool for this chip, **Code Composer Studio (CCS)**, only runs on Windows or Linux -- not natively on macOS.
- Lucas explicitly recommended: *"start with a MCU-based implementation where memory access is simulated"* -- i.e., prove the logic works in software first, before worrying about real hardware.

## 6.3 The Solution: FreeRTOS's POSIX/Linux Simulator Port

FreeRTOS is designed to be portable across many different chips and operating systems. Alongside its "real" ports for actual microcontrollers, the FreeRTOS project also maintains a special **POSIX port** -- this lets the entire FreeRTOS kernel run as an ordinary process on any POSIX-compliant operating system (Linux, and also **macOS**, since macOS's Unix core is POSIX-compliant).

This means: **we can write, compile, and run our exact task/queue logic directly on a Mac's Terminal**, using `gcc`, with FreeRTOS behaving almost identically to how it would on the real chip -- just using the Mac's CPU and threads to simulate the scheduling, instead of real microcontroller hardware timers.

**Why this is the right call:**
- No hardware purchase or wait time needed
- Extremely fast iteration -- edit code, recompile, run, see results in seconds
- All the *logic* (fragmentation, queue management, NGHam encoding) is 100% transferable later -- when we do move to the real MSP430 + Code Composer Studio, the task/queue code itself won't need to change, only the low-level hardware-specific pieces (like actually keying a radio, or reading real memory addresses) would need adapting.
- Teammate Amrit independently validated this exact same approach using WSL (Windows Subsystem for Linux) on his machine, and it worked well for building and testing Task 1.

---

# 7. PART F -- COMPLETE FILE STRUCTURE EXPLAINED

## 7.1 The Full Folder Layout

```
cubesat-pipeline/                              <- our project root
|
+-- FreeRTOS-Kernel/                           <- cloned directly from
|   |                                             github.com/FreeRTOS/FreeRTOS-Kernel
|   |                                             (the official FreeRTOS core source code)
|   |
|   +-- tasks.c                                <- implements task creation, scheduling,
|   |                                             switching between tasks, priorities, delays
|   +-- queue.c                                <- implements xQueueCreate/Send/Receive and
|   |                                             all queue behavior
|   +-- list.c                                 <- internal linked-list data structure used
|   |                                             by tasks.c and queue.c to track tasks/items
|   +-- timers.c                               <- software timer functionality (not directly
|   |                                             used by our simple demo yet, but required
|   |                                             to compile the kernel as a whole)
|   +-- event_groups.c                         <- another synchronization primitive (bit-flag
|   |                                             based signaling between tasks); required
|   |                                             for compilation even if unused so far
|   +-- include/                               <- all the FreeRTOS header files (.h) that
|   |                                             declare the functions/types above
|   |                                             (task.h, queue.h, FreeRTOS.h, etc.)
|   |
|   +-- portable/                              <- hardware/OS-specific "glue" code --
|       |                                          this is the part that changes depending
|       |                                          on what chip/OS FreeRTOS is running on
|       |
|       +-- MemMang/
|       |   +-- heap_3.c                       <- memory allocator implementation we chose;
|       |                                          heap_3 simply wraps the standard C
|       |                                          malloc()/free(), which is the simplest
|       |                                          and safest option for a simulator
|       |                                          (FreeRTOS offers 5 different heap
|       |                                          strategies -- heap_1 through heap_5 --
|       |                                          each with different tradeoffs; heap_3
|       |                                          is ideal for POSIX simulation)
|       |
|       +-- ThirdParty/GCC/Posix/
|           +-- port.c                          <- THE key file that makes FreeRTOS work
|           |                                       on a POSIX system: it uses real OS
|           |                                       threads (pthreads) and signals to
|           |                                       *simulate* what a microcontroller's
|           |                                       hardware timer interrupts would
|           |                                       normally do
|           +-- port.h                          <- header declarations for the above
|           +-- utils/
|               +-- wait_for_event.c            <- small helper used internally by port.c
|                                                   for thread synchronization
|
+-- src/                                        <- OUR OWN application code (everything
|   |                                               here, we wrote ourselves)
|   |
|   +-- FreeRTOSConfig.h                        <- THE configuration file -- this is how
|   |                                               we customize FreeRTOS's behavior for
|   |                                               our specific project: how much memory
|   |                                               to reserve, how many priority levels
|   |                                               to allow, which optional features
|   |                                               (mutexes, timers, stack-overflow
|   |                                               checking, malloc-failure hooks) to
|   |                                               turn on, and the simulated "tick rate"
|   |                                               (how many scheduler ticks happen per
|   |                                               second -- we set this to 1000 Hz,
|   |                                               i.e. one tick per millisecond)
|   |
|   +-- main.c                                  <- our application entry point: this is
|   |                                               where we define our tasks (currently
|   |                                               2 simple test tasks; will become our
|   |                                               real 3-task pipeline), create them
|   |                                               with xTaskCreate(), and start the
|   |                                               scheduler. Also contains the 3
|   |                                               "hook" functions FreeRTOS calls if
|   |                                               something goes wrong (assertion
|   |                                               failure, malloc failure, stack
|   |                                               overflow) -- these are required
|   |                                               because we enabled the corresponding
|   |                                               safety checks in FreeRTOSConfig.h
|   |
|   +-- payload_chunk.h                         <- OUR custom struct definition
|                                                    (PayloadChunk_t) -- the shared
|                                                    "envelope" format that will travel
|                                                    between Task 1 -> Task 2 -> Task 3
|                                                    through the queues. Fields:
|                                                    occultation_id, sequence_number,
|                                                    total_chunks, data_size, and the
|                                                    raw data bytes themselves.
|
+-- build/                                      <- output folder; this is where the
|                                                    Makefile places the final compiled
|                                                    program (cubesat_pipeline) after
|                                                    building. Nothing in here is
|                                                    hand-written -- it's all generated
|                                                    automatically by `make`.
|
+-- Makefile                                    <- the build recipe: tells `gcc` exactly
                                                     which .c files to compile, which
                                                     folders to search for header files
                                                     (-I flags), which libraries to link
                                                     against (-lpthread), and produces
                                                     the final executable at
                                                     build/cubesat_pipeline
```

## 7.2 Why Each Piece Matters (Conceptual Summary)

| Folder/File | Role in the System |
|-------------|---------------------|
| `FreeRTOS-Kernel/*.c` (root level) | The **brain** of FreeRTOS -- chip-independent scheduling logic, used unchanged regardless of what hardware you run on |
| `FreeRTOS-Kernel/portable/` | The **adapter** -- translates FreeRTOS's abstract scheduling concepts into real actions on a specific platform (in our case, POSIX threads on macOS; later, real MSP430 hardware timers) |
| `src/FreeRTOSConfig.h` | The **settings panel** -- every project using FreeRTOS must supply one of these; it's how you tell the kernel "here's how much memory I have, here's what features I want enabled" |
| `src/main.c` | **Our actual program logic** -- where we define what our tasks actually do |
| `src/payload_chunk.h` | **Our shared data contract** -- ensures Task 1, 2, and 3 all agree on exactly what a "chunk" looks like in memory |
| `Makefile` | **The construction instructions** -- turns all the scattered `.c` files above into one runnable program |
| `build/` | **The finished product** -- the actual compiled, runnable binary |

---

# 8. PART G -- STEP-BY-STEP SETUP LOG (What We Actually Did)

## 8.1 Environment Verification

Checked that the Mac already had the necessary command-line tools:

```bash
gcc --version
make --version
```

Both were available via Apple's Xcode Command Line Tools (no extra install needed in our case).

## 8.2 Project Folder Creation

```bash
cd ~/Desktop
mkdir cubesat-pipeline
cd cubesat-pipeline
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
mkdir -p src build
```

## 8.3 Writing `src/FreeRTOSConfig.h`

Created with settings including: preemptive scheduling enabled, 1000 Hz tick rate, 5 priority levels, 1024-word minimal stack size, 128 KB total heap, mutexes/semaphores/queue-sets enabled, stack-overflow checking enabled, malloc-failure hook enabled, and various optional `INCLUDE_...` API features turned on.

## 8.4 Writing `src/main.c` (first version -- simple 2-task test)

Two tasks: `vTask1` prints a message every 1 second, `vTask2` prints a message every 2 seconds -- deliberately simple, just to prove the whole toolchain (compiler, kernel, POSIX port, Makefile) works correctly before building anything complex.

## 8.5 Writing the `Makefile`

Defined compiler flags, include paths (pointing at `src/`, `FreeRTOS-Kernel/include/`, the POSIX port folder, and its utils folder), the list of kernel + port + application source files to compile, and linked against `-lpthread` (needed because the POSIX port uses real OS threads under the hood to simulate tasks).

## 8.6 First Build Attempt -- and the 3 Mac-Specific Issues We Hit & Fixed

### Issue 1: `pthread_setname_np` undeclared

**Error:**
```
error: call to undeclared function 'pthread_setname_np'
```

**Cause:** The FreeRTOS POSIX port's `port.c` file calls this function to (optionally) label OS threads with a readable name for debugging purposes. macOS's version of this function has a different signature (takes 1 argument) than Linux's version (takes 2 arguments), and under the specific `_POSIX_C_SOURCE` compiler flag we were using, macOS's header didn't expose it as expected.

**Fix:** Since this only affects a cosmetic debug feature (naming threads for debuggers/profilers) and has zero effect on actual task/queue functionality, we simply commented out that one line:

```bash
sed -i.bak 's/pthread_setname_np( pxThreadName );/\/\/pthread_setname_np( pxThreadName );/' \
  FreeRTOS-Kernel/portable/ThirdParty/GCC/Posix/port.c
```

### Issue 2: `ld: library 'rt' not found`

**Error occurred at the final linking stage** (after all files compiled successfully):
```
ld: library 'rt' not found
```

**Cause:** Our Makefile's `LDFLAGS` included `-lrt`, which links against `librt` -- a library that provides POSIX real-time extensions (like certain timer functions) **on Linux**. macOS does not ship a separate `librt` because that same functionality is already built into its core system libraries.

**Fix:** Edited the Makefile to remove `-lrt`, keeping only `-lpthread`:
```makefile
LDFLAGS = -lpthread
```

### Issue 3: Undefined symbols -- `_vApplicationMallocFailedHook` / `_vApplicationStackOverflowHook`

**Error:**
```
Undefined symbols for architecture arm64:
  "_vApplicationMallocFailedHook", referenced from: _pvPortMalloc in heap_3.o
  "_vApplicationStackOverflowHook", referenced from: _vTaskSwitchContext in tasks.o
```

**Cause:** In our `FreeRTOSConfig.h`, we had enabled two safety-check features:
```c
#define configUSE_MALLOC_FAILED_HOOK    1
#define configCHECK_FOR_STACK_OVERFLOW  2
```
Enabling these tells the FreeRTOS kernel: "if memory allocation ever fails, or if a task's stack ever overflows, call a function named `vApplicationMallocFailedHook()` / `vApplicationStackOverflowHook()` so the application can react (e.g., log an error)." However, FreeRTOS does **not** provide default implementations of these -- the application (us) must supply them, or the linker can't find them.

**Fix:** Added both hook functions directly into `src/main.c`:

```c
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

## 8.7 Successful Build & First Run

```bash
make clean
make
./build/cubesat_pipeline
```

**Output (excerpt):**
```
=== FreeRTOS POSIX Test ===
[Task1] Hello from Task 1!
[Task2] Hello from Task 2!
[Task1] Hello from Task 1!
[Task1] Hello from Task 1!
[Task2] Hello from Task 2!
...
```

Task1 printed roughly every 1 second, Task2 roughly every 2 seconds -- **both running concurrently**, confirming the entire toolchain (kernel + POSIX port + Makefile + config + hooks) works correctly, entirely simulated on macOS, with zero real hardware involved. Stopped the program manually with `Ctrl+C` (since the tasks loop forever by design).

---

# 9. PART H -- WHERE WE ARE RIGHT NOW

- [x] Learned NGHam protocol theory in full depth (packet structure, size tags, Reed-Solomon, CRC, padding, scrambling)
- [x] Learned SPP (Serial Port Protocol) theory in full depth (packet structure, 4 payload types, little-endian CRC)
- [x] Learned NGHam Extension Packets (background knowledge, not required for current task)
- [x] Learned FreeRTOS theory from zero (tasks, queues, structs, delays, priority, scheduler)
- [x] Understood why we're using the FreeRTOS POSIX simulator instead of real MSP430 hardware
- [x] Set up the full project folder structure on macOS
- [x] Successfully compiled and linked FreeRTOS on macOS, fixing 3 Mac-specific build issues along the way
- [x] Successfully ran a working 2-task FreeRTOS demo, proving the whole toolchain works
- [x] Defined `payload_chunk.h` -- the shared struct that Task 1/2/3 will use to pass data
- [ ] **Not yet started:** actual implementation of Task 1 (Payload Reader/Fragmenter), Task 2 (FIFO Manager), Task 3 (NGHam Encoder)

**In short: all groundwork and environment setup is complete and verified working. We are now ready to begin writing the real pipeline logic.**

---

# 10. PART I -- NEXT STEPS

1. Implement **Task 1**: simulate a fake payload memory buffer, fragment it into fixed-size chunks, fill in `PayloadChunk_t` metadata for each chunk, and send each chunk into Queue 1 -- test it alone first with a temporary "dummy consumer" task (the same approach teammate Amrit already validated on WSL), to confirm correctness in isolation.
2. Implement **Task 2**: receive chunks from Queue 1, preserve strict ordering, forward to Queue 2.
3. Implement **Task 3**: receive chunks from Queue 2, wrap each one into a full NGHam packet (applying everything learned in Part A), and simulate "transmission" via printed/logged output.
4. Test the complete end-to-end pipeline with multiple simulated occultation events of varying sizes.
5. Revisit open questions with mentor Lucas: is the chunk header field set correct/sufficient? Is the chosen chunk size appropriate for this mission, or should it be tied to actual NGHam payload limits? Confirm the POSIX-simulator-first approach is aligned with his expectations.
6. (Later, after logic is proven) Port the working code to the real MSP430F6659 target using Code Composer Studio, replacing the simulated memory-read step with real payload memory access, and the "transmit" print statement with an actual radio interface call.

---
