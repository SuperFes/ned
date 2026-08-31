#include "MappedFile.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <utility>

namespace ned::text {

MappedFile MappedFile::Open(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        throw MappedFileError("ned: failed to open for mapping: " + path.string());
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        throw MappedFileError("ned: failed to stat for mapping: " + path.string());
    }

    MappedFile result;

    if (st.st_size == 0) {
        // mmap rejects a zero-length mapping outright -- represent an empty
        // file as a valid MappedFile with no backing mapping at all rather
        // than special-casing this in every caller.
        ::close(fd);
        return result;
    }

    void* mapped = ::mmap(nullptr, static_cast<std::size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    // The fd is only needed to create the mapping -- once mmap succeeds the
    // mapping stays valid independent of the fd, same POSIX guarantee every
    // mmap-based reader relies on.
    ::close(fd);

    if (mapped == MAP_FAILED) {
        throw MappedFileError("ned: failed to map: " + path.string());
    }

    result.data_ = static_cast<const char*>(mapped);
    result.size_ = static_cast<std::size_t>(st.st_size);
    return result;
}

MappedFile::~MappedFile() {
    if (data_ != nullptr) {
        ::munmap(const_cast<char*>(data_), size_);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept : data_(other.data_), size_(other.size_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (data_ != nullptr) {
            ::munmap(const_cast<char*>(data_), size_);
        }
        data_       = other.data_;
        size_       = other.size_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

const char* MappedFile::Data() const {
    return data_;
}

std::size_t MappedFile::Size() const {
    return size_;
}

void MappedFile::Advise(AccessPattern pattern) const {
    if (data_ == nullptr) {
        return;
    }

    int advice = MADV_NORMAL;
    switch (pattern) {
        case AccessPattern::kNormal:
            advice = MADV_NORMAL;
            break;
        case AccessPattern::kRandom:
            advice = MADV_RANDOM;
            break;
        case AccessPattern::kSequential:
            advice = MADV_SEQUENTIAL;
            break;
    }

    // Best-effort -- a failed madvise leaves the mapping exactly as usable
    // as it was, just without the hint, so the return value is deliberately
    // ignored rather than surfaced as an error.
    ::madvise(const_cast<char*>(data_), size_, advice);
}

void MappedFile::ReleasePages(std::size_t offset, std::size_t length) const {
    if (data_ == nullptr || offset >= size_) {
        return;
    }

    length = std::min(length, size_ - offset);
    if (length == 0) {
        return;
    }

    // madvise wants a page-aligned range on every platform that matters
    // here (Linux tolerates an unaligned one via internal rounding, macOS
    // does not) -- round outward rather than relying on that, so a caller
    // never has to think about page size itself.
    const std::size_t pageSize = static_cast<std::size_t>(::sysconf(_SC_PAGESIZE));
    const std::size_t end      = offset + length;
    const std::size_t alignedStart = offset - (offset % pageSize);
    const std::size_t alignedEnd   = std::min(size_, end + (pageSize - (end % pageSize)) % pageSize);

    ::madvise(const_cast<char*>(data_) + alignedStart, alignedEnd - alignedStart, MADV_DONTNEED);
}

} // namespace ned::text
