# JackFruit: Multi-Container Runtime

A lightweight, C-based multi-container runtime that utilizes Linux namespaces for isolation, a supervisor process for lifecycle management via UNIX domain sockets, and a custom kernel module for memory monitoring.

## Features

Our runtime implements the following core features:

- **Shell & CLI**: A robust command-line interface with parsing and built-in control over the supervisor.
- **Process Management**: Heavy use of `clone()`, `fork()`, `execvp()`, and `waitpid()` for managing container lifecycles safely.
- **Scheduling (CFS)**: Integration with the Linux Completely Fair Scheduler (CFS) to demonstrate container-level priority management using nice values.
- **Advanced Features**:
  - Background supervisor execution.
  - Pipe-based IPC for container logging.
  - I/O redirection for a unified logging pipeline.
  - Custom Kernel Module for memory hard-limit enforcement.

## 🛠️ Build & Installation

### Prerequisites
- Linux Kernel (with support for namespaces)
- GCC Compiler
- `make`

### Build the Project
Clone the repository and run `make` in the root directory:
```bash
make
```

### Install the Kernel Monitor
Load the custom memory monitor kernel module:
```bash
sudo insmod monitor.ko
```

##  Usage Guide

### 1. Start the Supervisor
The supervisor tracks all running containers and manages their logs.
<img width="1216" height="690" alt="WhatsApp Image 2026-04-15 at 7 50 48 PM" src="https://github.com/user-attachments/assets/aa4d91c5-9974-4b95-a01d-b1253cdf64bd" />

```bash
sudo ./engine supervisor ./rootfs-base
```

### 2. Start Containers
<img width="1216" height="690" alt="WhatsApp Image 2026-04-15 at 7 50 48 PM" src="https://github.com/user-attachments/assets/59a6d7ed-0079-49fe-88c1-6fc70f2e9fa0" />

```bash

# Start a CPU-bound workload in container 'alpha'
sudo ./engine start alpha ./rootfs-alpha /cpu_workload

# Start an I/O-bound workload in container 'beta'
sudo ./engine start beta ./rootfs-beta /io_workload
```

### 3. Track Containers

List all active containers managed by the supervisor:
<img width="1217" height="289" alt="WhatsApp Image 2026-04-15 at 7 51 37 PM" src="https://github.com/user-attachments/assets/ffa54eb2-923f-45e7-b98b-514f50fc51ad" />

```bash
sudo ./engine ps
```

### 4. View Logs
<img width="1216" height="690" alt="WhatsApp Image 2026-04-15 at 7 51 59 PM" src="https://github.com/user-attachments/assets/ade36aa8-445b-40bd-9c32-1ec3b6267f42" />

Stream or view logs from a specific container:
```bash
sudo ./engine logs alpha
```

### 5. Scheduling (Priority Management)
<img width="1184" height="250" alt="WhatsApp Image 2026-04-15 at 7 52 11 PM" src="https://github.com/user-attachments/assets/41d9989c-450e-4621-a981-e3f1c3231e35" />
<img width="1191" height="103" alt="WhatsApp Image 2026-04-15 at 7 52 44 PM" src="https://github.com/user-attachments/assets/b4448358-a40e-43e3-954d-0c4c4b37bfa5" />


Launch containers with specific nice values to observe scheduling behavior:
```bash
# High priority
sudo ./engine start priority-high ./rootfs-alpha /cpu_workload --nice -10

# Low priority
sudo ./engine start priority-low ./rootfs-beta /cpu_workload --nice 19
```

### 6. Memory Hard-Limits
<img width="1033" height="392" alt="WhatsApp Image 2026-04-15 at 7 52 58 PM" src="https://github.com/user-attachments/assets/4abb58a0-c5f6-47c1-a329-6a9dd1a8076d" />
<img width="1217" height="289" alt="WhatsApp Image 2026-04-15 at 7 54 13 PM" src="https://github.com/user-attachments/assets/a16c0740-d09c-4b6c-abfe-c640fe898abe" />

<img width="1106" height="548" alt="image" src="https://github.com/user-attachments/assets/a369baf7-05f9-410e-8adb-32bb38a3f403" />



Test memory limit enforcement using the kernel module:
```bash
sudo ./engine start mem-test ./rootfs-alpha /mem_workload --hard-mib 10
```
Check `dmesg` to see the kernel module in action when a process is killed for exceeding limits.

##  Architecture

Our runtime analyzes the **Linux Completely Fair Scheduler (CFS)** by allowing users to launch containers with specific process priorities (nice values). The logic relies on creating isolated payload processes (`cpu_workload`, `io_workload`, etc.) and delegating time-slice management to the host kernel's CFS.

The supervisor manages a safe IPC logging pipeline between containers and the user space, ensuring `stdout` and `stderr` are captured using pipes without dropping lines or encountering deadlocks.

## 👥 Credits
- NAME :KAVANA P G
- SRN:PES1UG24CS669
- NAME:KRUTHIKA C B
- SRN:PES1UG24CS670
