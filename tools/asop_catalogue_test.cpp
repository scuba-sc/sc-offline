// SPDX-License-Identifier: GPL-3.0-only
#include "asop_catalogue.h"
#include <cstdio>
int main() {
    using offline::asop_selection::ParseCatalogue;
    std::vector<std::string> result;
    if (!ParseCatalogue(" # comment\r\n Alpha_One  # trailing\nBeta-2\n", result) ||
        result != std::vector<std::string>{"Alpha_One", "Beta-2"}) return 1;
    const auto old = result;
    if (ParseCatalogue("good\nbad name\n", result) || result != old) return 2;
    if (ParseCatalogue(std::string("a\0b", 3), result) || result != old) return 3;
    if (ParseCatalogue(std::string(SC_ASOP_SELECTION_MAX_FILE_BYTES + 1, 'a'), result) || result != old) return 4;
    if (!ParseCatalogue("", result) || !result.empty()) return 5;
    std::puts("ASOP complete catalogue parse and refusal passed");
}
