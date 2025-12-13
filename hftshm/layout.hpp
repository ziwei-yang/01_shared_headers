#pragma once

#include <cstdint>
#include <cstddef>
#include <algorithm>
#include <string>
#include <string_view>
#include <new>  // For std::hardware_destructive_interference_size

namespace hftshm {

// ============================================================================
// Cache and Page Constants
// ============================================================================

// Cache line size - platform dependent
// x86/x64: 64 bytes
// Apple Silicon (M1/M2/M3): 128 bytes
#ifdef __cpp_lib_hardware_interference_size
inline constexpr std::size_t CACHE_LINE = std::hardware_destructive_interference_size;
#elif defined(__aarch64__) && defined(__APPLE__)
inline constexpr std::size_t CACHE_LINE = 128;  // Apple Silicon
#else
inline constexpr std::size_t CACHE_LINE = 64;   // x86/x64 default
#endif
static_assert(CACHE_LINE == 64 || CACHE_LINE == 128, "Unexpected cache line size");

// Page size (for header segment alignment)
inline constexpr uint32_t PAGE_SIZE = 4096;
inline constexpr uint8_t PAGE_SIZE_LOG2 = 12;  // log2(4096)

// ============================================================================
// Path Conventions
// ============================================================================

// Platform-specific base path for shared memory files
#if defined(__linux__)
inline constexpr std::string_view BASE_PATH = "/dev/shm/hft";
#elif defined(__APPLE__)
inline constexpr std::string_view BASE_PATH = "/tmp/hft";
#else
inline constexpr std::string_view BASE_PATH = "/tmp/hft";
#endif

// Get header file path: <base>/<name>.hdr
inline std::string get_header_path(std::string_view name) {
    return std::string(BASE_PATH) + "/" + std::string(name) + ".hdr";
}

// Get data file path: <base>/<name>.dat
inline std::string get_data_path(std::string_view name) {
    return std::string(BASE_PATH) + "/" + std::string(name) + ".dat";
}

// ============================================================================
// Magic Number and Version
// ============================================================================

// Magic number: "HFTSHM\x02\x00" in little-endian (version 2)
inline constexpr uint64_t METADATA_MAGIC = 0x00024D4853544648ULL;

// Current layout version (2 = separate header/data segments)
inline constexpr uint8_t METADATA_VERSION = 2;

// ============================================================================
// Power-of-2 Helpers
// ============================================================================

// Convert size to log2 (size must be power of 2)
inline constexpr uint8_t size_to_log2(uint32_t size) {
    uint8_t log2 = 0;
    while ((1u << log2) < size) ++log2;
    return log2;
}

// Convert log2 back to size
inline constexpr uint32_t log2_to_size(uint8_t log2) {
    return 1u << log2;
}

// Check if value is power of 2
inline constexpr bool is_power_of_2(uint32_t x) {
    return x && !(x & (x - 1));
}

// ============================================================================
// Metadata Structure (Header File Layout)
// ============================================================================

// Fixed fields size (before padding) = 39 bytes
inline constexpr std::size_t METADATA_FIXED_SIZE = 39;

// Metadata section (cache-line aligned)
// SPMC only: single producer, multiple consumers
// Version 2: header and data are separate segments
// Note: sizeof(metadata) = CACHE_LINE (64 bytes on x86, 128 bytes on Apple Silicon)
//
// This layout matches the hft-shm CLI format for compatibility.
// Use size_to_log2() at runtime if shift operations are needed.
struct alignas(CACHE_LINE) metadata {
    uint64_t magic;               // 0x00: Magic number (METADATA_MAGIC)
    uint8_t  version;             // 0x08: Layout version (METADATA_VERSION)
    uint8_t  max_consumers;       // 0x09: Max consumers (fixed at creation)
    uint16_t event_size;          // 0x0A: Event size in bytes (0 for variable)
    uint32_t producer_pid;        // 0x0C: Producer PID (0 = not attached)
    uint32_t buffer_size;         // 0x10: Buffer size in bytes (power of 2)
    uint32_t producer_offset;     // 0x14: Offset to producer section
    uint32_t consumer_0_offset;   // 0x18: Offset to consumer 0 section
    uint32_t header_size;         // 0x1C: Total header segment size (page-aligned)
    uint32_t index_mask;          // 0x20: = buffer_size - 1
    uint8_t  event_size_log2;     // 0x24: log2(event_size) for shift ops
    uint8_t  buffer_size_log2;    // 0x25: log2(buffer_size) for shift ops
    uint8_t  header_size_log2;    // 0x26: log2(header_size) for shift ops
    uint8_t  padding[CACHE_LINE - METADATA_FIXED_SIZE];
};
static_assert(sizeof(metadata) == CACHE_LINE);
static_assert(alignof(metadata) == CACHE_LINE);

// ============================================================================
// Metadata Accessors
// ============================================================================

// Direct access to sizes
// meta->event_size   - event size in bytes (0 for variable-size)
// meta->buffer_size  - buffer size in bytes (power of 2)

// Fast index-to-offset: index * event_size = index << event_size_log2
inline uint32_t event_offset(const metadata* meta, uint32_t index) {
    return index << meta->event_size_log2;
}

// Fast buffer index masking: index & mask
inline uint32_t buffer_index(const metadata* meta, uint64_t sequence) {
    return static_cast<uint32_t>(sequence) & meta->index_mask;
}

// Validation: verify buffer_size is power of 2 and index_mask matches
inline bool validate_sizes(const metadata* meta) {
    bool buffer_ok = is_power_of_2(meta->buffer_size) &&
                     (meta->index_mask == meta->buffer_size - 1);
    return buffer_ok;
}

// ============================================================================
// Section Layout
// ============================================================================

// Default section sizes (can be overridden by clients)
inline constexpr uint32_t DEFAULT_PRODUCER_SECTION_SIZE = 2 * CACHE_LINE;  // 128 bytes
inline constexpr uint32_t DEFAULT_CONSUMER_SECTION_SIZE = 2 * CACHE_LINE;  // 128 bytes

// Calculate offsets for default layout
inline constexpr uint32_t default_producer_offset() {
    return CACHE_LINE;  // Right after metadata
}

inline constexpr uint32_t default_consumer_0_offset(uint32_t producer_section_size = DEFAULT_PRODUCER_SECTION_SIZE) {
    return CACHE_LINE + producer_section_size;
}

// Calculate raw header size (before page alignment)
inline constexpr uint32_t raw_header_size(
    uint8_t max_consumers,
    uint32_t producer_section_size = DEFAULT_PRODUCER_SECTION_SIZE,
    uint32_t consumer_section_size = DEFAULT_CONSUMER_SECTION_SIZE
) {
    return CACHE_LINE + producer_section_size + (max_consumers * consumer_section_size);
}

// Calculate header segment size (page-aligned)
inline uint32_t header_segment_size(
    uint8_t max_consumers,
    uint32_t producer_section_size = DEFAULT_PRODUCER_SECTION_SIZE,
    uint32_t consumer_section_size = DEFAULT_CONSUMER_SECTION_SIZE
) {
    uint32_t raw = raw_header_size(max_consumers, producer_section_size, consumer_section_size);
    return ((raw + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
}

// ============================================================================
// Metadata Operations
// ============================================================================

// Initialize metadata at given memory location
// buffer_size must be power of 2
inline void metadata_init(
    void* ptr,
    uint8_t max_consumers,
    uint16_t event_size,          // Event size (0 for variable-size ringbuffer)
    uint32_t buffer_size,         // Buffer size (must be power of 2)
    uint32_t producer_offset,
    uint32_t consumer_0_offset,
    uint32_t header_size
) {
    auto* meta = static_cast<metadata*>(ptr);
    meta->magic = METADATA_MAGIC;
    meta->version = METADATA_VERSION;
    meta->max_consumers = max_consumers;
    meta->event_size = event_size;
    meta->producer_pid = 0;  // 0 = no producer attached yet
    meta->buffer_size = buffer_size;
    meta->producer_offset = producer_offset;
    meta->consumer_0_offset = consumer_0_offset;
    meta->header_size = header_size;
    meta->index_mask = buffer_size - 1;
    meta->event_size_log2 = event_size ? size_to_log2(event_size) : 0;
    meta->buffer_size_log2 = size_to_log2(buffer_size);
    meta->header_size_log2 = size_to_log2(header_size);
    std::fill(std::begin(meta->padding), std::end(meta->padding), 0);
}

// Validate metadata magic and version
inline bool metadata_validate(const void* ptr) {
    const auto* meta = static_cast<const metadata*>(ptr);
    return meta->magic == METADATA_MAGIC && meta->version == METADATA_VERSION;
}

// Get const pointer to metadata
inline const metadata* metadata_get(const void* ptr) {
    return static_cast<const metadata*>(ptr);
}

// Helper: get producer section size
inline uint32_t producer_section_size(const metadata* meta) {
    return meta->consumer_0_offset - meta->producer_offset;
}

// Helper: get consumer section size
inline uint32_t consumer_section_size(const metadata* meta) {
    uint32_t raw_end = raw_header_size(meta->max_consumers);
    return (raw_end - meta->consumer_0_offset) / meta->max_consumers;
}

// Helper: get consumer N offset
inline uint32_t consumer_offset(const metadata* meta, uint8_t n) {
    return meta->consumer_0_offset + n * consumer_section_size(meta);
}

// ============================================================================
// Data Segment Size Calculation
// ============================================================================

// Hugepage size constants
inline constexpr std::size_t HUGEPAGE_2MB = 2ULL * 1024 * 1024;
inline constexpr std::size_t HUGEPAGE_1GB = 1024ULL * 1024 * 1024;

// Calculate data segment size (hugepage-aligned when using hugepages)
inline uint32_t data_segment_size(
    uint32_t buffer_size,
    uint32_t hugepage_size  // 0 for regular pages, HUGEPAGE_2MB, or HUGEPAGE_1GB
) {
    if (hugepage_size == 0) {
        return buffer_size;  // Already power of 2
    }
    return ((buffer_size + hugepage_size - 1) / hugepage_size) * hugepage_size;
}

} // namespace hftshm
