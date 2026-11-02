// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace offline::asop_selection {
using CommittedSelectionNotice = std::shared_ptr<const std::string>;
enum class NoticeApplyResult { None, Applied, Deferred };

// One pending notification; a newer committed file supersedes an older one.
// Consumers acknowledge the exact snapshot only after its runtime apply succeeds.
class CommittedSelectionQueue {
public:
    void Publish(CommittedSelectionNotice notice) {
        std::lock_guard lock(mutex_);
        pending_ = std::move(notice);
    }
    bool Peek(CommittedSelectionNotice& out) const {
        std::lock_guard lock(mutex_);
        out = pending_;
        return static_cast<bool>(out);
    }
    void Acknowledge(const CommittedSelectionNotice& notice) {
        std::lock_guard lock(mutex_);
        if (pending_ == notice) pending_.reset();
    }
    template<class Apply>
    NoticeApplyResult ApplyNext(Apply&& apply) {
        CommittedSelectionNotice notice;
        if (!Peek(notice)) return NoticeApplyResult::None;
        if (!std::forward<Apply>(apply)(*notice)) return NoticeApplyResult::Deferred;
        Acknowledge(notice);
        return NoticeApplyResult::Applied;
    }
private:
    mutable std::mutex mutex_;
    CommittedSelectionNotice pending_;
};
inline CommittedSelectionQueue g_committedSelection;
}
