#include "HugeFileLoader.h"

#include "EventLoop.h"
#include "Editor/DiagnosticsLog.h"
#include "Text/LineEnding.h"
#include "Text/MappedFile.h"
#include "Text/PieceTable.h"

namespace ned::ui {

namespace {
    // Leaf granularity within one chunk-group is still PieceTable's own
    // kOriginalChunkSize (256 KiB, PieceTable.cpp) -- this is the size of
    // one background-thread build-and-post round, chosen to bound how many
    // EventLoop::Post/PieceTable::Concatenated calls a full load makes
    // (~1800 for a 14.7 GiB file) without making any single post's build
    // cost (the expensive part, done off the main thread) too large to
    // start posting real content quickly.
    constexpr std::size_t kChunkGroupBytes = 8 * 1024 * 1024;
} // namespace

HugeFileLoader::HugeFileLoader(text::Buffer& placeholder, text::BufferList& bufferList, std::filesystem::path path,
                               bool allowBinary, EventLoop& eventLoop) : bufferList_(bufferList), bufferName_(placeholder.Name()) {
    std::error_code sizeEc;
    if (const std::uintmax_t size = std::filesystem::file_size(path, sizeEc); !sizeEc) {
        progress_->totalBytes = size;
    }
    placeholder.SetLoadProgress(progress_);

    thread_ = std::jthread([this, path = std::move(path), allowBinary, &eventLoop](std::stop_token stopToken) {
        Run(stopToken, path, allowBinary, eventLoop);
    });
}

HugeFileLoader::~HugeFileLoader() {
    if (thread_.joinable()) {
        thread_.request_stop();
    }
}

bool HugeFileLoader::Done() const {
    return done_;
}

void HugeFileLoader::Run(std::stop_token stopToken, std::filesystem::path path, bool allowBinary, EventLoop& eventLoop) {
    std::shared_ptr<const text::MappedFile> mappedFile;
    try {
        mappedFile = std::make_shared<const text::MappedFile>(text::MappedFile::Open(path));
    }
    catch (const std::exception&) {
        // Mirrors AsyncFileLoader's own open-failure path -- the placeholder
        // is still empty at this point (a real edit landing in the
        // vanishingly small window between buffer creation and this mmap
        // open failing is the same accepted edge case AsyncFileLoader's own
        // ifstream-open failure already has).
        eventLoop.Post([this] {
            bufferList_.Close(bufferName_);
            done_ = true;
        });
        return;
    }

    // Sequential for the whole walk -- unlike PieceTable::FromFile's own
    // one-shot scan, this loader never switches to kRandom itself; ordinary
    // interactive editing after the load finishes only ever touches small
    // ranges, and by then this loader (and its hint) no longer exists.
    mappedFile->Advise(text::AccessPattern::kSequential);

    const std::size_t totalSize = mappedFile->Size();
    std::size_t        offset    = 0;

    while (offset < totalSize && !stopToken.stop_requested()) {
        const std::size_t groupLength = std::min(kChunkGroupBytes, totalSize - offset);

        // open-binary-anyway follow-up: skipped entirely when allowBinary is
        // set, exactly like Buffer::FromHugeFile's own already-shipped fix
        // -- a confirmed binary open has no line-ending semantics to
        // protect, and arbitrary binary content is essentially guaranteed
        // to contain a stray CR somewhere across a multi-GB file. Folded
        // into this same per-chunk-group scan rather than a separate pass
        // (unlike today's synchronous FromHugeFile, which pays a second
        // full-file walk for this).
        if (!allowBinary) {
            const std::string_view chunk(mappedFile->Data() + offset, groupLength);
            if (text::HasCarriageReturn(chunk)) {
                eventLoop.Post([this, path] {
                    editor::LogMessage(editor::LogCategory::General, editor::LogSeverity::Error,
                                       "ned: huge-file opening does not yet support CRLF/CR line endings (" + path.string() +
                                           ") -- \"" + bufferName_ +
                                           "\" is only partially loaded; save is disabled until this is resolved");
                    done_ = true;
                });
                return; // leaves IsLoading() stuck true -- Buffer::SaveToFile's guard keeps a truncated save impossible
            }
        }

        text::PieceTable fragment = text::PieceTable::FromFileRange(mappedFile, offset, groupLength);
        mappedFile->ReleasePages(offset, groupLength);
        progress_->bytesRead.fetch_add(groupLength, std::memory_order_relaxed);
        offset += groupLength;

        eventLoop.Post([this, fragment] {
            text::Buffer* buffer = bufferList_.Find(bufferName_);
            if (!buffer) {
                return; // closed mid-load -- safe no-op, same as AsyncFileLoader
            }

            if (!expectedByteLength_) {
                buffer->ReplaceContentForHugeLoad(fragment);
            } else if (buffer->Size() != *expectedByteLength_) {
                // See expectedByteLength_'s own doc comment in the header --
                // the load frontier was undone out from under this loader.
                editor::LogMessage(editor::LogCategory::General, editor::LogSeverity::Warning,
                                   "ned: \"" + buffer->Name() +
                                       "\" was undone past a loaded section -- background loading stopped, "
                                       "the rest of the file will not be loaded");
                thread_.request_stop();
                done_ = true;
                return;
            } else {
                buffer->AppendHugeLoadChunk(fragment);
            }
            expectedByteLength_ = buffer->Size();
        });
    }

    if (stopToken.stop_requested()) {
        return; // loader destroyed, buffer closed, undone-past-append stop, or app exiting -- nothing left to post
    }

    eventLoop.Post([this] {
        if (text::Buffer* buffer = bufferList_.Find(bufferName_)) {
            buffer->FinishHugeLoad();
        }
        done_ = true;
    });
}

} // namespace ned::ui
