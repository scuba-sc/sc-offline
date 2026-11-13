#include "outfit_import.h"
#include <algorithm>
#include <utility>

namespace sco::outfit_import {
namespace {
bool hair_color(const std::string& s) {
    static const char name[] = "hair_color";
    if (s.size() != sizeof(name)-1) return false;
    for (std::size_t i=0; i<s.size(); ++i) {
        char c=s[i]; if (c >= 'A' && c <= 'Z') c += 'a'-'A';
        if (c != name[i]) return false;
    }
    return true;
}
}
Result import_bytes(const char* bytes, std::size_t size, bool sq42,
                    outfit_recipe::LookupFn lookup, void* context, Snapshot& output) noexcept {
    using namespace outfit_recipe;
    if (size > max_bytes || (size && !bytes)) return Result::invalid;
    try {
        // Validate the entire copy, even bytes after text EOF, so unsupported input is explicit.
        for (std::size_t i=0;i<size;++i) {
            unsigned char c=static_cast<unsigned char>(bytes[i]);
            if ((c < 32 && c != 0 && c != 9 && c != 10 && c != 13 && c != 26) || c >= 127)
                return Result::unsafe_byte;
        }
        Snapshot next;
        Outfit current; bool open=false, stop=false;
        auto finish = [&]() -> Result {
            if (!open || current.inputs.empty()) return Result::ok;
            auto result=plan_accepted(current.inputs.data(),current.inputs.size(),sq42,current.recipe);
            if (result != outfit_recipe::Result::ok) return Result::planning_failed;
            next.outfits.push_back(std::move(current));
            return Result::ok;
        };
        std::size_t pos=0;
        while (pos<size && !stop) {
            std::string line;
            // Windows text mode converts CRLF before the 255-character fgets limit.
            while (pos<size && line.size()<255) {
                char c=bytes[pos++];
                if (c==26) { stop=true; break; }
                if (c=='\r' && pos<size && bytes[pos]=='\n') { ++pos; c='\n'; }
                line.push_back(c);
                if (c=='\n') break;
            }
            auto end=line.find_first_of(std::string("\0\r\n#",4));
            if (end!=std::string::npos) line.resize(end);
            auto start=line.find_first_not_of(" \t");
            if (start==std::string::npos) continue;
            if (line[start]=='[') {
                auto status=finish(); if (status!=Result::ok) return status;
                open=false;
                if (next.outfits.size()>=max_sections) break;
                current=Outfit{};
                auto close=line.find(']',start+1);
                current.name=line.substr(start+1, close==std::string::npos ? close : close-start-1);
                if (current.name.size()>127) current.name.resize(127);
                open=true; continue;
            }
            if (!open || current.inputs.size()>=max_inputs) continue;
            auto sep=line.find_first_of(" \t",start);
            if (sep==std::string::npos) continue;
            auto item_start=line.find_first_not_of(" \t",sep);
            if (item_start==std::string::npos) continue;
            auto item_end=line.find_first_of(" \t\r\n",item_start);
            std::string role=line.substr(start,sep-start), item=line.substr(item_start,item_end-item_start);
            if (!hair_color(role)) {
                if (!lookup) return Result::lookup_unavailable;
                Lookup found;
                try { found=lookup(item,context); } catch (...) { return Result::lookup_unavailable; }
                if (found==Lookup::unavailable) return Result::lookup_unavailable;
                if (found==Lookup::absent) { ++next.skipped_unknown; continue; }
                if (found!=Lookup::present) return Result::invalid;
            }
            role.resize((std::min)(role.size(),max_token));
            item.resize((std::min)(item.size(),max_token));
            current.inputs.push_back({std::move(role),std::move(item)});
        }
        auto status=finish(); if (status!=Result::ok) return status;
        output.outfits.swap(next.outfits); output.skipped_unknown=next.skipped_unknown;
        return Result::ok;
    } catch (...) { return Result::resource_failure; }
}
}
