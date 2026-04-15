# JackFruit: Multi-Container Runtime

A lightweight, C-based multi-container runtime that utilizes Linux namespaces for isolation, a supervisor process for lifecycle management via UNIX domain sockets, and a custom kernel module for memory monitoring.

## 🚀 Features

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

## 📋 Usage Guide

### 1. Start the Supervisor
The supervisor tracks all running containers and manages their logs.
```bash
sudo ./engine supervisor ./rootfs-base
```

### 2. Start Containers
You can start multiple containers in parallel:
```bash
# Start a CPU-bound workload in container 'alpha'
sudo ./engine start alpha ./rootfs-alpha /cpu_workload

# Start an I/O-bound workload in container 'beta'
sudo ./engine start beta ./rootfs-beta /io_workload
```

### 3. Track Containers
List all active containers managed by the supervisor:
```bash
sudo ./engine ps
```

### 4. View Logs
Stream or view logs from a specific container:
```bash
sudo ./engine logs alpha
```

### 5. Scheduling (Priority Management)
Launch containers with specific nice values to observe scheduling behavior:
```bash
# High priority
sudo ./engine start priority-high ./rootfs-alpha /cpu_workload --nice -10

# Low priority
sudo ./engine start priority-low ./rootfs-beta /cpu_workload --nice 19
```

### 6. Memory Hard-Limits
Test memory limit enforcement using the kernel module:
```bash
sudo ./engine start mem-test ./rootfs-alpha /mem_workload --hard-mib 10
```
Check `dmesg` to see the kernel module in action when a process is killed for exceeding limits.

## 📐 Architecture

Our runtime analyzes the **Linux Completely Fair Scheduler (CFS)** by allowing users to launch containers with specific process priorities (nice values). The logic relies on creating isolated payload processes (`cpu_workload`, `io_workload`, etc.) and delegating time-slice management to the host kernel's CFS.

The supervisor manages a safe IPC logging pipeline between containers and the user space, ensuring `stdout` and `stderr` are captured using pipes without dropping lines or encountering deadlocks.

## 👥 Credits
- NAME :KAVANA P G
- SRN:PES1UG24CS669
- NAME:KRUTHIKA C B
- SRN:PES1UG24CS670
