// SPDX-License-Identifier: GPL-3.0-only
// Synthetic notice/retry model; no host, game, filesystem or Windows APIs.
#include "../src/asop_selection_notice.h"
#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

using namespace offline::asop_selection;
static int failures=0;
#define CHECK(x) do { if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);++failures;} } while(0)

int main() {
    CommittedSelectionQueue queue;
    auto saved=std::make_shared<const std::string>("Class_A\n");
    queue.Publish(saved);

    CommittedSelectionNotice snapshot;
    CHECK(queue.Peek(snapshot) && snapshot==saved && *snapshot=="Class_A\n");
    const auto deferred=queue.ApplyNext([](const std::string&) { return false; });
    CHECK(deferred==NoticeApplyResult::Deferred);
    CommittedSelectionNotice retry;
    CHECK(queue.Peek(retry) && retry==saved); // failed apply is retained for retry

    const auto later=queue.ApplyNext([](const std::string& bytes) { return bytes=="Class_A\n"; });
    CHECK(later==NoticeApplyResult::Applied);
    CHECK(!queue.Peek(retry));

    auto old_notice=std::make_shared<const std::string>("Old\n");
    auto newer_notice=std::make_shared<const std::string>("New\n");
    queue.Publish(old_notice);
    CHECK(queue.Peek(snapshot) && snapshot==old_notice);
    const auto applied_old=queue.ApplyNext([&](const std::string& bytes) {
        if(bytes!="Old\n") return false;
        queue.Publish(newer_notice); // newer commit arrives while old apply is in flight
        return true;
    });
    CHECK(applied_old==NoticeApplyResult::Applied);
    CHECK(queue.Peek(retry) && retry==newer_notice && *retry=="New\n");
    CHECK(queue.ApplyNext([](const std::string&) { return true; })==NoticeApplyResult::Applied);
    CHECK(!queue.Peek(snapshot));

    // Race repeated publishers against a consumer that acknowledges snapshots.
    // The final published value must either have been observed or remain pending.
    std::atomic<bool> begin{false},finished{false},saw_final{false};
    std::thread publisher([&] {
        while(!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        for(unsigned i=0;i<10000;++i) {
            const auto value=std::to_string(i);
            queue.Publish(std::make_shared<const std::string>(value));
        }
        finished.store(true,std::memory_order_release);
    });
    std::thread consumer([&] {
        while(!begin.load(std::memory_order_acquire)) std::this_thread::yield();
        for(;;) {
            CommittedSelectionNotice current;
            if(queue.Peek(current)) {
                if(*current=="9999") saw_final.store(true,std::memory_order_release);
                queue.Acknowledge(current);
            } else if(finished.load(std::memory_order_acquire)) break;
        }
    });
    begin.store(true,std::memory_order_release);
    publisher.join(); consumer.join();
    while(queue.Peek(snapshot)) {
        if(*snapshot=="9999") saw_final.store(true,std::memory_order_release);
        queue.Acknowledge(snapshot);
    }
    CHECK(saw_final.load(std::memory_order_acquire));
    CHECK(!queue.Peek(snapshot));

    std::printf("asop_selection_notice: %s\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
