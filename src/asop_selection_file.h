// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <sco/game/asop_selection.h>
#include <string>

namespace offline::asop_selection {
// Host-owned, fixed sibling of the configured ships file. No plugin supplies a path.
class FixedFile {
public:
    bool Initialize();
    sco::game::asop_selection::FileAdapter Adapter();
    const std::wstring& Diagnostic() const { return diagnostic_; }
private:
    static sco::game::asop_selection::FileResult Read(void*, sco::game::asop_selection::Document&);
    static sco::game::asop_selection::FileResult Replace(void*, const sco::game::asop_selection::Document&,
        std::string_view, sco::game::asop_selection::Document&, const sco::game::asop_selection::CommitPermit&);
    std::wstring path_, diagnostic_;
};
}
