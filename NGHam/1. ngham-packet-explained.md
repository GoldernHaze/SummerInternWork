# Understanding NGHam Packets: From Sensor Data to Radio Transmission

## Introduction

When learning NGHam for CubeSat communications, it is easy to get lost in terms such as *payload*, *packet*, *CRC*, *Reed-Solomon*, and *scrambling*. This document explains the complete transmission process using a simple telemetry example.

The goal is to answer a common question:

> How does sensor data become an NGHam packet that is transmitted over the radio?

---

# 1. Telemetry Data

Assume a CubeSat measures:

```text
Temperature = 25°C
Battery Voltage = 7.8V
```

Inside the microcontroller:

```python
temperature = 25
battery_voltage = 7.8
```

At this stage, the data only exists as variables in memory.

---

# 2. Creating the Payload

The payload is the useful information that we want to transmit.

A text-based payload could be:

```text
TEMP=25,BAT=7.8
```

However, satellites typically use binary payloads because they are smaller and more efficient.

Example:

```python
payload = bytes([25, 78])
```

Where:

* 25 = temperature
* 78 = battery voltage × 10

Binary payload size:

```text
2 bytes
```

instead of approximately 15 bytes for the text representation.

---

# 3. Payload vs Packet

These terms are often confused.

## Payload

The actual information:

```text
Temperature = 25°C
Battery = 7.8V
```

or

```text
19 4E
```

in hexadecimal.

## Packet

The complete transmission unit:

```text
+---------+
| Header  |
+---------+
| Payload |
+---------+
| CRC     |
+---------+
| FEC     |
+---------+
```

The payload is only one part of the packet.

---

# 4. NGHam Packet Structure

An NGHam transmission consists of:

```text
+----------+
| Preamble |
+----------+
| Sync     |
+----------+
| Size Tag |
+----------+
| RS Block |
+----------+
```

Where:

### Preamble

Used for receiver synchronization.

Typical value:

```text
AA AA AA AA
```

---

### Sync Word

Marks the start of a packet.

Fixed value:

```text
5D E6 2A 7E
```

When the receiver detects this sequence, it knows a new NGHam packet is beginning.

---

### Size Tag

Identifies which NGHam packet size is being used.

Example for Size Group 1:

```text
59 73 CD
```

This tells the receiver:

* packet size
* Reed-Solomon configuration
* maximum payload size

---

# 5. Building the Reed-Solomon Data Block

The RS block contains:

```text
+--------+
| Header |
+--------+
| Payload|
+--------+
| CRC16  |
+--------+
| Padding|
+--------+
```

## Header

The first byte of the RS block.

Bit allocation:

```text
7-6 : Reserved
5   : Extension Flag
4-0 : Padding Length
```

Example:

```text
00000000
```

No extension and no padding.

---

## Payload

Telemetry data:

```text
19 4E
```

Hexadecimal representation of:

```text
25
78
```

---

## CRC16

Computed over:

```text
Header + Payload
```

Used for error detection.

NGHam uses CRC16-CCITT:

* Polynomial: 0x1021
* Initial value: 0xFFFF
* Final XOR: 0xFFFF

---

## Padding

Added when the payload is smaller than the maximum allowed size for the selected packet group.

Padding bytes are:

```text
00
```

The number of padding bytes is stored in the header.

---

# 6. Reed-Solomon Forward Error Correction

After the data section is complete, Reed-Solomon parity bytes are generated.

Example:

```text
Data Bytes
    ↓
RS Encoder
    ↓
Parity Bytes
```

The parity allows the receiver to correct transmission errors without requiring retransmission.

This is one of the major advantages of NGHam over AX.25.

---

# 7. Scrambling

Before transmission, the RS block is scrambled.

Operation:

```text
Packet Byte XOR Scramble Byte
```

Purpose:

* avoid long runs of zeros
* avoid long runs of ones
* improve radio synchronization

The receiver performs the same XOR operation to recover the original data.

---

# 8. Final Packet Assembly

After scrambling, the complete NGHam frame becomes:

```text
AA AA AA AA
5D E6 2A 7E
59 73 CD
[Scrambled RS Block]
```

This byte stream is passed to the radio modem.

---

# 9. What the Radio Actually Transmits

Radios do not transmit characters.

They transmit bits.

Example:

```text
AA
```

Hexadecimal:

```text
AA
```

Binary:

```text
10101010
```

The radio converts these bits into RF signals and transmits them through the antenna.

---

# 10. Reception Process

The ground station performs the reverse operation:

```text
RF Signal
    ↓
Demodulation
    ↓
Bit Stream
    ↓
Sync Detection
    ↓
Size Tag Detection
    ↓
Descrambling
    ↓
CRC Check
    ↓
Reed-Solomon Decode
    ↓
Payload Extraction
```

Recovered telemetry:

```text
Temperature = 25°C
Battery = 7.8V
```

---

# Summary

The complete NGHam transmission chain is:

```text
Sensor Data
    ↓
Payload
    ↓
Header
    ↓
CRC16
    ↓
Padding
    ↓
Reed-Solomon Encoding
    ↓
Scrambling
    ↓
Size Tag
    ↓
Sync Word
    ↓
Preamble
    ↓
Radio Transmission
```

Understanding this flow makes it much easier to implement NGHam packet generation in software and to debug real CubeSat telemetry systems.
