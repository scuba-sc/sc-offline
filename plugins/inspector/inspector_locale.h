// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstring>
namespace inspector_locale {
struct Entry { const char* key; const char* en; const char* ru; };
inline const Entry entries[] = {
    {"title", "Inspector", "Инспектор"}, {"refresh", "Refresh", "Обновить"},
    {"diagnostics", "Diagnostics", "Диагностика"}, {"inventory", "Inventory", "Инвентарь"},
    {"actions", "Actions", "Действия"}, {"parameters", "Parameters", "Параметры"},
    {"results", "Results", nullptr}, {"action_name", "Action name", "Название действия"},
    {"lookup", "Lookup", "Найти"}, {"browse_parameters", "Browse parameters", "Просмотр параметров"},
    {"refresh_action_count", "Refresh action count", "Обновить число действий"},
    {"previous", "Previous", "Назад"}, {"next", "Next", "Далее"},
};
inline const char* Lookup(const char* locale, const char* key, const char* fallback = "") {
    if (!key) return fallback;
    for (const auto& item : entries) if (std::strcmp(item.key, key) == 0)
        return locale && std::strcmp(locale, "ru") == 0 && item.ru && item.ru[0] ? item.ru : item.en;
    return fallback;
}
}
