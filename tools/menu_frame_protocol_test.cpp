// SPDX-License-Identifier: GPL-3.0-only
#include "menu_frame_protocol.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace sc_offline::menu_frame;
int main() {
    Protocol p;
    assert(!p.Acquire() && p.Request() && !p.Request());
    assert(p.Acquire() && p.Current()==State::Building && !p.CancelUnstarted());
    p.Finish(false);assert(p.Current()==State::Cancelled && !p.TakeBuilt() && p.TakeCancelled());
    assert(p.Request() && p.Acquire());p.Finish(true);assert(p.TakeBuilt());
    int wakes=0, aborts=0;
    assert(p.Request());
    assert(Run(p,[]{return false;},[&]{++aborts;},[&]{++wakes;}));
    assert(aborts==1 && wakes==1 && p.Current()==State::Cancelled && p.TakeCancelled());
    assert(p.Request());
    assert(Run(p,[]()->bool {throw 1;},[&]{++aborts;throw 2;},[&]{++wakes;}));
    assert(aborts==2 && wakes==2 && p.Current()==State::Cancelled && p.TakeCancelled());
    assert(p.Request());
    assert(Run(p,[]{return true;},[&]{++aborts;},[&]{++wakes;}));
    assert(aborts==2 && wakes==3 && p.Current()==State::Built && p.TakeBuilt());
    for(int i=0;i<1000;++i) {
        assert(p.Request());bool acquired=false,cancelled=false;
        std::thread game([&]{acquired=p.Acquire();});
        std::thread menu([&]{cancelled=p.CancelUnstarted();});
        game.join();menu.join();assert(acquired!=cancelled);
        if(acquired) {p.Finish(false);assert(p.TakeCancelled());}
        assert(p.Current()==State::Idle);
    }
    std::puts("menu frame protocol: terminal and competing ownership transitions passed");
}
