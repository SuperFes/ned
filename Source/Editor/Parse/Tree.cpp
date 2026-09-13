#include "Editor/Parse/Tree.h"

#include "Editor/Parse/Node.h"

namespace ned::editor::parse {

TreeData::~TreeData() {
    if (root.ptr != nullptr) {
        SubtreePool pool = SubtreePool::New(0);
        SubtreeRelease(&pool, root);
        pool.Delete();
    }
}

GreenTree::GreenTree(Subtree root, const abi::LanguageData* language) {
    if (root.ptr != nullptr)
        data_ = std::make_shared<const TreeData>(root, language);
}

RedNode GreenTree::RootNode() const {
    if (IsNull())
        return NodeNull();
    // Upstream positions the root node at the root subtree's own padding.
    return NodeNew(data_.get(), &data_->root, SubtreePadding(data_->root), 0);
}

GreenTree GreenTree::WithEdit(const InputEdit& edit) const {
    if (IsNull())
        return {};
    SubtreeRetain(data_->root);
    SubtreePool   pool   = SubtreePool::New(0);
    const Subtree edited = SubtreeEdit(data_->root, edit, &pool);
    pool.Delete();
    return {edited, data_->language};
}

} // namespace ned::editor::parse
