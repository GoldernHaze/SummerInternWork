# Understanding the NGHam Serial Port Protocol (SPP)

## Introduction

The NGHam Serial Port Protocol (SPP) is often confused with the NGHam RF protocol. While both belong to the NGHam ecosystem, they serve completely different purposes.

This document explains:

* The difference between NGHam RF and NGHam SPP
* Why SPP exists
* SPP packet structure
* Packet types
* Practical examples of packet transmission

---

# 1. NGHam RF vs NGHam SPP

A CubeSat communication system typically contains:

```text
+------------+       RF Link       +------------+
| Satellite  | <-----------------> | Ground     |
| Radio      |                     | Radio      |
+------------+                     +------------+
                                        |
                                        |
                                     UART/USB
                                        |
                                        ▼
                                  +------------+
                                  | Computer   |
                                  +------------+
```

There are two independent communication links:

## Link 1: Radio ↔ Radio

Communication over RF.

Protocol used:

```text
NGHam RF Protocol
```

Responsibilities:

* Packet synchronization
* Reed-Solomon Forward Error Correction
* CRC verification
* Packet scrambling
* RF packet framing

This is the protocol responsible for transmitting telemetry through space.

---

## Link 2: Computer ↔ Radio

Communication over UART or USB.

Protocol used:

```text
NGHam Serial Port Protocol (SPP)
```

Responsibilities:

* Deliver commands to the radio
* Transfer received packets to the host computer
* Exchange status information
* Control radio operation

SPP never goes over RF.

SPP exists only between the host computer and the radio hardware.

---

# 2. Why SPP Exists

Imagine a Python application connected to a radio.

The application wants to transmit:

```text
HELLO SATELLITE
```

If raw bytes are sent directly over UART:

```text
48 45 4C 4C 4F ...
```

the radio cannot determine:

* Is this a command?
* Is this RF data?
* Is this a status request?
* How long is the message?

SPP solves this problem by wrapping everything into a structured packet.

---

# 3. SPP Packet Format

Every SPP packet has the following structure:

```text
+-----------+
| Start Tag |
+-----------+
| CRC16     |
+-----------+
| Type      |
+-----------+
| Length    |
+-----------+
| Payload   |
+-----------+
```

Field details are described below.

---

# 4. Start Tag

Size:

```text
1 byte
```

Value:

```text
'$'
```

Hexadecimal:

```text
0x24
```

Purpose:

The start tag allows the receiver to detect the beginning of a new SPP packet.

Example:

```text
24
```

---

# 5. CRC16 Field

Size:

```text
2 bytes
```

Purpose:

Detect transmission errors on the serial link.

Configuration:

```text
CRC16-CCITT
Initial Value = 0xFFFF
Polynomial    = 0x1021
Final XOR     = 0xFFFF
```

Important:

SPP stores CRC in little-endian format.

The CRC is calculated over:

```text
Type
Length
Payload
```

The Start Tag and CRC bytes themselves are excluded.

---

# 6. Payload Type Field

Size:

```text
1 byte
```

The payload type determines how the payload should be interpreted.

## Type 0x00

RF Receive Packet

Direction:

```text
Radio → Host
```

Meaning:

```text
Data received from RF
```

---

## Type 0x01

RF Transmit Packet

Direction:

```text
Host → Radio
```

Meaning:

```text
Transmit this data over RF
```

---

## Type 0x02

Local Packet

Direction:

```text
Radio → Host
```

Meaning:

```text
Radio-generated information
```

Examples:

* Internal temperature
* Voltage
* Status reports
* Diagnostic information

---

## Type 0x03

Command Packet

Direction:

```text
Host → Radio
```

Meaning:

```text
Execute a command
```

Examples:

```text
FREQ 145800000
POWER HIGH
STATUS
```

---

# 7. Payload Length Field

Size:

```text
1 byte
```

Purpose:

Defines the number of bytes in the payload field.

Example:

Payload:

```text
HELLO
```

Length:

```text
5
```

Stored as:

```text
0x05
```

---

# 8. Payload Field

Size:

```text
Variable
```

The structure depends on the packet type.

---

# 9. RF Transmit Packet (Type 0x01)

Used when the host wants the radio to transmit data.

Payload structure:

```text
+-------+
| Flags |
+-------+
| Data  |
+-------+
```

## Flags

Size:

```text
1 byte
```

Bit definitions:

```text
Bit 0 = NGHam Extension Enabled
```

Normally:

```text
0x00
```

---

## Data

Actual RF payload.

Example:

```text
HELLO
```

ASCII bytes:

```text
48 45 4C 4C 4F
```

---

## Example RF TX Payload

```text
Flags = 00
Data  = 48 45 4C 4C 4F
```

Result:

```text
00 48 45 4C 4C 4F
```

Length:

```text
6 bytes
```

---

# 10. RF Receive Packet (Type 0x00)

Used by the radio to deliver received RF data to the host.

Payload structure:

```text
+-----------+
| Timestamp |
+-----------+
| Noise     |
+-----------+
| RSSI      |
+-----------+
| RS Errors |
+-----------+
| Flags     |
+-----------+
| Data      |
+-----------+
```

---

## Timestamp

Size:

```text
4 bytes
```

Represents:

```text
Microseconds within current hour
```

Special value:

```text
0xFFFFFFFF
```

means unavailable.

---

## Noise Floor

Size:

```text
1 byte
```

Convert to dBm:

```text
Value - 200
```

Example:

```text
0x50 = 80

80 - 200 = -120 dBm
```

---

## RSSI

Size:

```text
1 byte
```

Represents received signal strength.

Example:

```text
105 - 200 = -95 dBm
```

Higher values indicate stronger signals.

---

## Symbol Errors

Size:

```text
1 byte
```

Represents how many Reed-Solomon symbols were corrected.

Example:

```text
3
```

means:

```text
Three damaged symbols were corrected.
```

---

## Flags

Size:

```text
1 byte
```

Bit 0:

```text
NGHam Extension Enabled
```

---

## Data

Actual RF payload received from the satellite.

Example:

```text
TEMP=25
```

---

# 11. Local Packet (Type 0x02)

Generated internally by the radio.

Payload structure:

```text
+-------+
| Flags |
+-------+
| Data  |
+-------+
```

Examples:

* Radio status
* Diagnostic information
* Internal measurements

These packets are not received over RF.

---

# 12. Command Packet (Type 0x03)

Used to send commands to the radio.

Payload:

```text
Command String
```

Examples:

```text
FREQ 145800000
```

```text
POWER HIGH
```

```text
STATUS
```

Commands are not null-terminated.

The entire payload is interpreted as a command.

---

# 13. Example RF Transmission

Suppose the host wants to transmit:

```text
HELLO
```

RF TX payload:

```text
00 48 45 4C 4C 4F
```

Length:

```text
06
```

Type:

```text
01
```

The CRC is computed over:

```text
01 06 00 48 45 4C 4C 4F
```

Assuming CRC:

```text
B2 A1
```

Final SPP packet:

```text
24
B2 A1
01
06
00 48 45 4C 4C 4F
```

The radio receives this packet and converts the payload into an NGHam RF transmission.

---

# 14. Complete Communication Flow

The complete communication path is:

```text
Application
    │
    ▼
SPP Packet
    │
UART / USB
    │
    ▼
Radio
    │
NGHam RF Protocol
    │
    ▼
RF Transmission
    │
    ▼
Another Radio
    │
SPP Packet
    ▼
Application
```

---

# Summary

NGHam RF and NGHam SPP operate at different layers.

| Protocol  | Purpose                         |
| --------- | ------------------------------- |
| NGHam RF  | Radio-to-radio communication    |
| NGHam SPP | Computer-to-radio communication |

NGHam RF handles transmission through space.

NGHam SPP provides a structured method for exchanging commands, telemetry, and status information between a host computer and a radio over a serial interface.
