# Understanding NGHam Extension Packets

## Introduction

NGHam Extension Packets provide a structured way to include additional metadata and telemetry information inside an NGHam payload.

Instead of sending all information as a single block of raw bytes, NGHam allows payloads to be divided into smaller extension packets, each describing a specific type of information such as:

* Identification
* Position
* Status and telemetry
* Time information
* Destination information
* Generic user data

This makes packets easier to decode and provides a standardized format for exchanging information between stations.

---

# 1. Why Extension Packets Exist

Consider a CubeSat transmitting:

```text
Callsign    = NUTSAT
Latitude    = 12.9716
Longitude   = 77.5946
Battery     = 7.8V
Temperature = 25°C
Message     = Hello Earth
```

Without extension packets, everything might be packed into a single payload:

```text
CALL=NUTSAT,LAT=12.9716,LON=77.5946,BAT=7.8,TEMP=25,MSG=Hello Earth
```

Although this works, it is difficult to parse and wastes bandwidth.

Extension packets solve this by organizing information into well-defined structures.

Example:

```text
+-------------+
| ID Packet   |
+-------------+
| Position    |
+-------------+
| Status      |
+-------------+
| Data Packet |
+-------------+
```

Each extension packet contains a specific type of information.

---

# 2. Enabling Extension Packets

Extension packets are stored inside the NGHam payload.

The NGHam Header contains an Extension Flag:

```text
Bit 5 = Extension Enabled
```

When:

```text
Bit 5 = 0
```

the payload contains ordinary user data.

When:

```text
Bit 5 = 1
```

the payload contains one or more NGHam Extension Packets.

---

# 3. Generic Extension Packet Format

All extension packets follow the same structure:

```text
+--------+
| Type   |
+--------+
| Length |
+--------+
| Data   |
+--------+
```

Field descriptions:

| Field  | Size    | Description                      |
| ------ | ------- | -------------------------------- |
| Type   | 1 byte  | Extension packet type identifier |
| Length | 1 byte  | Number of data bytes             |
| Data   | N bytes | Packet-specific content          |

---

# 4. Multiple Extension Packets

A single NGHam payload may contain multiple extension packets.

Example:

```text
+-------------+
| ID Packet   |
+-------------+
| Position    |
+-------------+
| Status      |
+-------------+
| Data Packet |
+-------------+
```

The receiver processes them sequentially.

This allows multiple pieces of information to be transmitted within a single NGHam packet.

---

# 5. Data Packet

Type:

```text
00h
```

Purpose:

Generic user data.

Structure:

```text
+--------+
| Type   |
+--------+
| Length |
+--------+
| Data   |
+--------+
```

Example:

```text
Type   = 00
Length = 05
Data   = HELLO
```

Bytes:

```text
00 05 48 45 4C 4C 4F
```

---

# 6. ID Packet

Type:

```text
01h
```

Purpose:

Identify the transmitting station.

Structure:

| Field           | Size    |
| --------------- | ------- |
| Callsign        | 7 bytes |
| SSID            | 1 byte  |
| Sequence Number | 1 byte  |

Example:

```text
Callsign = NUTSAT
SSID     = 1
Sequence = 42
```

The sequence number helps detect lost packets.

Example:

```text
40
41
42
44
```

Packet 43 was likely lost.

### Note

According to the specification, the ID packet should normally be the first extension packet in the payload.

---

# 7. Status Packet

Type:

```text
02h
```

Purpose:

Transmit radio statistics and telemetry information.

Contains:

* Hardware version
* Serial number
* Software version
* Uptime
* Input voltage
* Temperature
* RSSI
* Noise floor
* Packet counters

Example:

```text
Voltage     = 7.8V
Temperature = 25°C
Uptime      = 7200 seconds
```

This packet is useful for health monitoring and diagnostics.

---

# 8. Position Packet

Type:

```text
04h
```

Purpose:

Transmit GPS and navigation information.

Fields:

| Field     | Size    |
| --------- | ------- |
| Latitude  | 4 bytes |
| Longitude | 4 bytes |
| Altitude  | 4 bytes |
| SOG       | 2 bytes |
| COG       | 2 bytes |
| HDOP      | 1 byte  |

Definitions:

* SOG = Speed Over Ground
* COG = Course Over Ground
* HDOP = Horizontal Dilution of Precision

Example:

```text
Latitude  = 12.9716
Longitude = 77.5946
Altitude  = 550000 m
```

This packet allows a receiver to determine the position of the transmitting station.

---

# 9. Time Information Packet

Type:

```text
05h
```

Purpose:

Transmit timing information.

Fields:

| Field              | Size    |
| ------------------ | ------- |
| TOH (Time Of Hour) | 4 bytes |
| Validity Flag      | 1 byte  |

Example:

```text
TOH = 34567890 µs
```

This information may be used for synchronization and timing analysis.

---

# 10. Destination Packet

Type:

```text
06h
```

Purpose:

Specify the intended destination station.

Fields:

| Field    | Size    |
| -------- | ------- |
| Callsign | 7 bytes |
| SSID     | 1 byte  |

Example:

```text
Destination = GROUND1
SSID        = 0
```

This identifies the target station for the packet.

---

# 11. Unimplemented Extension Packets

The original NGHam documentation mentions additional packet types:

* Simple Digipeater Packet
* Command Request Packet
* Command Reply Packet
* Request Packet

However, the specification is incomplete and some packet definitions are not fully documented.

As a result, support for these packet types may vary between implementations.

---

# 12. Example Extension Payload

Suppose a CubeSat wants to transmit:

```text
Callsign     = NUTSAT
Position     = Current GPS Position
Battery      = 7.8V
Temperature  = 25°C
Message      = Hello Earth
```

The payload may contain:

```text
+-------------+
| ID Packet   |
+-------------+
| Position    |
+-------------+
| Status      |
+-------------+
| Data Packet |
+-------------+
```

The receiver can decode each section independently.

---

# 13. Relationship to the NGHam Packet

The NGHam packet structure is:

```text
NGHam Packet
    |
    +-- Header
    |
    +-- Payload
    |
    +-- CRC16
    |
    +-- Reed-Solomon
```

When extensions are enabled:

```text
Payload
    |
    +-- ID Packet
    +-- Position Packet
    +-- Status Packet
    +-- Data Packet
```

Extension packets exist entirely inside the payload field.

They do not replace the NGHam packet format; they simply provide a structured way to organize payload data.

---

# Summary

Extension packets allow NGHam payloads to carry structured information instead of raw data.

Benefits include:

* Standardized telemetry formats
* Easier packet parsing
* Support for multiple information types in a single packet
* Better interoperability between NGHam implementations

Common extension packet types include:

| Type | Purpose            |
| ---- | ------------------ |
| 00h  | Generic Data       |
| 01h  | Identification     |
| 02h  | Status / Telemetry |
| 04h  | Position           |
| 05h  | Time Information   |
| 06h  | Destination        |

Extension packets are carried inside the NGHam payload and are enabled through the Extension Flag in the NGHam packet header.
