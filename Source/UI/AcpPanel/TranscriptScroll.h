//
// Scroll position over AcpPanel's wrapped transcript rows. Follows the tail
// until the user scrolls away; from then on the position is anchored to the
// TOP row, so rows streaming in below never move what is being read.
// Reaching the bottom again resumes following.
//

#ifndef NED_UI_ACPPANEL_TRANSCRIPTSCROLL_H
#define NED_UI_ACPPANEL_TRANSCRIPTSCROLL_H

namespace ned::ui::acppanel {

class TranscriptScroll {
  public:
    // The first visible row for a frame with `totalRows` rows and a
    // `viewportRows`-tall viewport. Clamps the stored position against a
    // transcript that shrank (a rewind), resuming follow if it now fits.
    [[nodiscard]] int FirstVisibleRow(int totalRows, int viewportRows);

    // Negative scrolls toward older rows.
    void ScrollBy(int deltaRows, int totalRows, int viewportRows);
    // Puts `row` at the top of the viewport, as far as the tail allows.
    void ScrollToRow(int row, int totalRows, int viewportRows);
    void ScrollToTop();
    void FollowTail();

    [[nodiscard]] bool Following() const {
        return following_;
    }

  private:
    bool following_ = true;
    int  top_       = 0;
};

} // namespace ned::ui::acppanel

#endif // NED_UI_ACPPANEL_TRANSCRIPTSCROLL_H
