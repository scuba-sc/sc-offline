#pragma once
// SPDX-License-Identifier: GPL-3.0-only
// The caller owns the snapshot throughout this call; this is not an image lease
// or a native reader/worker barrier.
#include "sco/signature_registry.h"
#include <cstdio>

namespace offline::signature_rows {
inline bool Ready(const sco::SignatureSnapshot* snapshot, const char* const* ids, size_t count,
                  char* why, size_t whySize) {
    if (!snapshot) {
        if (why && whySize) std::snprintf(why, whySize, "signature registry unavailable");
        return false;
    }
    if (!ids || !count) {
        if (why && whySize) std::snprintf(why, whySize, "invalid signature rows");
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!ids[i]) {
            if (why && whySize) std::snprintf(why, whySize, "invalid signature rows");
            return false;
        }
        const sco::SigResult* result = snapshot->Lookup(ids[i]);
        if (!result) {
            if (why && whySize) std::snprintf(why, whySize, "unknown signature %s", ids[i]);
            return false;
        }
        if (result->state != sco::SigState::Ok) {
            if (why && whySize) std::snprintf(why, whySize, "needs %s (%s)", ids[i], sco::SigStateName(result->state));
            return false;
        }
    }
    return true;
}
}
