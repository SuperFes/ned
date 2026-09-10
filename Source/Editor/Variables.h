//
// Editor-remembered variables -- small key/value facts the *editor* decides
// to persist across runs, in $XDG_STATE_HOME/ned/variables.json. Currently:
// sidebar width and visibility, which left-dock panel was showing, whether
// the minimap is on.
//
// State, not config, on purpose: $XDG_CONFIG_HOME (init.janet) is what the
// user writes, $XDG_STATE_HOME is what the editor writes -- the same line
// file-places.json/trusted.json already draw, and the reason this isn't a
// machine-written variables.janet: auto-evaluating generated code invites
// the Emacs custom-set-variables file-fighting problem, while JSON state
// stays inert and mergeable.
//
// The test for what belongs here is *how often you change it*, not whether
// the user could also have written it down. Everything above is a thing you
// flip in passing and expect to find where you left it: you drag the sidebar
// wider, you toggle the minimap, you switch panels. Remembering the last
// flip is the whole point, and it beating a ned/set-* default is right --
// the flip is newer, and flipping back costs one keystroke.
//
// The theme failed that test, which is why it used to be here and no longer
// is. You set a theme once and expect it to stay set, so a picker choice
// silently outranking init.janet's own ned/set-theme just made the config
// file look ignored, with nothing on screen to explain it and no cheap way
// to undo it. It is config now (Docs/Themes.md), and the picker offers to
// write the line rather than remembering it here.
//
// So: a live toggle belongs here. A thing you decide once does not.
//
// VariableStore is the pure, unit-testable core (JSON round-trip); the
// process-wide accessors wrap one mutex-guarded static instance --
// Session.h's exact layering. SetVariable writes through to disk
// immediately (variables change at interactive-command frequency, not
// keystroke frequency, so there's nothing for a periodic save to batch),
// swallowing I/O failures the same way SaveFilePlaces does: convenience
// state, nothing to report a failure to.
//

#ifndef NED_EDITOR_VARIABLES_H
#define NED_EDITOR_VARIABLES_H

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace ned::editor {

class VariableStore {
  public:
    [[nodiscard]] std::optional<std::string> Get(const std::string& key) const;
    void                                     Set(const std::string& key, const std::string& value);

    // Missing file loads as an empty store; a malformed one is discarded
    // the same way (convenience state must never block startup). SaveToFile
    // writes via a sibling .ned-tmp + rename, mirroring
    // FilePlaceStore::SaveToFile exactly; throws std::runtime_error on I/O
    // failure.
    void LoadFromFile(const std::filesystem::path& path);
    void SaveToFile(const std::filesystem::path& path) const;

    [[nodiscard]] std::string ToJson() const;
    static VariableStore      FromJson(std::string_view json); // malformed -> empty store
    [[nodiscard]] std::size_t Count() const;

  private:
    std::map<std::string, std::string> values_;
};

// -- Process-wide store (mutex-guarded static state) --------------------------

// $XDG_STATE_HOME/ned/variables.json (falling back to
// ~/.local/state/ned/variables.json); throws std::runtime_error if neither
// XDG_STATE_HOME nor HOME is set. FilePlacesPath's exact resolution.
[[nodiscard]] std::filesystem::path VariablesPath();

// Loads the process-wide store from VariablesPath() once at startup,
// swallowing every failure (missing file, malformed JSON, no HOME).
void LoadVariables();

// Reads/writes the process-wide store; SetVariable also saves to
// VariablesPath() immediately (see the header comment), swallowing I/O
// failures.
[[nodiscard]] std::optional<std::string> Variable(const std::string& key);
void                                     SetVariable(const std::string& key, const std::string& value);

} // namespace ned::editor

#endif // NED_EDITOR_VARIABLES_H
