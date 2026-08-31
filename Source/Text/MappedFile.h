//
// RAII read-only mmap of a file -- the storage PieceTable.h maps a huge
// file's original content through, so opening it never copies the file
// into memory. First mmap use in this codebase (confirmed by grep before
// adding it); every other file-reading path in Text/ and Editor/ uses plain
// read()/ifstream, which is the right call for anything that fits
// comfortably in memory -- this type exists specifically because a
// multi-GB file doesn't.
//
// Memory-residency model: mapping a file doesn't load it -- only the pages
// actually touched get faulted into the process's resident set, and
// because this mapping is always PROT_READ/MAP_PRIVATE (never written),
// the kernel can drop any of those pages under memory pressure and
// transparently re-fault them later at no correctness cost. Steady-state
// interactive editing of a huge file therefore only ever keeps "whatever's
// near the viewport/recent edits" resident, not the whole file. The one
// deliberate exception is PieceTable::FromFile's one-time linear scan to
// seed line/codepoint counts, which does touch every byte once -- see
// Advise/ReleasePages below, which is exactly the lever that scan uses to
// hand those pages back afterward instead of letting a single open leave
// a multi-GB file fully resident.
//

#ifndef NED_TEXT_MAPPEDFILE_H
#define NED_TEXT_MAPPEDFILE_H

#include <cstddef>
#include <filesystem>
#include <stdexcept>

namespace ned::text {

// Thrown by MappedFile::Open on any failure (open/fstat/mmap) -- deliberately
// distinct from a plain std::runtime_error, same shape as BinaryFileError
// (BinaryDetect.h), so a caller can tell "this path can't be mapped" apart
// from other I/O failures if it ever wants to react differently. There is
// no silent fallback to a full read on mmap failure: for the huge-file path
// that would defeat the entire point (see PieceTable.h), so a failure here
// is meant to surface, not be worked around invisibly.
class MappedFileError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

enum class AccessPattern { kNormal, kRandom, kSequential };

// Move-only; the mapping (and the underlying fd, only needed transiently to
// create it) is released in the destructor. A zero-byte file maps to a
// valid MappedFile with Data() == nullptr, Size() == 0 -- POSIX mmap
// rejects a zero-length mapping outright, so this is handled as a special
// case rather than calling mmap at all.
//
// Not thread-safe to construct/destroy concurrently with reads of the same
// instance, same contract as every other Text/ type -- expected to live on
// the main thread for its whole lifetime, same as the Buffer/PieceTable
// that owns it.
class MappedFile {
  public:
    // Throws MappedFileError on any failure to open/stat/map path.
    static MappedFile Open(const std::filesystem::path& path);

    ~MappedFile();

    MappedFile(const MappedFile&)            = delete;
    MappedFile& operator=(const MappedFile&) = delete;
    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    [[nodiscard]] const char* Data() const;
    [[nodiscard]] std::size_t Size() const;

    // madvise hints/releases -- never required for correctness (a clean,
    // read-only file-backed mapping is always safely re-faultable from the
    // page cache, and the kernel can already evict it under memory pressure
    // with no help from us), purely a residency optimization. A no-op if
    // the underlying madvise call fails; there's nothing a caller could
    // usefully do about that failure anyway, so this never throws.
    //
    // ReleasePages exists for exactly one shape of caller: something that
    // necessarily has to touch a large, possibly whole-file range once (the
    // one-time newline/codepoint count PieceTable::FromFile does to seed
    // its tree, a future streaming save/search pass) and wants that touch
    // to not linger as resident memory afterward -- see PieceTable.h's own
    // note on the memory-residency model this is part of. Steady-state
    // interactive editing never needs this: BufferView only ever asks for
    // small ranges near the viewport/edit point to begin with.
    void Advise(AccessPattern pattern) const;
    void ReleasePages(std::size_t offset, std::size_t length) const;

  private:
    MappedFile() = default;

    const char* data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace ned::text

#endif // NED_TEXT_MAPPEDFILE_H
