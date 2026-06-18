# PyNGHam Examples

## Introduction

This document contains practical examples of using the PyNGHam library for:

* NGHam packet encoding
* NGHam packet decoding
* Error correction testing
* Stream decoding
* Serial Port Protocol (SPP)
* Extension Packets
* CubeSat telemetry examples

---

# Example 1: Create Your First NGHam Packet

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode([0, 1, 2, 3, 4])

print(packet)
```

Purpose:

* Creates a basic NGHam packet
* Automatically adds CRC, Reed-Solomon parity, scrambling, and framing

---

# Example 2: Decode a Packet

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode([0, 1, 2, 3, 4])

decoded = ngham.decode(packet)

print(decoded)
```

Expected output:

```python
([0, 1, 2, 3, 4], 0, [])
```

Meaning:

* Original data recovered
* No errors detected

---

# Example 3: Encode a String

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode("HELLO WORLD")

print(packet)
```

Useful for:

* Text messages
* Commands
* Telemetry labels

---

# Example 4: Encode Raw Bytes

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode(
    bytes([10, 20, 30, 40, 50])
)

print(packet)
```

Useful for:

* Sensor data
* Binary protocols
* Embedded systems

---

# Example 5: Inspect Packet Length

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode("HELLO")

print("Payload Length:", len("HELLO"))
print("Packet Length:", len(packet))
```

Observe:

NGHam packets are larger than the payload because protocol fields are added automatically.

---

# Example 6: Reed-Solomon Error Correction

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode([0, 1, 2, 3, 4])

packet[30] = 5

result = ngham.decode(packet)

print(result)
```

Expected:

```python
([0,1,2,3,4], 1, [227])
```

Meaning:

* One error detected
* Data recovered successfully

---

# Example 7: Corrupt Multiple Bytes

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode([10,20,30,40,50])

packet[20] ^= 0xFF
packet[21] ^= 0xAA

print(ngham.decode(packet))
```

Demonstrates Reed-Solomon correction capability.

---

# Example 8: Decode Byte by Byte

```python
from pyngham import PyNGHam

ngham = PyNGHam()

packet = ngham.encode("TEST")

for byte in packet:
    result = ngham.decode_byte(byte)

    if result[0]:
        print("Packet Complete")
        print(result)
```

Simulates real radio reception.

---

# Example 9: Encode Telemetry Data

```python
from pyngham import PyNGHam

ngham = PyNGHam()

temperature = 25
battery_voltage = 78

payload = bytes([
    temperature,
    battery_voltage
])

packet = ngham.encode(payload)

print(packet)
```

---

# Example 10: Decode Telemetry Data

```python
from pyngham import PyNGHam

ngham = PyNGHam()

payload = bytes([25, 78])

packet = ngham.encode(payload)

data, errors, positions = ngham.decode(packet)

temperature = data[0]
battery = data[1]

print("Temperature:", temperature)
print("Battery:", battery)
```

---

# Example 11: Create an SPP TX Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

packet = spp.encode_tx_pkt(
    0,
    [1,2,3,4,5]
)

print(packet)
```

Purpose:

Host → Radio communication

---

# Example 12: Decode an SPP TX Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

packet = spp.encode_tx_pkt(
    0,
    [1,2,3,4,5]
)

print(spp.decode(packet))
```

Expected:

```python
{
 'type':1,
 'flags':0,
 'payload':[1,2,3,4,5]
}
```

---

# Example 13: Create an SPP RX Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

packet = spp.encode_rx_pkt(
    -50,
    -10,
    4,
    0,
    [1,2,3,4,5]
)

print(packet)
```

Purpose:

Radio → Host communication

---

# Example 14: Decode an SPP RX Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

packet = spp.encode_rx_pkt(
    -50,
    -10,
    4,
    0,
    [1,2,3,4,5]
)

print(spp.decode(packet))
```

Shows:

* RSSI
* Noise Floor
* Symbol Errors
* Payload

---

# Example 15: Create a Command Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

cmd = spp.encode_cmd_pkt(
    b"FREQ 144800000"
)

print(cmd)
```

Purpose:

Send commands to a radio.

---

# Example 16: Decode a Command Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

cmd = spp.encode_cmd_pkt(
    b"STATUS"
)

print(spp.decode(cmd))
```

---

# Example 17: Create a Local Packet

```python
from pyngham import PyNGHamSPP

spp = PyNGHamSPP()

local = spp.encode_local_pkt(
    0,
    [1,2,3,4]
)

print(local)
```

Used for locally generated radio events.

---

# Example 18: Create an ID Extension Packet

```python
from pyngham import PyNGHamExtension

ext = PyNGHamExtension()

payload = []

payload = ext.append_id_pkt(
    payload,
    ext.encode_callsign(
        "TESTSAT",
        1
    ),
    42
)

print(payload)
```

---

# Example 19: Decode an ID Extension Packet

```python
from pyngham import PyNGHamExtension

ext = PyNGHamExtension()

payload = []

payload = ext.append_id_pkt(
    payload,
    ext.encode_callsign(
        "TESTSAT",
        1
    ),
    42
)

print(ext.decode(payload))
```

Expected:

```python
[
 {
  'call_ssid': ('TESTSAT',1),
  'sequence': 42
 }
]
```

---

# Example 20: Create Multiple Extension Packets

```python
from pyngham import PyNGHamExtension

ext = PyNGHamExtension()

payload = []

payload = ext.append_id_pkt(
    payload,
    ext.encode_callsign(
        "MYSAT",
        1
    ),
    100
)

print(payload)
```

This payload can later be embedded inside an NGHam packet.

---

# Example 21: Extension Packet + NGHam Packet

```python
from pyngham import PyNGHam
from pyngham import PyNGHamExtension

ngham = PyNGHam()
ext = PyNGHamExtension()

payload = []

payload = ext.append_id_pkt(
    payload,
    ext.encode_callsign(
        "MYSAT",
        1
    ),
    10
)

packet = ngham.encode(payload)

print(packet)
```

This is a real NGHam packet carrying extension data.

---

# Example 22: Decode Extension Data from NGHam Packet

```python
from pyngham import PyNGHam
from pyngham import PyNGHamExtension

ngham = PyNGHam()
ext = PyNGHamExtension()

payload = []

payload = ext.append_id_pkt(
    payload,
    ext.encode_callsign(
        "MYSAT",
        1
    ),
    10
)

packet = ngham.encode(payload)

decoded_payload, errors, positions = ngham.decode(packet)

print(ext.decode(decoded_payload))
```

---

# Example 23: CubeSat Telemetry Packet

```python
from pyngham import PyNGHam

ngham = PyNGHam()

temperature = 24
battery = 81
mode = 2

payload = bytes([
    temperature,
    battery,
    mode
])

packet = ngham.encode(payload)

print(packet)
```

---

# Example 24: CubeSat Ground Station Decoder

```python
from pyngham import PyNGHam

ngham = PyNGHam()

payload = bytes([
    24,
    81,
    2
])

packet = ngham.encode(payload)

data, errors, positions = ngham.decode(packet)

print("Temperature:", data[0])
print("Battery:", data[1])
print("Mode:", data[2])
```

---

# Example 25: Complete Communication Workflow

```python
from pyngham import PyNGHam

satellite = PyNGHam()
ground = PyNGHam()

message = "HELLO EARTH"

packet = satellite.encode(message)

print("Transmitted Packet:")
print(packet)

decoded = ground.decode(packet)

print("\nReceived:")
print(decoded)
```

Workflow:

Message

↓

NGHam Encode

↓

Packet

↓

Transmission

↓

NGHam Decode

↓

Original Message

---

# Challenge Exercises

1. Create a packet containing:

```text
HELLO SATELLITE
```

2. Corrupt one byte and verify Reed-Solomon correction.

3. Create an SPP TX packet.

4. Create an SPP RX packet.

5. Create an ID Extension Packet.

6. Embed an Extension Packet inside an NGHam packet.

7. Build a telemetry packet containing:

```text
Temperature
Voltage
Mode
```

and decode it successfully.
