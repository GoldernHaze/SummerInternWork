# My summer Inern Work Docs

## Overview

The objective of Week 1 was to understand the communication protocol and software architecture used in the CubeSat TT&C (Telemetry, Tracking & Command) system.

The work was divided into three tasks:

1. Understanding the NGHam communication protocol.
2. Learning the fundamentals of FreeRTOS.
3. Designing the packet handling pipeline using FreeRTOS tasks.

---

# Task 1 – Understanding the NGHam Protocol

## Objective

The goal of this task was to understand how telemetry data is transformed into an NGHam packet before being transmitted over the radio.

## Topics Covered

- Packet-based communication
- Payload vs Packet
- Binary data representation
- NGHam packet structure
- CRC error detection
- Reed-Solomon Forward Error Correction (FEC)
- Packet scrambling
- NGHam Serial Port Protocol (SPP)

## Learning Outcomes

During this task I learned:

- The difference between a payload and a transmission packet.
- How NGHam improves communication reliability compared to AX.25.
- The complete NGHam packet structure.
- The purpose of CRC and Reed-Solomon error correction.
- How packets are prepared before radio transmission.
- The communication flow between a host computer and a radio using NGHam SPP.

## Documentation

```
NGHam/
├── ngham-packet-explained.md
└── ngham-spp-explained.md
```

---

# Task 2 – Learning FreeRTOS Fundamentals

## Objective

The objective of this task was to understand the fundamentals of FreeRTOS and how embedded applications are divided into independent concurrent tasks.

These concepts are required before implementing the packet handling system.

## Topics Covered

- What is an RTOS?
- Bare-metal vs RTOS
- FreeRTOS architecture
- Task creation
- Scheduler
- Task priorities
- Context switching
- Delays (`vTaskDelay`)
- Queues
- Semaphores
- Mutexes
- Memory management
- Software timers

## Learning Outcomes

After completing this task I understood:

- How FreeRTOS schedules multiple tasks.
- Why tasks execute independently.
- How queues enable communication between tasks.
- How synchronization primitives protect shared resources.
- Why FreeRTOS is widely used in embedded systems and CubeSat software.

## Documentation

```
FreeRTOS/
├── freertos-fundamentals.md
├── task-scheduling.md
└── queues-and-intertask-communication.md
```

---

# Task 3 – Packet Handling Flow using FreeRTOS

## Objective

The objective of this task was to understand how payload data can be prepared for transmission using multiple FreeRTOS tasks.

The implementation focuses on reading payload data, fragmenting it into packets, maintaining transmission order, and preparing packets for NGHam encoding.

## Planned Processing Flow

```
Payload Memory
      │
      ▼
Memory Reader Task
      │
      ▼
Fragmentation Task
      │
      ▼
Packet Builder
      │
      ▼
FIFO Queue
      │
      ▼
NGHam Encoder Task
      │
      ▼
Radio Driver
      │
      ▼
Transmission
```

## Packet Fragmentation

Large payloads cannot always fit inside a single transmission packet.

Each packet fragment contains:

- Payload data
- Sequence number
- Occultation ID
- Payload size
- Additional packet metadata

These fields allow the receiver to correctly reconstruct the complete payload after transmission.

## FreeRTOS Task Architecture

### Task 1 – Payload Reader

Responsibilities:

- Simulate payload memory
- Read payload data
- Divide large payloads into fixed-size chunks

---

### Task 2 – FIFO Queue Manager

Responsibilities:

- Receive fragmented packets
- Store packets in transmission order
- Deliver packets to the encoder

---

### Task 3 – NGHam Encoder

Responsibilities:

- Receive packets from the queue
- Create NGHam payloads
- Apply CRC
- Apply Reed-Solomon FEC
- Scramble packets
- Generate complete NGHam frames

These frames are then ready for transmission by the radio.

## Planned MCU Simulation

The implementation will initially simulate payload memory on a microcontroller.

Simulation workflow:

1. Store sample payload data.
2. Read data from simulated memory.
3. Fragment payload into packets.
4. Push packets into a FreeRTOS queue.
5. Build NGHam frames.
6. Output generated packets for testing.

## Learning Outcomes

This task demonstrated how a communication pipeline can be divided into multiple FreeRTOS tasks.

The use of queues provides:

- Ordered packet transmission
- Modular software design
- Independent processing stages
- Scalable architecture suitable for CubeSat firmware

## Documentation

```
FreeRTOS/
├── packet-handling-flow.md
├── fifo-queue-design.md
└── ngham-task-architecture.md
```

---

# Repository Structure

```
Week-1/
│
├── README.md
│
├── NGHam/
│   ├── ngham-packet-explained.md
│   └── ngham-spp-explained.md
│
└── FreeRTOS/
    ├── freertos-fundamentals.md
    ├── task-scheduling.md
    ├── queues-and-intertask-communication.md
    ├── packet-handling-flow.md
    ├── fifo-queue-design.md
    └── ngham-task-architecture.md
```

---

# References

- FreeRTOS Documentation
- PyNGHam Documentation
- PyNGHam GitHub Repository
- Original NGHam Protocol Repository
- TT&C Firmware Repository (for queue and NGHam implementation examples)

---

## Next Steps

The next stage of the project will focus on implementing the packet handling pipeline on an MCU using FreeRTOS, including:

- Simulated payload memory
- Payload fragmentation
- FIFO queue implementation
- NGHam packet generation
- End-to-end packet transmission simulation
- dont till task1
