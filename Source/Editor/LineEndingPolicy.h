//
// Process-wide, Janet-configurable policy governing what line ending
// Buffer::SaveToFile writes for a buffer that has no per-buffer override
// (see Buffer::SetLineEndingOverride) -- mirrors FinalNewline.h's
// mutex-guarded-static-bool shape exactly, just over an enum. Default is
// Preserve: every buffer keeps whatever ending Buffer::FromFile detected
// (see Text/LineEnding.h), matching most editors' own default and doing
// nothing surprising to a Windows-authored file just because it was opened
// here. Force(LF)/Force(CRLF)/Force(CR) instead re-encode every save
// regardless of what was detected -- the "just get rid of the CRs" case a
// team standardized on LF wants. Configured from Janet via
// ned/set-line-ending-policy.
//

#ifndef NED_EDITOR_LINEENDINGPOLICY_H
#define NED_EDITOR_LINEENDINGPOLICY_H

#include <string>

#include "../Text/LineEnding.h"

namespace ned::text {
class Buffer;
}

namespace ned::editor {

enum class LineEndingPolicyMode {
    Preserve, // use the buffer's own detected/overridden ending
    Force,    // always re-encode to forcedEnding, regardless of detection
};

struct LineEndingPolicy {
    LineEndingPolicyMode  mode         = LineEndingPolicyMode::Preserve;
    ned::text::LineEnding forcedEnding = ned::text::LineEnding::LF; // only meaningful when mode == Force
};

void                           SetLineEndingPolicy(LineEndingPolicy policy);
[[nodiscard]] LineEndingPolicy GetLineEndingPolicy();

// ned/set-line-ending-policy's own binding: "preserve" (the default) /
// "lf" / "crlf" / "cr" -- an unrecognized value leaves the current policy
// unchanged, matching PanelConfig.cpp's SetAcpPanelDock precedent.
void SetLineEndingPolicyFromString(const std::string& value);

// Resolves what a buffer whose detected/overridden ending is
// bufferEnding should actually be saved as, under the current policy --
// the one call site Buffer::SaveToFile needs.
[[nodiscard]] ned::text::LineEnding ResolveLineEndingForSave(ned::text::LineEnding bufferEnding);

// The buffer's own convention (Buffer::Conventions) when it states one --
// a file's project outranks the process-wide policy -- else the policy
// applied to the buffer's tracked ending.
[[nodiscard]] ned::text::LineEnding ResolveLineEndingForSave(const ned::text::Buffer& buffer);

} // namespace ned::editor

#endif // NED_EDITOR_LINEENDINGPOLICY_H
