// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// For the synthetic test fixtures' set_panel: Violation() applies the node and tree rules the real
// sco.widgets service applies (sco-core src/widgets/widgets.cpp CopyNode/ParseNodes), which the
// fixtures don't otherwise check, so a page the host would refuse fails its test instead of
// vanishing in game. Record() keeps every accepted panel state for tools/menu_preview when
// SCO_PANEL_DUMP names a file. Test-only: never compiled into the product or a plugin.
//
// Format (UTF-8, one record per panel state, fields separated by tabs, \\ \t \n escaped):
//   panel <seq> <id> <title> <node count>
//   node <kind> <id> <parent> <flags> <index> <value> <min> <max> <columns> <severity> <first_row>
//        <visible_rows> <presentation> <label> <text> <tooltip> <items, separated by \x1f>
#include <sco_widgets.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace sc_offline::panel_dump {

// "" when the host would accept these nodes; otherwise what is wrong (1.0 rules and the 1.1 tail).
inline std::string Violation(const sco_widget_node* nodes, uint32_t count, uint32_t stride) {
    if (!nodes || !count || stride < sizeof(sco_widget_node) || stride % alignof(sco_widget_node)) return "bad node array";
    if (count > SCO_WIDGETS_MAX_NODES) return "more than SCO_WIDGETS_MAX_NODES nodes";
    std::vector<uint32_t> ids, ancestors{0};
    auto text = [](const char* s) { return !s || std::strlen(s) <= SCO_WIDGETS_MAX_TEXT; };
    for (uint32_t i = 0; i < count; ++i) {
        const auto* p = reinterpret_cast<const unsigned char*>(nodes) + static_cast<size_t>(i) * stride;
        const auto& n = *reinterpret_cast<const sco_widget_node*>(p);
        const std::string at = "node " + std::to_string(i) + " (id " + std::to_string(n.id) + "): ";
        if (n.size < sizeof(sco_widget_node) || n.size > stride) return at + "size";
        if (!n.id || std::find(ids.begin(), ids.end(), n.id) != ids.end()) return at + "zero or duplicate id";
        const auto parent = std::find(ancestors.begin(), ancestors.end(), n.parent);
        if (parent == ancestors.end()) return at + "parent is not 0 or an open preceding ROW/COLUMN (preorder, contiguous)";
        ancestors.erase(parent + 1, ancestors.end());
        if (n.kind < SCO_WIDGET_LABEL || n.kind > SCO_WIDGET_COLUMN) return at + "kind";
        if ((n.flags & ~(SCO_WIDGET_DISABLED | SCO_WIDGET_PASSWORD)) || ((n.flags & SCO_WIDGET_PASSWORD) && n.kind != SCO_WIDGET_TEXT_INPUT))
            return at + "flags";
        if (!std::isfinite(n.value) || !std::isfinite(n.minimum) || !std::isfinite(n.maximum)) return at + "non-finite number";
        const bool numeric = n.kind == SCO_WIDGET_TOGGLE || n.kind == SCO_WIDGET_SLIDER;
        const bool selection = n.kind == SCO_WIDGET_COMBO || n.kind == SCO_WIDGET_LIST;
        const bool items = selection || n.kind == SCO_WIDGET_TABLE;
        if ((!numeric && n.value != 0) || (n.kind != SCO_WIDGET_SLIDER && (n.minimum != 0 || n.maximum != 0)) ||
            (!selection && n.index != 0 && n.index != -1) || (n.kind != SCO_WIDGET_TEXT_INPUT && n.text) ||
            (!items && (n.items || n.item_count)) || (n.kind != SCO_WIDGET_TABLE && n.columns))
            return at + "a field this kind must leave zero/NULL is set";
        if (n.kind == SCO_WIDGET_TOGGLE && n.value != 0 && n.value != 1) return at + "toggle value";
        if (n.kind == SCO_WIDGET_SLIDER && (n.minimum > n.maximum || n.value < n.minimum || n.value > n.maximum)) return at + "slider range";
        if (n.item_count > SCO_WIDGETS_MAX_ITEMS || n.columns > SCO_WIDGETS_MAX_COLUMNS) return at + "too many items/columns";
        if ((n.item_count && !n.items) || (!n.item_count && n.items)) return at + "items without count";
        if (selection && (n.index < -1 || (n.index >= 0 && static_cast<uint32_t>(n.index) >= n.item_count))) return at + "selection index";
        if (n.kind == SCO_WIDGET_TABLE && (!n.columns || n.item_count % n.columns)) return at + "table cells";
        if (!text(n.label) || !text(n.text)) return at + "text longer than SCO_WIDGETS_MAX_TEXT";
        if (n.kind >= SCO_WIDGET_BUTTON && n.kind <= SCO_WIDGET_TEXT_INPUT && (!n.label || !*n.label)) return at + "interactive node without a label";
        for (uint32_t k = 0; k < n.item_count; ++k)
            if (!n.items[k] || !text(n.items[k])) return at + "item missing or too long";
        if (n.size >= sizeof(sco_widget_node_1_1)) {
            const auto& x = *reinterpret_cast<const sco_widget_node_1_1*>(p);
            const bool pageable = n.kind == SCO_WIDGET_LIST || n.kind == SCO_WIDGET_TABLE;
            if (x.severity > SCO_WIDGET_SEVERITY_ERROR || x._reserved) return at + "severity/reserved";
            if ((!pageable && (x.first_row || x.visible_rows)) || (!x.visible_rows && x.first_row)) return at + "paging";
            if (!text(x.tooltip)) return at + "tooltip too long";
        }
        ids.push_back(n.id);
        if (n.kind == SCO_WIDGET_ROW || n.kind == SCO_WIDGET_COLUMN) ancestors.push_back(n.id);
    }
    return {};
}

inline std::string Escape(const char* text) {
    std::string out;
    for (const char* p = text ? text : ""; *p; ++p) {
        if (*p == '\\') out += "\\\\";
        else if (*p == '\t') out += "\\t";
        else if (*p == '\n') out += "\\n";
        else if (*p == '\r') out += "\\r";
        else out += *p;
    }
    return out;
}

// Call from a fixture's set_panel after Violation() and the fixture's own checks accepted the panel.
inline void Record(const char* id, const char* title, const sco_widget_node* nodes, uint32_t count, uint32_t stride) {
    std::string path;
#ifdef _MSC_VER
    char* value = nullptr;
    size_t length = 0;
    if (_dupenv_s(&value, &length, "SCO_PANEL_DUMP") == 0 && value) { path = value; std::free(value); }
#else
    if (const char* value = std::getenv("SCO_PANEL_DUMP")) path = value;
#endif
    if (path.empty() || !id || !nodes || !count || stride < sizeof(sco_widget_node)) return;
    static unsigned long sequence = 0;
    FILE* file = nullptr;
#ifdef _MSC_VER
    if (fopen_s(&file, path.c_str(), "ab") != 0) file = nullptr;
#else
    file = std::fopen(path.c_str(), "ab");
#endif
    if (!file) return;
    std::fprintf(file, "panel\t%lu\t%s\t%s\t%u\n", ++sequence, Escape(id).c_str(), Escape(title).c_str(), count);
    const auto* bytes = reinterpret_cast<const unsigned char*>(nodes);
    for (uint32_t i = 0; i < count; ++i) {
        const auto* node = reinterpret_cast<const sco_widget_node*>(bytes + static_cast<size_t>(i) * stride);
        const char* tooltip = nullptr;
        uint32_t severity = 0, first = 0, visible = 0, presentation = 0;
        if (node->size >= sizeof(sco_widget_node_1_1)) {
            const auto* extended = reinterpret_cast<const sco_widget_node_1_1*>(node);
            tooltip = extended->tooltip;
            severity = extended->severity;
            first = extended->first_row;
            visible = extended->visible_rows;
        }
#ifdef SCO_WIDGET_NUMERIC_NUMBER
        if (node->size >= sizeof(sco_widget_node_1_3))
            presentation = reinterpret_cast<const sco_widget_node_1_3*>(node)->presentation;
#endif
        std::string items;
        for (uint32_t k = 0; node->items && k < node->item_count; ++k) {
            if (k) items += '\x1f';
            items += Escape(node->items[k]);
        }
        std::fprintf(file, "node\t%u\t%u\t%u\t%u\t%d\t%.17g\t%.17g\t%.17g\t%u\t%u\t%u\t%u\t%u\t%s\t%s\t%s\t%s\n",
                     node->kind, node->id, node->parent, node->flags, node->index, node->value, node->minimum,
                     node->maximum, node->columns, severity, first, visible, presentation, Escape(node->label).c_str(),
                     Escape(node->text).c_str(), Escape(tooltip).c_str(), items.c_str());
    }
    std::fclose(file);
}

} // namespace sc_offline::panel_dump
