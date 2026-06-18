# Week 1 / Task 1: Packet Creation and Exploring the NGHam Protocol

## Objective

The objective of this task was to understand how telemetry data is transformed into a complete NGHam packet and prepared for transmission in a CubeSat communication system.

During this task, I explored:

* Packet-based communication concepts
* Payload and packet structures
* Binary data representation
* NGHam packet format
* CRC error detection
* Reed-Solomon Forward Error Correction (FEC)
* Packet scrambling
* NGHam Serial Port Protocol (SPP)

## What I Learned

A payload represents the useful information to be transmitted, such as telemetry data collected from sensors. A packet is the complete transmission unit that wraps the payload together with synchronization, error detection, and error correction information.

The NGHam protocol is a packet radio protocol designed for amateur radio and CubeSat communications. Compared to AX.25, NGHam improves communication reliability through the use of Reed-Solomon Forward Error Correction and a well-defined packet structure.

I also learned that NGHam consists of multiple protocol layers:

* **NGHam RF Protocol** – used for radio-to-radio communication.
* **NGHam Serial Port Protocol (SPP)** – used for communication between a host computer and a radio over UART/USB.

## Documentation

Detailed notes and explanations created during this task:

* `NGHam/ngham-packet-explained.md`
* `NGHam/ngham-spp-explained.md`

These documents include packet structure diagrams, transmission flow explanations, payload creation examples, and practical NGHam packet walkthroughs.

## References

* [PyNGHam Documentation](https://mgm8.github.io/pyngham/?utm_source=chatgpt.com)
* [PyNGHam GitHub Repository](https://github.com/mgm8/pyngham?utm_source=chatgpt.com)
* [Original NGHam Protocol Repository](https://github.com/skagmo/ngham?utm_source=chatgpt.com)
