# HFTSHM

A header-only C++ library for low-latency, high-performance shared memory IPC designed for high-frequency trading systems.

## Overview

HFTSHM implements a **Single Producer, Multiple Consumer (SPMC)** ringbuffer architecture for inter-process communication. It features support for huge pages (Linux), cache-line alignment, and zero-copy data exchange between processes.

## Features

- **SPMC Ringbuffer**: Optimized for one fast producer and multiple readers
- **Huge Page Support**: 2MB and 1GB huge pages on Linux to minimize TLB misses
- **Cache-Line Alignment**: Prevents false sharing between producer and consumers
- **Power-of-2 Buffers**: Bitwise operations for fast index calculations
- **Zero-Copy IPC**: Direct memory mapping for minimal latency
- **Cross-Platform**: Linux and macOS support with platform-specific optimizations
- **Header-Only**: No compilation required, just `#include` the headers

## Requirements

- C++17 or later
- Linux or macOS
- (Optional) Huge pages configured on Linux for optimal performance

## Installation

Copy the `hftshm/` directory to your project's include path:

```bash
cp -r hftshm /path/to/your/project/include/
```

Then include the headers in your code:

```cpp
#include "hftshm/layout.hpp"
#include "hftshm/platform.hpp"
```

## Directory Structure

```
hftshm/
├── layout.hpp    # Metadata structure and ringbuffer layout calculations
├── platform.hpp  # Platform-specific shared memory implementations
└── types.hpp     # Core data types (SegmentInfo, SegmentHandle)
```

## Architecture

### Memory Layout

The shared memory is organized into two segments:

1. **Header Segment**: Contains metadata and control sections
   - Metadata section (1 cache line)
   - Producer section (128 bytes default)
   - Consumer sections (128 bytes each)

2. **Data Segment**: Contains the ringbuffer for actual data

### Platform Policies

| Platform | Shared Memory Path | Huge Pages |
|----------|-------------------|------------|
| Linux    | `/dev/shm/hft/`   | Supported (2MB, 1GB) |
| macOS    | `/tmp/hft/`       | Not available |

### Naming and Path Conventions

Each shared memory buffer consists of two segment files with standardized extensions:

| Segment | Extension | Description |
|---------|-----------|-------------|
| Header  | `.hdr`    | Metadata + producer/consumer control sections |
| Data    | `.dat`    | Ringbuffer data storage |

**File Naming Pattern:**
```
{BASE_PATH}/{buffer_name}.hdr    # Header segment
{BASE_PATH}/{buffer_name}.dat    # Data segment
```

**Examples:**

| Buffer Name | Platform | Header Path | Data Path |
|-------------|----------|-------------|-----------|
| `orderbook` | Linux | `/dev/shm/hft/orderbook.hdr` | `/dev/shm/hft/orderbook.dat` |
| `marketdata`| Linux | `/dev/shm/hft/marketdata.hdr` | `/dev/shm/hft/marketdata.dat` |
| `orderbook` | macOS | `/tmp/hft/orderbook.hdr` | `/tmp/hft/orderbook.dat` |

**Helper Functions:**
```cpp
Policy policy;
std::string header_path = policy.get_header_path("orderbook");  // Full path with .hdr
std::string data_path = policy.get_data_path("orderbook");      // Full path with .dat
```

## Usage Example

```cpp
#include "hftshm/layout.hpp"
#include "hftshm/platform.hpp"

using namespace hftshm;
using namespace hftshm::policies;

// Buffer configuration
constexpr const char* BUFFER_NAME = "orderbook";
constexpr size_t EVENT_SIZE = 64;           // Size of each event in bytes
constexpr size_t BUFFER_SLOTS = 1024;       // Number of slots (must be power of 2)
constexpr size_t MAX_CONSUMERS = 4;
constexpr size_t HUGEPAGE_SIZE = HUGEPAGE_2MB;  // 2MB huge pages

// Calculate segment sizes
size_t header_size = header_segment_size(MAX_CONSUMERS, HUGEPAGE_SIZE);
size_t data_size = data_segment_size(EVENT_SIZE, BUFFER_SLOTS, HUGEPAGE_SIZE);

// Create shared memory segments using platform policy
DefaultPlatformPolicy policy;

// Creates /dev/shm/hft/orderbook.hdr (Linux) or /tmp/hft/orderbook.hdr (macOS)
int header_fd = policy.create(std::string(BUFFER_NAME) + ".hdr", header_size, HUGEPAGE_SIZE);

// Creates /dev/shm/hft/orderbook.dat (Linux) or /tmp/hft/orderbook.dat (macOS)
int data_fd = policy.create(std::string(BUFFER_NAME) + ".dat", data_size, HUGEPAGE_SIZE);

// Map segments into memory
void* header_ptr = policy.map(header_fd, header_size, HUGEPAGE_SIZE);
void* data_ptr = policy.map(data_fd, data_size, HUGEPAGE_SIZE);

// Initialize metadata
metadata* meta = static_cast<metadata*>(header_ptr);
metadata_init(meta, MAX_CONSUMERS, EVENT_SIZE, BUFFER_SLOTS);
```

## Performance Considerations

- **Huge Pages**: Configure huge pages on Linux for reduced TLB pressure
- **CPU Pinning**: Pin producer and consumer processes to specific cores
- **NUMA Awareness**: Allocate shared memory on the same NUMA node as processes
- **Cache Line Size**: Library auto-detects 64 bytes (x86) or 128 bytes (Apple Silicon)

## Configuring Huge Pages (Linux)

```bash
# Check available huge pages
cat /proc/meminfo | grep Huge

# Allocate 2MB huge pages
echo 1024 | sudo tee /proc/sys/vm/nr_hugepages

# For persistent configuration, add to /etc/sysctl.conf:
# vm.nr_hugepages = 1024
```

## License

See LICENSE file for details.
