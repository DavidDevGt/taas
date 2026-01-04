![platform](https://img.shields.io/badge/platform-Raspberry%20Pi%20Zero%202%20W-red)
![kernel](https://img.shields.io/badge/kernel-6.12.x--v7-green)
![license](https://img.shields.io/badge/license-GPLv2-blue)
![realtime](https://img.shields.io/badge/realtime-SCHED_FIFO_99-critical)
![memory](https://img.shields.io/badge/memory-mlockall-orange)
![crypto](https://img.shields.io/badge/crypto-Ed25519-blue)

# TaaS — Time as a Service (Enterprise Edition)

**Deterministic hardware time access and PTP node for Raspberry Pi (BCM2837)**

---

## Overview

**TaaS (Time as a Service)** is a low-level timing architecture focused on **deterministic, low-latency time access** on Linux-based embedded systems.

Instead of relying on syscall-based clocks (`clock_gettime`, hrtimers, vDSO), TaaS exposes the **BCM2837 64-bit System Timer** directly to user space via a minimal kernel driver and an `mmap()`-based MMIO interface.

The goal is not abstraction — the goal is **predictability**.

This project exists because:
* Scheduler latency exists
* Syscalls are not free
* Time-sensitive systems should read time, not ask for it

---

## Design Goals

* Deterministic reads (no scheduler involvement)
* Zero-copy MMIO access
* No timekeeping reinvention
* Minimal kernel surface area
* Clear failure modes
* No background threads in kernel space

If you need portability or POSIX compliance, this is not for you.

---

## System Architecture

TaaS maps the hardware timer directly into user space.
The kernel is used only to **validate, map and protect access**.

```text
┌──────────────────────────────┐
│      BCM2837 SoC Hardware    │
│   64-bit System Timer (ST)   │
└───────────────┬──────────────┘
                │ MMIO
┌───────────────▼──────────────┐
│      Kernel Module (taas)    │
│  - Non-cached page mapping   │
│  - Atomic 64-bit read logic  │
│  - miscdevice (/dev/taas)    │
└───────────────┬──────────────┘
                │ mmap()
┌───────────────▼──────────────┐
│       User Space Node        │
│  - SCHED_FIFO (prio 99)      │
│  - mlockall(MCL_CURRENT|FUT) │
│  - UDP time responder        │
│  - Ed25519 Signing Engine    │ 
└──────────────────────────────┘

```

No kernel threads.
No ioctl spam.
No polling inside the kernel.

---

## Kernel Driver (`taas_driver.c`)

The kernel module performs **three tasks only**:

1. Maps the System Timer registers using MMIO
2. Ensures non-cached access (`pgprot_noncached`)
3. Exposes a read-only memory region via `mmap()`

### Atomicity

BCM2837 exposes the timer via two 32-bit registers.
The driver implements a verification loop to guarantee a **consistent 64-bit read** without locks.

No spinlocks.
No IRQ hooks.
No scheduler interaction.

---

## User Space Node (`taas_node.c`)

The user-space daemon is intentionally simple and written in plain C.

Key properties:

* Runs under `SCHED_FIFO` priority 99
* Memory fully locked using `mlockall()`
* No dynamic allocation after init (Zero-Alloc Hot-Path)
* No libc time functions
* Reads hardware time directly from mapped memory
* **Integrated Ed25519 TSA:** Provides signed "Proof of Existence" certificates.

The process does not *request* time — it **loads it**.

---

## Installation

### Requirements

* Raspberry Pi Zero 2 W
(RPi 3 supported with adjusted MMIO offsets)
* Raspberry Pi OS (Debian 13 / newer)
* Kernel headers
* OpenSSL Libs (`libssl-dev`)

```bash
sudo apt install raspberrypi-kernel-headers libssl-dev

```

### Setup

1. **Generate Identity (Node Keys):**

```bash
openssl genpkey -algorithm ed25519 -out private_key.pem

```

2. **Deploy:**

```bash
chmod +x setup_taas.sh
sudo ./setup_taas.sh

```

To ensure deterministic behavior and avoid scheduler interference, the Raspberry Pi **must** be configured with CPU isolation and tickless operation.

Edit `/boot/cmdline.txt` and append (single line, space-separated):

```text
isolcpus=3 rcu_nocbs=3 nohz_full=3

```

This dedicates **CPU core 3** exclusively to the TaaS user-space node.

---

## Validation & Usage

### 1. Basic UDP Timer (Raw PTP)

```bash
echo -n "ping" | nc -u -w 1 127.0.0.1 1588 | hexdump -C

```

The returned value is the **raw System Timer value** (8 bytes).

### 2. Trusted Timestamping (TSA Certificate)

Send a 32-byte SHA256 hash to receive a 104-byte signed certificate:

```bash
# Example: notarizing a file
sha256sum hola.txt | cut -d' ' -f1 | xxd -r -p | nc -u -w 1 127.0.0.1 1588 > cert.tsr

```

---

## Compatibility Matrix

| Component | Status |
| --- | --- |
| Hardware | Raspberry Pi Zero 2 W ✅ |
| SoC | BCM2837 ✅ |
| Architecture | armv7l (32-bit) ✅ |
| Kernel | Linux 6.12.x-rpi-v7 ✅ |
| Time Source | System Timer (µs) ✅ |
| Crypto | Ed25519 Signature ✅ |

---

## Limitations

* Not portable
* Tied to BCM2837 memory layout
* Not synchronized by default
* No clock discipline logic
* No leap second handling

This is intentional.

---

## License

GPLv2
Kernel code follows Linux kernel licensing conventions.

---

## Philosophy

> In real-time systems, abstraction is latency.
> The kernel should protect access — not stand in the way.

