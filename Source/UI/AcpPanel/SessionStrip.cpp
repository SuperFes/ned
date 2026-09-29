#include "SessionStrip.h"

#include <algorithm>

#include "Text/DisplayWidth.h"

namespace ned::ui::acppanel {

namespace {

    using editor::acp::Manager;

    constexpr int              kMaxLabelColumns = 24;
    constexpr std::string_view kClose           = " × ";
    constexpr std::string_view kNew             = " + ";
    constexpr std::string_view kMoreBefore      = "‹";
    constexpr std::string_view kMoreAfter       = "›";

    std::string TabText(std::size_t index, const Manager::SessionTab& tab, int labelColumns) {
        std::string text = " " + std::to_string(index + 1);
        if (tab.attention) {
            text += " ●";
        }
        else if (tab.busy) {
            text += " ⋯";
        }
        if (labelColumns > 0) {
            std::string label = tab.label.empty() ? std::string("new") : tab.label;
            if (text::StringColumns(label) > labelColumns) {
                label = label.substr(0, text::PrefixBytesForColumns(label, labelColumns - 1)) + "…";
            }
            text += " " + label;
        }
        return text + " ";
    }

} // namespace

std::vector<SessionStripItem> LayoutSessionStrip(const std::vector<Manager::SessionTab>& tabs, int width) {
    std::vector<SessionStripItem> items;
    if (tabs.empty() || width <= 0) {
        return items;
    }
    const std::size_t count        = tabs.size();
    const auto        found        = std::find_if(tabs.begin(), tabs.end(), [](const Manager::SessionTab& tab) { return tab.current; });
    const std::size_t current      = found == tabs.end() ? 0 : static_cast<std::size_t>(found - tabs.begin());
    const int         closeColumns = count > 1 ? text::StringColumns(kClose) : 0;
    const int         newColumns   = text::StringColumns(kNew);

    std::vector<std::string> texts(count);
    std::vector<int>         columns(count);
    auto                     spanColumns = [&](std::size_t first, std::size_t last) {
        int total = closeColumns + newColumns + (first > 0 ? 1 : 0) + (last + 1 < count ? 1 : 0);
        for (std::size_t i = first; i <= last; ++i) {
            total += columns[i];
        }
        return total;
    };
    for (int cap = kMaxLabelColumns; cap >= 0; --cap) {
        for (std::size_t i = 0; i < count; ++i) {
            texts[i]   = TabText(i, tabs[i], cap);
            columns[i] = text::StringColumns(texts[i]);
        }
        if (spanColumns(0, count - 1) <= width) {
            break;
        }
    }

    // Too many to show even unlabelled: scroll, keeping the current tab.
    std::size_t first = 0;
    std::size_t last  = count - 1;
    while (spanColumns(first, last) > width && first < current) {
        ++first;
    }
    while (spanColumns(first, last) > width && last > current) {
        --last;
    }

    int  x    = 0;
    auto push = [&items, &x](SessionStripItem item) {
        item.x       = x;
        item.columns = text::StringColumns(item.text);
        x += item.columns;
        items.push_back(std::move(item));
    };
    if (first > 0) {
        push({.kind = SessionStripItem::Kind::More, .key = tabs[first - 1].key, .text = std::string(kMoreBefore)});
    }
    for (std::size_t i = first; i <= last; ++i) {
        push({.kind      = SessionStripItem::Kind::Tab,
              .key       = tabs[i].key,
              .text      = texts[i],
              .current   = tabs[i].current,
              .live      = tabs[i].live,
              .attention = tabs[i].attention});
        if (tabs[i].current && closeColumns > 0) {
            push({.kind = SessionStripItem::Kind::Close, .key = tabs[i].key, .text = std::string(kClose), .current = true});
        }
    }
    if (last + 1 < count) {
        push({.kind = SessionStripItem::Kind::More, .key = tabs[last + 1].key, .text = std::string(kMoreAfter)});
    }
    push({.kind = SessionStripItem::Kind::New, .text = std::string(kNew)});
    return items;
}

const SessionStripItem* SessionStripItemAt(const std::vector<SessionStripItem>& items, int x) {
    for (const SessionStripItem& item : items) {
        if (x >= item.x && x < item.x + item.columns) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace ned::ui::acppanel
