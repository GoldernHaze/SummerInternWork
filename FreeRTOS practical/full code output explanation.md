

---

# THE FULL OUTPUT, EXPLAINED LINE BY LINE

```
=== CubeSat Packet Pipeline - Task 1 + 2 + 3 Test ===
```
This is the very first `printf` in `main()`, before any queues or tasks even exist yet. It runs on the "main thread" before `vTaskStartScheduler()` hands control over to FreeRTOS — proof the program started successfully and got past all our earlier build/link fixes.

```
[Task1-PayloadReader] occultation 1000: 100 bytes -> 4 chunk(s)
```
FreeRTOS's scheduler picked Task 1 to run first (arbitrary — all three were created at equal priority, so which one runs "first" is really just whichever the scheduler happens to pick initially). This line comes from the `printf` right after the ceiling-division calculation:
```c
uint8_t total_chunks = (data_len + CHUNK_DATA_SIZE - 1) / CHUNK_DATA_SIZE;
printf("[Task1-PayloadReader] occultation %d: %d bytes -> %d chunk(s)\n", ...);
```
`100 bytes -> 4 chunk(s)`: `(100+31)/32 = 131/32 = 4` (integer truncation). This confirms the math is working before Task 1 even starts sending anything.

```
[Task2-FifoManager] started.
```
Notice this appears **after** Task 1's first log line, not before. This tells us something real about scheduling order: even though `xTaskCreate` registered Task 2 before the scheduler started, FreeRTOS chose to actually run Task 1 first for at least one full pass through its outer loop (calculating chunk count, generating fake data) before switching over to let Task 2 print its own startup message. This is exactly the kind of non-deterministic-looking interleaving that's normal and expected in preemptive multitasking — the *exact* order tasks first get a turn isn't something we control precisely, only the equal priority we gave them.

```
[Task2-FifoManager] forwarding  occ=1000  seq=1/4  bytes=32
```
This is proof Task 1 already sent its **first chunk** into Queue 1, and Task 2 immediately picked it up:
```c
if (xQueueReceive(xQueue1_RawChunks, &chunk, pdMS_TO_TICKS(3000)) == pdTRUE)
{
    total_forwarded++;
    printf("[Task2-FifoManager] forwarding  occ=%d  seq=%d/%d  bytes=%d\n", ...);
```
`seq=1/4` confirms this is chunk 1 of the 4 total we calculated a moment earlier. `bytes=32` confirms this first chunk took a full 32-byte slice (since 100 bytes remaining is more than one chunk's worth).

```
[Task3-NGHamEncoder] started.
```
Same pattern as Task 2's startup — Task 3 only got its first turn to run *after* something was already flowing through the pipeline ahead of it. This is a nice illustration of how tasks don't necessarily all "start" at the same instant just because they were all created before the scheduler launched.

```
[Task3-NGHamEncoder] occ=1000 seq=1/4 -> NGHam packet (90 bytes): AA AA AA AA 5D E6 2A 7E 4D DA 57 17 03 E8 01 04 20 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F 56 EC 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B
```
This is the payoff line — Task 3 received the same chunk that Task 2 just forwarded, and ran the entire `ngham_build_packet()` pipeline on it. Breaking this exact line down byte-by-byte (this is the chunk we already verified independently in Section 10 of `task3.md`):

- `AA AA AA AA` — the preamble, hardcoded, identical on every single packet.
- `5D E6 2A 7E` — the sync word, also hardcoded, identical on every packet.
- `4D DA 57` — the size tag in hex; this is `(77,218,87)` in decimal, meaning our size-selection loop determined this 37-byte serialized payload needed **Size 2** (`RS(79,63)`).
- `17` — the header byte, `0x17` = 23 decimal, meaning 23 padding bytes were added.
- `03 E8 01 04 20` — our serialized metadata: `03 E8` = 1000 (occultation ID, big-endian), `01` = sequence number 1, `04` = total_chunks 4, `20` = data_size 32 (0x20 = 32 decimal).
- `10 11 12 ... 2F` — the 32 actual data bytes, exactly matching what Task 1 generated for this chunk.
- `56 EC` — the CRC16 checksum, calculated over everything from the header through the data (38 bytes).
- 23 bytes of `00` — the padding, exactly matching the `23` we calculated in the header.
- `FC 48 46 EB C6 99 CC 1D 63 10 FA 05 65 74 E2 8B` — the 16 Reed-Solomon parity bytes.
- `(90 bytes)` in the printed label — confirms `4+4+3+63+16 = 90`, exactly matching our expected total.

```
[Task2-FifoManager] forwarding  occ=1000  seq=2/4  bytes=32
```
Task 1 has now looped back around (after its `vTaskDelay(50)`), produced chunk 2, sent it to Queue 1 — and Task 2 immediately grabbed it. Notice the pattern emerging: **Task2's "forwarding" line always appears right before the matching Task3 "packet" line** — this is the visual signature of our pipeline actually working as a real assembly line, not just three unrelated tasks printing randomly.

```
[Task3-NGHamEncoder] occ=1000 seq=2/4 -> NGHam packet (90 bytes): ... 30 31 32 ... 4F ... AD 36 ...
```
Same structure as before, but notice the data bytes now start at `30` instead of `10` — this is chunk 2 of the same event, correctly picking up right where chunk 1 left off (chunk 1 covered bytes `10`-`2F`, chunk 2 covers `30`-`4F`). The CRC (`AD 36`) and parity bytes are completely different from chunk 1's — exactly what we'd expect, since even a single different input byte should produce a completely different CRC/parity (this is a desirable property of both algorithms, called the "avalanche effect").

```
[Task2-FifoManager] forwarding  occ=1000  seq=3/4  bytes=32
[Task3-NGHamEncoder] occ=1000 seq=3/4 -> NGHam packet (90 bytes): ... 50 51 52 ... 6F ...
```
Chunk 3, continuing the data range `50`-`6F`. Same pattern repeats.

```
[Task2-FifoManager] forwarding  occ=1000  seq=4/4  bytes=4
[Task3-NGHamEncoder] occ=1000 seq=4/4 -> NGHam packet (58 bytes): AA AA AA AA 5D E6 2A 7E 3B 49 CD 13 03 E8 04 04 04 70 71 72 73 BB DB 00 00 ... 00 (19 zeros) ...
```
This is the interesting one — the **remainder** chunk. `bytes=4` confirms only 4 bytes were left over from the original 100-byte event (`100 - 32 - 32 - 32 = 4`). Because the *serialized* payload here is only `5 + 4 = 9` bytes (much smaller than the other chunks' 37 bytes), the size-selection loop picks a **different, smaller category**: the size tag is now `3B 49 CD` (decimal `59,73,205` = **Size 1**, not Size 2). This is visible proof that our size-selection logic dynamically adapts per-chunk rather than always using one fixed category. The header is `13` (hex) = 19 decimal padding bytes, matching `k(31) - 3 - 9 = 19`. Total packet length drops to `(58 bytes)` instead of 90, since Size 1 uses a smaller `RS(47,31)` scheme.

```
[Task1-PayloadReader] occultation 1001: 45 bytes -> 2 chunk(s)
```
Task 1 has finished all 4 chunks of occultation 1000, and moved to the outer loop's next iteration — event index 1 (occultation 1001). `(45+31)/32 = 76/32 = 2` chunks, as expected.

```
[Task2-FifoManager] forwarding  occ=1001  seq=1/2  bytes=32
[Task3-NGHamEncoder] occ=1001 seq=1/2 -> NGHam packet (90 bytes): ... 03 E9 01 02 20 40 41 ... 5F ...
```
`03 E9` = 1001 in hex, confirming the occultation ID correctly updated for this new event. Data starts at `40` — matching our earlier fake-data formula `0x10 + (1 * 0x30) + i`, where `e=1` for this second event.

```
[Task2-FifoManager] forwarding  occ=1001  seq=2/2  bytes=13
[Task3-NGHamEncoder] occ=1001 seq=2/2 -> NGHam packet (58 bytes): ... 03 E9 02 02 0D 60 61 ... 6C ...
```
The remainder chunk for this event: `45 - 32 = 13` bytes left, matching `bytes=13`. Serialized payload size is `5+13=18` bytes, small enough for Size 1 again → 58-byte packet, same as before. The size-tag byte here (`0A` in header, meaning padding=10) differs from the previous Size-1 example (`13`/19 padding) purely because this chunk's payload length (18) differs from the earlier one (9) — less padding needed to fill the same 31-byte block.

```
[Task1-PayloadReader] occultation 1002: 70 bytes -> 3 chunk(s)
```
Third and final fake event. `(70+31)/32 = 101/32 = 3` chunks.

```
[Task2-FifoManager] forwarding  occ=1002  seq=1/3  bytes=32
[Task3-NGHamEncoder] occ=1002 seq=1/3 -> NGHam packet (90 bytes): ... 03 EA 01 03 20 70 71 ... 8F ...
```
`03 EA` = 1002. Data starts at `70`, matching `0x10 + (2*0x30) + i` for `e=2`.

```
[Task2-FifoManager] forwarding  occ=1002  seq=2/3  bytes=32
[Task3-NGHamEncoder] occ=1002 seq=2/3 -> NGHam packet (90 bytes): ... 90 91 ... AF ...
```
Continuing the data range for this event's second chunk.

```
[Task2-FifoManager] forwarding  occ=1002  seq=3/3  bytes=6
[Task3-NGHamEncoder] occ=1002 seq=3/3 -> NGHam packet (58 bytes): ... 03 EA 03 03 06 B0 B1 B2 B3 B4 B5 A9 4D ...
```
Final remainder chunk: `70 - 32 - 32 = 6` bytes, matching `bytes=6`. Serialized length `5+6=11` bytes, again small enough for Size 1 (58-byte packet).

```
[Task1-PayloadReader] all occultations fragmented. Task finished.
```
Task 1's outer `for` loop over all 3 fake events has completed. This is the exact `printf` right before Task 1 enters its permanent idle loop.

```
[Task2-FifoManager] no more chunks arriving. Total forwarded: 9
```
Task 2's `xQueueReceive` call waited its full `3000ms` timeout with nothing new arriving in Queue 1 (since Task 1 is now idle forever and will never send again), so it fell into the `else` branch and printed this final tally. `9` matches exactly: 4 (event 1000) + 2 (event 1001) + 3 (event 1002).

```
[Task3-NGHamEncoder] no more chunks arriving. Total encoded: 9
```
Same timeout logic, one stage further down the pipeline. Task 3's queue (Queue 2) also went quiet once Task 2 stopped forwarding, so it declares itself done too — also correctly counting all 9 chunks.

```
^C
```
This isn't program output at all — it's the terminal showing that **you** pressed `Ctrl+C` to manually stop the program, since both Task 1, Task 2, and Task 3 are now sitting in infinite idle loops (`for(;;) vTaskDelay(...)`) and would otherwise run forever doing nothing, waiting for a program that will never send them more work.

---
