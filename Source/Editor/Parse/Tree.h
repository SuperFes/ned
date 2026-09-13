#pragma once

#include <memory>

#include "Editor/Parse/Green.h"

// The tree handle — upstream TSTree's shape: a stable heap object owning the
// green root, shared by every GreenTree copy. Red nodes (Parse/Node.h) point
// at the TreeData and must not outlive the last GreenTree holding it, the
// same lifetime contract TSNode has with TSTree today.

namespace ned::editor::parse {

struct TreeData {
    Subtree                  root     = kNullSubtree;
    const abi::LanguageData* language = nullptr;

    TreeData() = default;
    TreeData(Subtree treeRoot, const abi::LanguageData* treeLanguage) : root(treeRoot), language(treeLanguage) {
    }
    TreeData(const TreeData&)            = delete;
    TreeData& operator=(const TreeData&) = delete;
    ~TreeData();
};

struct RedNode;

class GreenTree {
  public:
    GreenTree() = default;
    // Adopts the root (no retain).
    GreenTree(Subtree root, const abi::LanguageData* language);

    [[nodiscard]] bool IsNull() const {
        return data_ == nullptr || data_->root.ptr == nullptr;
    }
    [[nodiscard]] Subtree Root() const {
        return data_ != nullptr ? data_->root : kNullSubtree;
    }
    [[nodiscard]] const abi::LanguageData* Language() const {
        return data_ != nullptr ? data_->language : nullptr;
    }
    [[nodiscard]] const TreeData* Data() const {
        return data_.get();
    }
    [[nodiscard]] bool HasError() const {
        return !IsNull() && SubtreeErrorCost(Root()) > 0;
    }

    // The root as a red node (null node for a null tree).
    [[nodiscard]] RedNode RootNode() const;

    // A copy of this tree whose byte layout reflects `edit` (hasChanges
    // marked along the touched path); this tree itself is unaffected.
    [[nodiscard]] GreenTree WithEdit(const InputEdit& edit) const;

  private:
    std::shared_ptr<const TreeData> data_;
};

} // namespace ned::editor::parse
