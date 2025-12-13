#pragma once

#include <string>
#include <string_view>
#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <chrono>
#include <iomanip>
#include <sstream>

#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <cstring>

#include "types.hpp"
#include "layout.hpp"

namespace hftshm::policies {

// Error for platform operations
struct PlatformError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#if defined(__linux__)

// Linux shared memory policy
// Uses /dev/shm/hft/<name> with MAP_HUGETLB support
struct LinuxShmPolicy {
    static constexpr std::string_view BASE_PATH = "/dev/shm/hft";

    static auto ensure_base_dir() -> void {
        std::filesystem::create_directories(BASE_PATH);
    }

    auto get_path(std::string_view name) const -> std::string {
        return std::string(BASE_PATH) + "/" + std::string(name);
    }

    auto get_header_path(std::string_view name) const -> std::string {
        return get_path(name) + ".hdr";
    }

    auto get_data_path(std::string_view name) const -> std::string {
        return get_path(name) + ".dat";
    }

    auto create(std::string_view name, std::size_t size, std::size_t hugepage_size) const -> int {
        ensure_base_dir();
        auto path = get_path(name);

        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            if (errno == EEXIST) {
                fd = ::open(path.c_str(), O_RDWR);
                if (fd < 0) return -1;
                if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
                    ::close(fd);
                    return -1;
                }
                return fd;
            }
            return -1;
        }

        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            ::close(fd);
            ::unlink(path.c_str());
            return -1;
        }
        return fd;
    }

    auto map(int fd, std::size_t size, std::size_t hugepage_size) const -> void* {
        int flags = MAP_SHARED;

        if (hugepage_size > 0) {
#ifdef MAP_HUGETLB
            flags |= MAP_HUGETLB;
#ifdef MAP_HUGE_2MB
            if (hugepage_size == HUGEPAGE_2MB) flags |= MAP_HUGE_2MB;
#endif
#ifdef MAP_HUGE_1GB
            if (hugepage_size == HUGEPAGE_1GB) flags |= MAP_HUGE_1GB;
#endif
#endif
        }

        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, fd, 0);

        if (ptr == MAP_FAILED && hugepage_size > 0) {
            // Fallback to regular pages
            ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        }

        return (ptr == MAP_FAILED) ? nullptr : ptr;
    }

    auto open(std::string_view name) const -> int {
        return ::open(get_path(name).c_str(), O_RDWR);
    }

    auto get_size(int fd) const -> std::size_t {
        struct stat st;
        return (::fstat(fd, &st) == 0) ? static_cast<std::size_t>(st.st_size) : 0;
    }

    auto unlink(std::string_view name) const -> bool {
        return ::unlink(get_path(name).c_str()) == 0;
    }

    auto unmap(void* ptr, std::size_t size) const -> void {
        if (ptr && ptr != MAP_FAILED) ::munmap(ptr, size);
    }

    auto close_fd(int fd) const -> void {
        if (fd >= 0) ::close(fd);
    }

    auto get_info(std::string_view name) const -> SegmentInfo {
        SegmentInfo info;
        info.path = get_path(name);
        info.exists = std::filesystem::exists(info.path);

        if (!info.exists) {
            info.size = 0;
            info.permissions = "";
            info.hugepage_size = 0;
            info.last_modified = "";
            return info;
        }

        struct stat st;
        if (::stat(info.path.c_str(), &st) == 0) {
            info.size = static_cast<std::size_t>(st.st_size);

            std::string perms;
            perms += (st.st_mode & S_IRUSR) ? 'r' : '-';
            perms += (st.st_mode & S_IWUSR) ? 'w' : '-';
            perms += (st.st_mode & S_IXUSR) ? 'x' : '-';
            perms += (st.st_mode & S_IRGRP) ? 'r' : '-';
            perms += (st.st_mode & S_IWGRP) ? 'w' : '-';
            perms += (st.st_mode & S_IXGRP) ? 'x' : '-';
            perms += (st.st_mode & S_IROTH) ? 'r' : '-';
            perms += (st.st_mode & S_IWOTH) ? 'w' : '-';
            perms += (st.st_mode & S_IXOTH) ? 'x' : '-';
            info.permissions = perms;

            auto mtime = std::chrono::system_clock::from_time_t(st.st_mtime);
            auto time_t_val = std::chrono::system_clock::to_time_t(mtime);
            std::tm* tm = std::localtime(&time_t_val);
            std::ostringstream oss;
            oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
            info.last_modified = oss.str();
        }

        info.hugepage_size = 0;
        return info;
    }
};

#elif defined(__APPLE__)

// macOS shared memory policy
// Uses /tmp/hft/<name> (file-backed mmap, no hugepage support)
struct MacOSShmPolicy {
    static constexpr std::string_view BASE_PATH = "/tmp/hft";

    static auto ensure_base_dir() -> void {
        std::filesystem::create_directories(BASE_PATH);
    }

    auto get_path(std::string_view name) const -> std::string {
        return std::string(BASE_PATH) + "/" + std::string(name);
    }

    auto get_header_path(std::string_view name) const -> std::string {
        return get_path(name) + ".hdr";
    }

    auto get_data_path(std::string_view name) const -> std::string {
        return get_path(name) + ".dat";
    }

    auto create(std::string_view name, std::size_t size, std::size_t /*hugepage_size*/) const -> int {
        ensure_base_dir();
        auto path = get_path(name);

        int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            if (errno == EEXIST) {
                fd = ::open(path.c_str(), O_RDWR);
                if (fd < 0) return -1;
                if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
                    ::close(fd);
                    return -1;
                }
                return fd;
            }
            return -1;
        }

        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            ::close(fd);
            ::unlink(path.c_str());
            return -1;
        }
        return fd;
    }

    auto map(int fd, std::size_t size, std::size_t /*hugepage_size*/) const -> void* {
        void* ptr = ::mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        return (ptr == MAP_FAILED) ? nullptr : ptr;
    }

    auto open(std::string_view name) const -> int {
        return ::open(get_path(name).c_str(), O_RDWR);
    }

    auto get_size(int fd) const -> std::size_t {
        struct stat st;
        return (::fstat(fd, &st) == 0) ? static_cast<std::size_t>(st.st_size) : 0;
    }

    auto unlink(std::string_view name) const -> bool {
        return ::unlink(get_path(name).c_str()) == 0;
    }

    auto unmap(void* ptr, std::size_t size) const -> void {
        if (ptr && ptr != MAP_FAILED) ::munmap(ptr, size);
    }

    auto close_fd(int fd) const -> void {
        if (fd >= 0) ::close(fd);
    }

    auto get_info(std::string_view name) const -> SegmentInfo {
        SegmentInfo info;
        info.path = get_path(name);
        info.exists = std::filesystem::exists(info.path);

        if (!info.exists) {
            info.size = 0;
            info.permissions = "";
            info.hugepage_size = 0;
            info.last_modified = "";
            return info;
        }

        struct stat st;
        if (::stat(info.path.c_str(), &st) == 0) {
            info.size = static_cast<std::size_t>(st.st_size);

            std::string perms;
            perms += (st.st_mode & S_IRUSR) ? 'r' : '-';
            perms += (st.st_mode & S_IWUSR) ? 'w' : '-';
            perms += (st.st_mode & S_IXUSR) ? 'x' : '-';
            perms += (st.st_mode & S_IRGRP) ? 'r' : '-';
            perms += (st.st_mode & S_IWGRP) ? 'w' : '-';
            perms += (st.st_mode & S_IXGRP) ? 'x' : '-';
            perms += (st.st_mode & S_IROTH) ? 'r' : '-';
            perms += (st.st_mode & S_IWOTH) ? 'w' : '-';
            perms += (st.st_mode & S_IXOTH) ? 'x' : '-';
            info.permissions = perms;

            auto mtime = std::chrono::system_clock::from_time_t(st.st_mtime);
            auto time_t_val = std::chrono::system_clock::to_time_t(mtime);
            std::tm* tm = std::localtime(&time_t_val);
            std::ostringstream oss;
            oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
            info.last_modified = oss.str();
        }

        info.hugepage_size = 0;
        return info;
    }
};

#endif

// Default platform policy based on OS
#if defined(__linux__)
using DefaultPlatformPolicy = LinuxShmPolicy;
#elif defined(__APPLE__)
using DefaultPlatformPolicy = MacOSShmPolicy;
#else
#error "Unsupported platform"
#endif

} // namespace hftshm::policies
