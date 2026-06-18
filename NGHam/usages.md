# PyNGHam Usage Guide

## Introduction

After understanding the NGHam packet structure, Serial Port Protocol (SPP), and Extension Packets, the next step is learning how to use the PyNGHam library to create, encode, decode, and inspect packets.

This document focuses on practical usage and explains what happens internally when the library functions are called.

---

# 1. Installation

Install PyNGHam from PyPI:

```bash
pip install pyngham
```

Verify installation:

```python
import pyngham

print("PyNGHam installed successfully")
```

---

# 2. Basic NGHam Packet Encoding

Import the main class:

```python
from pyngham import PyNGHam
```

Create an encoder/decoder object:

```python
x = PyNGHam()
```

Create a payload:

```python
payload = [0, 1, 2, 3, 4]
```

Encode it:

```python
packet = x.encode(payload)
```

Print the generated packet:

```python
print(packet)
```

---

# What Happens Internally?

The payload:

```text
[0, 1, 2, 3, 4]
```

is not transmitted directly.

PyNGHam automatically constructs:

```text
NGHam Packet
│
├── Preamble
├── Sync Word
├── Size Tag
├── Header
├── Payload
├── CRC16
├── Reed-Solomon Parity
└── Scrambling
```

The resulting packet is significantly larger than the original payload.

---

# 3. Decoding a Packet

Example:

```python
from pyngham import PyNGHam

x = PyNGHam()

packet = x.encode([0, 1, 2, 3, 4])

result = x.decode(packet)

print(result)
```

Output:

```python
([0, 1, 2, 3, 4], 0, [])
```

---

# Understanding the Output

The decode function returns:

```python
(decoded_data, error_count, error_positions)
```

Example:

```python
([0,1,2,3,4], 0, [])
```

Meaning:

```text
Decoded Data  = [0,1,2,3,4]
Errors Found  = 0
Error Locations = []
```

No errors were detected.

---

# 4. Encoding Strings

PyNGHam can encode strings directly.

Example:

```python
from pyngham import PyNGHam

x = PyNGHam()

packet = x.encode("HELLO SATELLITE")

print(packet)
```

The string is automatically converted into bytes before packet construction.

---

# 5. Encoding Byte Arrays

Example:

```python
from pyngham import PyNGHam

x = PyNGHam()

packet = x.encode(
    bytes([10,20,30,40,50])
)

print(packet)
```

Useful when working with sensor data and binary telemetry.

---

# 6. Error Correction Demonstration

One of the most important NGHam features is Reed-Solomon Forward Error Correction.

Create a packet:

```python
from pyngham import PyNGHam

x = PyNGHam()

packet = x.encode([0,1,2,3,4])
```

Intentionally corrupt a byte:

```python
packet[30] = 5
```

Decode:

```python
print(x.decode(packet))
```

Output:

```python
([0,1,2,3,4], 1, [227])
```

---

# What Happened?

Although a byte was corrupted:

```text
packet[30]
```

the original message was recovered successfully.

Output interpretation:

```text
Recovered Data = [0,1,2,3,4]
Corrected Errors = 1
Error Position = 227
```

The Reed-Solomon algorithm corrected the damage.

This is one of the main advantages of NGHam compared to traditional packet radio protocols.

---

# 7. Stream Decoding

Normally:

```python
x.decode(packet)
```

processes an entire packet at once.

In real radio systems, bytes arrive one at a time.

For this reason PyNGHam provides:

```python
x.decode_byte(byte)
```

Example:

```python
for b in packet:
    result = x.decode_byte(b)

    if result[0]:
        print(result)
```

---

# Why Is This Useful?

Real radios receive:

```text
Byte 1
Byte 2
Byte 3
...
```

rather than an entire packet instantly.

The stream decoder simulates actual radio reception.

---

# 8. Serial Port Protocol (SPP)

SPP packets are used between:

```text
Computer ↔ Radio
```

Import:

```python
from pyngham import PyNGHamSPP
```

Initialize:

```python
x = PyNGHamSPP()
```

---

# 9. Creating an RF TX Packet

Transmit packet:

```python
tx_packet = x.encode_tx_pkt(
    0,
    [0,1,2,3,4]
)
```

Print:

```python
print(tx_packet)
```

Decode:

```python
print(x.decode(tx_packet))
```

Output:

```python
{
 'type':1,
 'flags':0,
 'payload':[0,1,2,3,4]
}
```

Meaning:

```text
Type 1 = RF Transmit Packet
```

The radio should transmit the payload over RF.

---

# 10. Creating an RF RX Packet

Example:

```python
rx_packet = x.encode_rx_pkt(
    -50,
    -10,
    4,
    0,
    [0,1,2,3,4]
)
```

Parameters:

```text
Noise Floor
RSSI
Symbol Errors
Flags
Payload
```

Decode:

```python
print(x.decode(rx_packet))
```

Output:

```python
{
 'type':0,
 'timestamp':...,
 'noise_floor':-50,
 'rssi':-10,
 'symbol_errors':4,
 'flags':0,
 'payload':[0,1,2,3,4]
}
```

---

# Understanding RF RX Metadata

Noise Floor:

```text
Background radio noise
```

RSSI:

```text
Signal strength
```

Symbol Errors:

```text
How many Reed-Solomon corrections occurred
```

These values are useful for evaluating radio link quality.

---

# 11. Creating Command Packets

Command packets allow the host computer to control the radio.

Example:

```python
cmd_packet = x.encode_cmd_pkt(
    [70,82,69,81]
)
```

Decoded:

```python
{
 'type':3,
 'payload':[70,82,69,81]
}
```

Type:

```text
3 = Command Packet
```

---

# 12. Creating Local Packets

Local packets are generated internally by the radio.

Example:

```python
local_packet = x.encode_local_pkt(
    0,
    [0,1,2,3,4]
)
```

Decoded:

```python
{
 'type':2,
 'flags':0,
 'payload':[0,1,2,3,4]
}
```

Type:

```text
2 = Local Packet
```

---

# 13. Extension Packets

Import:

```python
from pyngham import PyNGHamExtension
```

Initialize:

```python
x = PyNGHamExtension()
```

Create payload container:

```python
payload = []
```

---

# 14. Creating an ID Extension Packet

Example:

```python
payload = x.append_id_pkt(
    payload,
    x.encode_callsign(
        "PU5GMA",
        1
    ),
    1
)
```

Print:

```python
print(payload)
```

Output:

```python
[1,9,80,85,53,71,77,65,32,1,1]
```

---

# Decoding Extension Packets

Decode:

```python
print(x.decode(payload))
```

Output:

```python
[
 {
  'call_ssid':('PU5GMA',1),
  'sequence':1
 }
]
```

---

# Understanding the Result

The decoder extracted:

```text
Callsign = PU5GMA
SSID     = 1
Sequence = 1
```

from the raw extension packet.

---

# 15. Building Multiple Extensions

Extension packets can be chained together.

Example:

```python
payload = []

payload = x.append_id_pkt(
    payload,
    x.encode_callsign("TESTSAT",1),
    1
)

# Additional extension packets may be added here
```

The resulting payload can then be embedded inside:

```text
NGHam Packet
```

or

```text
SPP Packet
```

---

# Real Communication Flow

A complete CubeSat communication path may look like:

```text
Telemetry Data
       │
       ▼
Extension Packet
       │
       ▼
NGHam Packet
       │
       ▼
Radio Transmission
       │
       ▼
Ground Radio
       │
       ▼
SPP Packet
       │
       ▼
Ground Station Software
```

---

# Summary

PyNGHam provides three major components:

| Class            | Purpose                                        |
| ---------------- | ---------------------------------------------- |
| PyNGHam          | Encode and decode NGHam RF packets             |
| PyNGHamSPP       | Encode and decode Serial Port Protocol packets |
| PyNGHamExtension | Create and decode NGHam Extension Packets      |

Together, these classes allow a complete implementation of:

* Packet creation
* Packet decoding
* Error correction
* Radio communication
* Telemetry transport
* Ground station interfaces

without manually implementing CRC generation, Reed-Solomon coding, packet framing, scrambling, or SPP packet handling.
