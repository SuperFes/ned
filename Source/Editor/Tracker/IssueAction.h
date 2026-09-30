//
// What can be done to one issue from wherever it is picked: a tracker
// panel's row, an issue's buffer, a tracker-* command's prompt.
//

#ifndef NED_EDITOR_TRACKER_ISSUEACTION_H
#define NED_EDITOR_TRACKER_ISSUEACTION_H

namespace ned::editor::tracker {

enum class IssueAction {
    CreateBranch,
    Comment,
    Transition,
    Assign,
    ClockIn,
};

} // namespace ned::editor::tracker

#endif // NED_EDITOR_TRACKER_ISSUEACTION_H
