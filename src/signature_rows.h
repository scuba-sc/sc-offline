#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// Check all rows against one immutable resolution; the caller keeps the image alive.
#include "sco/signature_registry.h"
#include <cstdio>

namespace offline::signature_rows {
inline bool Ready(const sco::SignatureSnapshot* snapshot, const char* const* ids, size_t count,
                  char* why, size_t whySize) {
    if (!snapshot) { std::snprintf(why, whySize, "signature registry unavailable"); return false; }
    for (size_t i = 0; i < count; ++i) {
        const sco::SigResult* result = snapshot->Lookup(ids[i]);
        if (!result) { std::snprintf(why, whySize, "unknown signature %s", ids[i]); return false; }
        if (result->state != sco::SigState::Ok) {
            std::snprintf(why, whySize, "needs %s (%s)", ids[i], sco::SigStateName(result->state));
            return false;
        }
    }
    return true;
}
} // namespace offline::signature_rows
