//
// Capture classifiers: the escape hatch for a SyntaxClass that is a
// *function of the captured text* rather than of the capture name -- Org's
// headline level is arithmetic over counted stars, and its TODO-vs-DONE
// resolution compares against a runtime-configured keyword list; no static
// query predicate can express either. A classifier is registered per
// (language key, capture name) and consulted by the generic highlight
// closure ahead of the name-based resolution (ModeInternal.h's
// SyntaxClassForCapture).
//
// The interface is deliberately BATCH -- one call per registered capture
// name per highlight run, over every one of that name's captured texts --
// because the expected implementation is Janet through the
// janet_def/dostring pattern (EditorBindings.cpp), which costs a small
// compile per invocation: per-capture calls would put that compile inside
// the per-keystroke reparse loop, batch puts it at one or two per repaint.
// The Janet-facing binding (ned/register-capture-classifier) keeps the
// user's function per-text and maps it over the batch internally.
//
// A Janet-backed classifier is main-thread-only (Janet itself is); its
// wrapper answers Fallthrough for every entry when called from another
// thread -- ModePrewarm's background highlight pass, whose spans are
// discarded anyway (only the parse tree it warms is kept).
//

#ifndef NED_EDITOR_CAPTURECLASSIFIERS_H
#define NED_EDITOR_CAPTURECLASSIFIERS_H

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Mode.h"

namespace ned::editor {

// One capture's classification: the class to use, "no span at all"
// (Suppress -- Org's keyword-candidate capture on a word that is not a
// configured keyword must contribute nothing, not a Default-classed span
// that would clobber the headline wash under the later-wins render rule),
// or fall through to the name-based resolution.
struct CaptureClassification {
    enum class Kind {
        Fallthrough,
        Suppress,
        Classified,
    };
    Kind        kind = Kind::Fallthrough;
    SyntaxClass cls  = SyntaxClass::Default;

    static CaptureClassification Class(SyntaxClass cls) {
        return {.kind = Kind::Classified, .cls = cls};
    }
    static CaptureClassification Suppressed() {
        return {.kind = Kind::Suppress};
    }
};

// texts[i] is the i-th captured node's own text; the result must be
// texts.size() entries (a wrong-sized result is treated as all-Fallthrough).
using CaptureClassifier = std::function<std::vector<CaptureClassification>(std::span<const std::string_view> texts)>;

// Re-registering the same (language, capture) replaces; an empty function
// clears. Mutex-guarded process-wide state, the settings-module pattern.
void RegisterCaptureClassifier(const std::string& languageKey, const std::string& captureName,
                               CaptureClassifier classifier);

[[nodiscard]] CaptureClassifier FindCaptureClassifier(std::string_view languageKey, std::string_view captureName);

// True when the language has any classifier at all -- the highlight closure
// checks this once per run before grouping captures by name.
[[nodiscard]] bool HasCaptureClassifiers(std::string_view languageKey);

// Test hygiene: registration is process-wide state.
void ClearCaptureClassifiers();

} // namespace ned::editor

#endif // NED_EDITOR_CAPTURECLASSIFIERS_H
