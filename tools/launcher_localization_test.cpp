// SPDX-License-Identifier: GPL-3.0-only
#include "../launcher/localization.h"
#include "../launcher/locale_ru_builtin.h"
#include <cstdlib>
#include <iostream>
#include <vector>
#include <fstream>

using namespace sco::launcher::locale;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << "FAIL " << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

int main(int argc, char** argv) {
    CHECK(argc == 1 || argc == 3);
    Catalogue catalogue;
    CHECK(std::string(catalogue.Text(Key::Play)) == "Play");
    Localizer locale(true);
    CHECK(locale.LoadRussian("launcher.action.play=Играть\n"));
    locale.SetGlyphCoverage(true);
    CHECK(std::string(locale.Text(Key::Play)) == "Play");
    locale.Select(Language::Russian);
    CHECK(std::string(locale.Text(Key::Play)) == "Играть");
    CHECK(catalogue.Load("launcher.action.running=Выполняется {command}\n"));
    bool formatted = false;
    CHECK(catalogue.Format(Key::Running, {{"command", "play"}}, &formatted) == "Выполняется play");
    CHECK(formatted);
    CHECK(!catalogue.Load("launcher.removed.key=Устаревшее\n"));
    CHECK(!catalogue.Load("launcher.action.running=Выполняется {operation}\n"));
    CHECK(!catalogue.Load("launcher.action.play=\xC0\xAF\n"));
    CHECK(catalogue.Load("launcher.action.play = Играть\n"));
    CHECK(std::string(catalogue.Text(Key::Play)) == "Играть");
    CHECK(std::string(catalogue.Text(Key::FormatUnavailable)) == "Text could not be displayed.");
    CHECK(!catalogue.Load("launcher.action.play=Играть\nlauncher.action.play=Запуск\n"));
    CHECK(std::string(catalogue.Text(Key::Play)) == "Play");
    CHECK(catalogue.Diagnostic().error == Error::Duplicate && catalogue.Diagnostic().line == 2);
    CHECK(catalogue.Load("\xEF\xBB\xBF# whole-line comment\r\n launcher.action.play = Играть \r\n"));
    CHECK(catalogue.Diagnostic().missing.size() == kMessageCount - 1);
    CHECK(std::string(catalogue.Text(Key::Play)) == "Играть");
    CHECK(!catalogue.Load("launcher.removed.key=Old\n"));
    CHECK(catalogue.Diagnostic().obsolete == std::vector<std::string>{"launcher.removed.key"});
    CHECK(std::string(catalogue.Text(Key::Play)) == "Play");
    for (const std::string& invalid : std::vector<std::string>{
             "\x80", "\xC0\xAF", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80",
             "\xF0\x80\x80\xAF", "\xE2\x82", "\xC2\x85", "\x7F", std::string(1, '\0')}) {
        CHECK(!catalogue.Load("launcher.action.play=" + invalid));
        CHECK(std::string(catalogue.Text(Key::Play)) == "Play");
    }
    for (const auto* value : {"", "bad\\q", "unfinished\\", "raw\tcontrol", "{x}", "extra}", "{", "{{x}}"})
        CHECK(!catalogue.Load(std::string("launcher.action.play=") + value));
    CHECK(catalogue.Load("launcher.action.play=data\\\\plugins\\\\example\n"));
    CHECK(std::string(catalogue.Text(Key::Play)) == "data\\plugins\\example");
    CHECK(catalogue.Load("launcher.action.play=%n %s ###literal\n"));
    CHECK(catalogue.Format(Key::Play, {}, &formatted) == "%n %s ###literal" && formatted);
    CHECK(catalogue.Load("launcher.action.running=Выполняется\\n{command}\n"));
    CHECK(catalogue.Format(Key::Running, {{"command", "play"}}, &formatted) == "Выполняется\nplay" && formatted);
    CHECK(!catalogue.Load("launcher.action.running={command} {command}\n"));
    CHECK(!catalogue.Load("launcher.action.running=missing\n"));
    CHECK(!catalogue.Load("launcher.action.running={Command}\n"));
    CHECK(catalogue.Load("launcher.action.running=Выполняется {command}\n"));
    for (const auto arguments : {std::initializer_list<Argument>{},
                                {{"other", "play"}}, {{"command", "a"}, {"command", "b"}}}) {
        CHECK(catalogue.Format(Key::Running, arguments, &formatted) == catalogue.Text(Key::FormatUnavailable));
        CHECK(!formatted);
    }
    const std::string big(kValueLimit + 1, 'a');
    CHECK(catalogue.Format(Key::Running, {{"command", big}}, &formatted) == catalogue.Text(Key::FormatUnavailable));
    CHECK(!formatted);
    CHECK(!catalogue.Load("launcher.action.play=" + big));
    CHECK(catalogue.Load("launcher.action.play=" + std::string(kValueLimit, 'a')));
    CHECK(std::string(catalogue.Text(Key::Play)).size() == kValueLimit);
    CHECK(!catalogue.Load(std::string(kCatalogueLimit + 1, '\n')));
    CHECK(catalogue.Load(std::string(kCatalogueLimit, '\n')));
    CHECK(!catalogue.Load("#" + std::string(kLineLimit, 'a')));
    CHECK(catalogue.Load("#" + std::string(kLineLimit - 1, 'a')));
    CHECK(!catalogue.Load(std::string(kKeyLimit + 1, 'a') + "=x"));
    CHECK(std::string(catalogue.Text(static_cast<Key>(99999))) == "Text could not be displayed.");

    Language language = Language::Russian;
    CHECK(!ParseLanguage("ru-RU", language) && language == Language::English);
    CHECK(!ParseLanguage("AUTO", language) && language == Language::English);
    CHECK(ParseLanguage("auto", language) && language == Language::Auto);
    CHECK(ParseLanguage("ru", language) && language == Language::Russian);
    CHECK(ParseLanguage("en", language) && language == Language::English);
    CHECK(std::string(LanguageName(Language::Auto)) == "auto");
    CHECK(std::string(LanguageName(Language::Russian)) == "ru");
    CHECK(std::string(LanguageName(Language::English)) == "en");
    std::string updated;
    CHECK(UpdateLanguageSetting("\xEF\xBB\xBF# keep\r\nplugins = on\r\nLanguage=ru\r\n# language=auto\r\nlanguage=auto\r\n",
                                Language::English, updated));
    CHECK(updated == "\xEF\xBB\xBF# keep\r\nplugins = on\r\nlanguage = en\r\n# language=auto\r\nlanguage = en\r\n");
    locale.Select(Language::Auto);
    CHECK(std::string(locale.Text(Key::Play)) == "Играть");
    const char* kept = locale.Text(Key::Play);
    locale.Select(Language::English);
    CHECK(std::string(locale.Text(Key::Play)) == "Play" && std::string(kept) == "Играть");
    locale.Select(Language::Russian);
    CHECK(locale.Text(Key::Play) == kept);
    CHECK(!locale.LoadRussian("launcher.action.play=replaced\n") && locale.Text(Key::Play) == kept);
    locale.SetGlyphCoverage(false);
    CHECK(std::string(locale.Text(Key::Play)) == "Play" && locale.Choice() == Language::Russian);
    locale.SetGlyphCoverage(true);
    CHECK(locale.Text(Key::Play) == kept);
    Localizer other_system;
    CHECK(other_system.LoadRussian("launcher.action.play=Играть\n"));
    other_system.SetGlyphCoverage(true);
    other_system.Select(Language::Auto);
    CHECK(std::string(other_system.Text(Key::Play)) == "Play");
    Localizer unavailable(true);
    unavailable.Select(Language::Russian);
    unavailable.SetGlyphCoverage(true);
    CHECK(std::string(unavailable.Text(Key::Play)) == "Play");
    CHECK(!unavailable.LoadRussian("launcher.action.play=broken{\n"));
    CHECK(std::string(unavailable.Text(Key::Play)) == "Play");
    CHECK(!unavailable.LoadRussian("launcher.action.play=Играть\n"));
    if (argc == 3) {
        for (int file = 1; file < 3; ++file) {
            std::ifstream input(argv[file], std::ios::binary | std::ios::ate);
            CHECK(input.good());
            const auto size = input.tellg();
            CHECK(size >= 0 && size <= static_cast<std::streamoff>(kCatalogueLimit));
            std::string bytes(static_cast<size_t>(size), '\0');
            input.seekg(0);
            CHECK(bool(input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))));
            Catalogue reference;
            const bool loaded = reference.Load(bytes);
            if (!loaded)
                std::cerr << "catalogue: " << ErrorName(reference.Diagnostic().error)
                          << ", line " << reference.Diagnostic().line << " (0 means file-level)\n";
            for (const auto& key : reference.Diagnostic().missing) std::cerr << "missing: " << key << '\n';
            for (const auto& key : reference.Diagnostic().obsolete) std::cerr << "obsolete: " << key << '\n';
            CHECK(loaded);
            CHECK(reference.Diagnostic().missing.empty());
            if (file == 2) {
                Catalogue builtin;
                CHECK(builtin.Load(kBuiltinRussianCatalogue) && builtin.Diagnostic().missing.empty());
                for (size_t i = 0; i < kMessageCount; ++i) {
                    const auto key = static_cast<Key>(i);
                    CHECK(std::string(builtin.Text(key)) == reference.Text(key));
                }
            }
            if (file == 1)
                for (size_t key = 0; key < kMessageCount; ++key)
                    CHECK(std::string(reference.Text(static_cast<Key>(key))) == kMessages[key].english);
        }
    }
    std::cout << "launcher localization: " << checks << " checks passed\n";
}
