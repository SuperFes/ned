//
// change-signature follow-up (ROADMAP.md's Refactoring section, "the hard
// one, scoped honestly"): turning a function's OLD parameter list and the
// NEW one the user retyped into a position mapping, and turning that mapping
// plus one call site's own argument list into the new argument text for
// that call.
//
// Pure and buffer-free, the same split RenameReview.h/LocalScopes.h already
// take: everything here works over plain text plus the byte ranges
// Mode::signatures/Mode::calls already produced (SignatureParameter,
// CallArgument -- Editor/Mode.h), so it is unit-testable with no Buffer, no
// Parser and no Screen. Project-wide discovery (DiscoverSignatureAndCallSites
// below) keeps the same shape via caller-supplied hooks (SourceLookup,
// FileScanner) rather than touching a filesystem or a Mode itself. The I/O
// half that actually resolves those hooks -- point resolution, the real
// SearchDirectory candidate scan, ModeForPath, building the review -- is
// Source/UI/BufferView/ChangeSignature.cpp's job, not this file's.
//
// Matching is by PARAMETER NAME alone, never position: a name in both the
// old and new list is `Kept` (its call-site argument is copied verbatim from
// wherever it sits in the OLD list, regardless of where it now sits in the
// new one -- that's what makes a pure reorder a no-op on every call site's
// own values); a name only in the new list is `New` and must carry a
// default expression (typed as `Type name = expr` -- there is no other
// source for a value an existing call site never wrote); a name only in the
// old list is simply absent from the mapping, which is what drops it.
//
// Two things this module declines outright (the whole operation, not one
// call site) rather than guess at, per the "never guess" rule every review
// feature in this codebase already follows (ImportFixup.h's FixupPlan::
// declined, LocalScopes.h's usedBeforeDefinition): a variadic parameter on
// either side (nothing sensible to reorder/drop/default about `...`), a new
// parameter with no default, and an old or new parameter name that isn't
// unique enough to match unambiguously. RewriteArgumentList additionally
// declines per CALL SITE -- never the whole operation -- when that one call
// supplied fewer arguments than the old signature had parameters (an
// omitted trailing default this module has no text for).
//

#ifndef NED_EDITOR_CHANGESIGNATURE_H
#define NED_EDITOR_CHANGESIGNATURE_H

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Mode.h"

namespace ned::editor::changesig {

enum class ParamOriginKind { Kept,
                             New };

// Where one NEW parameter's call-site value comes from. `Kept::oldIndex` is
// an index into the OLD parameter/argument list a caller passes back into
// RewriteArgumentList; `New`'s default expression is a byte range into the
// SAME newText BuildPositionMapping was given (the text the user retyped),
// not a copied string -- RewriteArgumentList slices it from there, same
// "text plus ranges, not pre-sliced strings" shape every other pure module
// here follows.
struct ParamOrigin {
    ParamOriginKind kind;
    std::size_t     oldIndex            = 0;
    std::size_t     newDefaultStartByte = 0;
    std::size_t     newDefaultEndByte   = 0;
};

struct MappingResult {
    bool                      declined = false;
    std::string               declineReason; // set only when declined
    std::vector<ParamOrigin> origins;        // one per NEW parameter, in order; valid only when !declined
};

// oldText/newText are whatever text oldParams/newParams's own byte ranges
// index into -- typically the function's real source text, and the
// synthetic `void __ned_sig(<retyped text>);` wrapper respectively (see
// Source/UI/BufferView/ChangeSignature.cpp), though nothing here depends on
// that -- both are treated as opaque text to slice names and defaults out
// of.
[[nodiscard]] MappingResult BuildPositionMapping(std::string_view oldText, const std::vector<SignatureParameter>& oldParams,
                                                 std::string_view newText, const std::vector<SignatureParameter>& newParams);

struct ArgumentRewrite {
    bool        declined = false;
    std::string declineReason; // set only when declined
    std::string argumentListText; // the new, comma-joined argument text; valid only when !declined
};

// callText/oldArgs are one call site's own text and its argument byte
// ranges (Mode::calls' CallMarker::arguments, in call order); newDefaultText
// is the SAME newText BuildPositionMapping was given, since a `New` origin's
// default range points into it. `origins` is BuildPositionMapping's own
// result for this function -- every call site sharing one signature change
// reuses the same origins list.
[[nodiscard]] ArgumentRewrite RewriteArgumentList(std::string_view callText, const std::vector<CallArgument>& oldArgs,
                                                  std::string_view newDefaultText, const std::vector<ParamOrigin>& origins);

// Project-wide discovery follow-up: finding every OTHER place -- a call
// site, or the function's own signature in a different file (a header
// prototype, an out-of-line definition, ...) -- that has to change too.
// Same "text plus ranges, not pre-sliced strings" shape as everything
// above, and the same TextReader-hook split
// Editor/ImportFixup.h::PlanImportFixups already established for exactly
// this reason: a caller hands in already-read candidate text (an open
// buffer's live content in place of its file, same rule every project-wide
// operation in this codebase follows) rather than this module touching a
// filesystem or a BufferList itself, which is what keeps it unit-testable
// with canned data and no real Mode/grammar/Buffer involved.
//
// Ned has no type checker, so it cannot tell a real overload (`void f(int)`
// alongside `void f(int, int)`) from this function's own prototype/
// definition pair (which always share the same parameter COUNT, since
// that's what makes them valid C++ in the first place) -- arity is the one
// thing it CAN check safely. A same-name signature found with a DIFFERENT
// arity than the one being changed is therefore never treated as another
// place to rewrite; it is counted in `arityMismatches`, and a caller
// declines the WHOLE operation outright whenever that count is nonzero,
// same "never guess" posture as BuildPositionMapping's own variadic check.
// A same-name, SAME-arity signature -- found in the invocation's own file,
// a header prototype, an out-of-line definition, wherever the search
// reaches -- is added to `signatureSites` unconditionally; a real same-arity
// overload slipping through this check and getting rewritten alongside the
// intended function is the one residual risk this doesn't close, same
// caveat the "Overload gate" already carries.

// A call site found during discovery: which file, that file's own text
// (what `call`'s own byte ranges index into), and the call itself.
struct CallSite {
    std::filesystem::path file;
    std::string            text;
    CallMarker              call;
};

// A same-name, same-arity signature found during discovery -- a candidate
// for the parameter-list rewrite itself, not a call site.
struct SignatureSite {
    std::filesystem::path file;
    std::string            text;
    SignatureMarker         signature;
};

// One candidate file's own Mode::signatures/Mode::calls output, already run
// by the caller (BufferView resolves the real Mode via ModeForPath; this
// module never does). A candidate with neither query configured for its
// language contributes two empty vectors, same as an ordinary Mode with
// nothing to say.
struct FileScanResult {
    std::vector<SignatureMarker> signatures;
    std::vector<CallMarker>      calls;
};

// The caller's hook for reading a candidate's current text -- an open
// buffer's live content in place of its on-disk copy, same TextReader shape
// ImportFixup.h uses. nullopt means "skip this candidate": unreadable,
// binary, or too big to hold.
using SourceLookup = std::function<std::optional<std::string>(const std::filesystem::path&)>;

// The caller's hook for running the two capabilities over one candidate's
// text.
using FileScanner = std::function<FileScanResult(const std::filesystem::path&, std::string_view text)>;

struct DiscoveryResult {
    std::vector<SignatureSite> signatureSites;
    std::vector<CallSite>      callSites;
    // Same-name signatures found with a DIFFERENT parameter count than
    // targetArity -- see this section's own header comment. Nonzero means
    // "decline the whole operation," never "rewrite anyway."
    std::size_t arityMismatches = 0;
    // Candidates readText declined to read (see SourceLookup) -- reported
    // the same "say what was covered" way MultibufferMaxExcerpts's own note
    // and ImportFixup.h's FixupPlan::scanned are.
    std::size_t filesSkipped = 0;
};

[[nodiscard]] DiscoveryResult DiscoverSignatureAndCallSites(std::string_view name, std::size_t targetArity,
                                                            const std::vector<std::filesystem::path>& candidates,
                                                            const SourceLookup& readText, const FileScanner& scanner);

// An RE2 alternation matching every whole-word spelling of `name` -- for
// feeding Editor/Project/Search.h's own threaded, .gitignore-aware,
// live-buffer-first scan, the same "which files might mention this?"
// candidate pass ImportFixup.h::CandidatePattern already runs (word-bounded
// for the same reason: everything this matches gets a real parse, which is
// the expensive half). A single name rather than a set, since a signature
// change only ever targets one function.
[[nodiscard]] std::string CandidatePattern(std::string_view name);

} // namespace ned::editor::changesig

#endif // NED_EDITOR_CHANGESIGNATURE_H
