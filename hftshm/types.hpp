#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hftshm {

// Runtime segment info (from filesystem inspection)
struct SegmentInfo {
    std::string path;
    bool exists;
    std::size_t size;
    std::string permissions;
    std::size_t hugepage_size;
    std::string last_modified;
};

// Active shared memory segment handle
struct SegmentHandle {
    int fd;                     // File descriptor
    void* ptr;                  // Mapped memory pointer
    std::size_t size;           // Mapped size
    std::string path;           // Filesystem path

    SegmentHandle() : fd(-1), ptr(nullptr), size(0) {}

    auto is_valid() const -> bool {
        return fd >= 0 && ptr != nullptr && size > 0;
    }
};

} // namespace hftshm
