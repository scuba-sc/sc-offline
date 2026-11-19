// SPDX-License-Identifier: GPL-3.0-only
//! Where each page of the menu goes, and the two small files the menu keeps. This is a port of
//! `src/menu_navigation.h` (sections, titles, summaries, developer pages, `menu-view.ini`) and of the
//! pure part of `src/menu_window.h` (window size rules, `menu-window.ini`). The Rust menu and the
//! ImGui menu read and write the same files, so the formats are the same byte for byte.
//!
//! A test parses the C++ table out of `src/menu_navigation.h` and compares it with `KNOWN`, so the
//! two cannot drift apart without a failing test.
use std::path::Path;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Section {
    Character,
    Travel,
    Vehicles,
    Building,
    People,
    Squadron42,
    Multiplayer,
    Other,
    Settings,
    Developer,
}

impl Section {
    pub fn title(self) -> &'static str {
        match self {
            Section::Character => "Character",
            Section::Travel => "Travel",
            Section::Vehicles => "Ships & vehicles",
            Section::Building => "Build",
            Section::People => "People",
            Section::Squadron42 => "Squadron 42",
            Section::Multiplayer => "Multiplayer",
            Section::Other => "More plugins",
            Section::Settings => "Settings",
            Section::Developer => "Developer",
        }
    }
}

/// The menu's own page. A plugin id never starts with "sc_offline".
pub const SETTINGS_PAGE: &str = "sc_offline.settings";
/// Not a page: the sidebar entry that opens the ImGui window for `sco.ui` tabs.
pub const CLASSIC_ENTRY: &str = "sc_offline.classic";

pub struct Known {
    pub id: &'static str,
    pub title: &'static str,
    pub summary: &'static str,
    pub section: Section,
    pub rank: i32,
}

macro_rules! known {
    ($id:expr, $title:expr, $summary:expr, $section:ident, $rank:expr) => {
        Known {
            id: $id,
            title: $title,
            summary: $summary,
            section: Section::$section,
            rank: $rank,
        }
    };
}

pub const KNOWN: &[Known] = &[
    known!("loadout.player", "Gear", "What your character wears and carries: armour, clothes and weapons.", Character, 10),
    known!("gear_presets.main", "Saved outfits", "Save the gear you have on now and put it back on later.", Character, 20),
    known!("quantum.travel", "Travel", "Travel within this star system. For now it only reaches the place you are in and the bigger places around it.", Travel, 10),
    known!("teleport.travel", "Saved spots", "Remember where you are standing (F7) and go back there (F8).", Travel, 20),
    known!("spawn.vehicles", "Ships & vehicles", "Put a ship or ground vehicle next to you and get in.", Vehicles, 10),
    known!("build.picker", "Build", "Place props and buildings in front of you.", Building, 10),
    known!("build.build", "Build", "Place props and buildings in front of you.", Building, 10),
    known!("creative.panel", "Creative", "God mode, flying through walls, and unlimited ammo for you and your ship.", Building, 20),
    known!("crew.crew", "Crew", "Give your ship an AI crew: pilots, gunners and engineers.", People, 10),
    known!("npc.npcs", "Characters", "Put a character from the game near you.", People, 20),
    known!("loadout.sq42", "Squadron 42", "Squadron 42 outfits, ships and game settings. Contains story spoilers.", Squadron42, 10),
    known!("multiplayer.session", "Multiplayer", "Play together with friends in a private session.", Multiplayer, 10),
    known!("titanlink.titanfall", "Titanfall link", "Connection to the Titanfall bridge plugin.", Other, 10),
    known!("voxel_bridge.voxel", "Voxel link", "Connection to the voxel bridge plugin.", Other, 20),
    known!(SETTINGS_PAGE, "Menu settings", "How this menu looks, developer pages, and where to report problems.", Settings, 10),
    known!("inspector.main", "Inspector", "Read-only view of what the game reports. For finding bugs.", Developer, 10),
    known!("ui_probe.main", "UI probe", "Test page for the plugin UI.", Developer, 20),
];

pub fn find_known(id: &str) -> Option<&'static Known> {
    KNOWN.iter().find(|k| k.id == id)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Source {
    Panel,
    Settings,
    Classic,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Page {
    pub id: String,
    pub title: String,
    pub summary: String,
    pub section: Section,
    pub rank: i32,
    pub arrival: i32,
    pub source: Source,
}

/// One live page. `title` is the plugin's own and is kept only for pages sc-offline does not know;
/// an empty title falls back to the id.
pub fn describe(id: &str, title: &str, arrival: i32, source: Source) -> Page {
    if let Some(k) = find_known(id) {
        return Page {
            id: id.into(),
            title: k.title.into(),
            summary: k.summary.into(),
            section: k.section,
            rank: k.rank,
            arrival,
            source,
        };
    }
    let owner = id.split('.').next().unwrap_or(id);
    Page {
        id: id.into(),
        title: if title.is_empty() {
            id.into()
        } else {
            title.into()
        },
        summary: format!("Added by the {owner} plugin."),
        section: Section::Other,
        rank: 1000,
        arrival,
        source,
    }
}

/// Developer pages are left out unless asked for; the rest is sorted by section, rank, then arrival.
/// A second page with an id already listed is dropped.
pub fn arrange(pages: Vec<Page>, developer: bool) -> Vec<Page> {
    let mut out: Vec<Page> = Vec::with_capacity(pages.len());
    for p in pages {
        if p.section == Section::Developer && !developer {
            continue;
        }
        if !out.iter().any(|q| q.id == p.id) {
            out.push(p);
        }
    }
    out.sort_by_key(|p| (p.section, p.rank, p.arrival));
    out
}

/// The page to show: the one asked for when it is still there, otherwise the first page.
pub fn choose(pages: &[Page], wanted: &str) -> String {
    if pages
        .iter()
        .any(|p| p.id == wanted && p.source != Source::Classic)
    {
        return wanted.to_string();
    }
    pages
        .iter()
        .find(|p| p.source != Source::Classic)
        .map(|p| p.id.clone())
        .unwrap_or_default()
}

// ---- menu-view.ini: the page that was open and the developer switch ----

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ViewState {
    pub page: String,
    pub developer: bool,
}

pub const MAX_PAGE_ID: usize = 63;

pub fn valid_page_id(id: &str) -> bool {
    !id.is_empty()
        && id.len() <= MAX_PAGE_ID
        && id.bytes().all(|c| {
            c.is_ascii_lowercase() || c.is_ascii_digit() || matches!(c, b'_' | b'.' | b'-')
        })
}

fn trim(s: &str) -> &str {
    s.trim_matches(|c| c == ' ' || c == '\t' || c == '\r')
}

/// Reads a file the way `LoadView` does: at most 256 bytes, anything unexpected gives the defaults.
pub fn parse_view(bytes: &[u8]) -> ViewState {
    if bytes.len() > 256 {
        return ViewState::default();
    }
    let Ok(text) = std::str::from_utf8(bytes) else {
        return ViewState::default();
    };
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut view = ViewState::default();
    let (mut section, mut page, mut developer) = (false, false, false);
    for raw in text.split('\n') {
        let line = trim(raw);
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line == "[view]" {
            section = true;
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            return ViewState::default();
        };
        if !section {
            return ViewState::default();
        }
        let (k, v) = (trim(k), trim(v));
        if k == "page" && !page {
            if !valid_page_id(v) {
                return ViewState::default();
            }
            view.page = v.into();
            page = true;
        } else if k == "developer" && !developer {
            if v != "0" && v != "1" {
                return ViewState::default();
            }
            view.developer = v == "1";
            developer = true;
        } else {
            return ViewState::default();
        }
    }
    view
}

pub fn load_view(path: &Path) -> ViewState {
    std::fs::read(path)
        .map(|b| parse_view(&b))
        .unwrap_or_default()
}

pub fn view_text(view: &ViewState) -> String {
    let mut s = String::from("[view]\n");
    if valid_page_id(&view.page) {
        s.push_str(&format!("page={}\n", view.page));
    }
    s.push_str(&format!("developer={}\n", u8::from(view.developer)));
    s
}

pub fn save_view(path: &Path, view: &ViewState) -> bool {
    std::fs::write(path, view_text(view)).is_ok()
}

// ---- menu-window.ini and the size rules ----

pub const MIN_WIDTH: i32 = 720;
pub const MIN_HEIGHT: i32 = 520;
pub const MAX_SAVED: i32 = 8192;
pub const MAX_PERCENT: i64 = 90;
pub const DEFAULT_SIZE: Size = Size {
    width: 1000,
    height: 800,
};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Size {
    pub width: i32,
    pub height: i32,
}

impl Default for Size {
    fn default() -> Self {
        DEFAULT_SIZE
    }
}

pub fn size_valid(s: Size) -> bool {
    s.width > 0 && s.height > 0 && s.width <= 32768 && s.height <= 32768
}

pub fn size_sane(s: Size) -> bool {
    s.width >= MIN_WIDTH && s.height >= MIN_HEIGHT && s.width <= MAX_SAVED && s.height <= MAX_SAVED
}

/// A size clamped to [the minimum, 90 percent of the work area]; the minimum yields to a small area.
/// All values are in logical (96 dpi) points.
pub fn clamp_size(s: Size, work_w: i64, work_h: i64) -> Size {
    if work_w <= 0 || work_h <= 0 {
        return DEFAULT_SIZE;
    }
    let s = if size_valid(s) { s } else { DEFAULT_SIZE };
    let max_w = (work_w * MAX_PERCENT / 100).max(1);
    let max_h = (work_h * MAX_PERCENT / 100).max(1);
    let min_w = (MIN_WIDTH as i64).min(max_w);
    let min_h = (MIN_HEIGHT as i64).min(max_h);
    Size {
        width: (s.width as i64).max(min_w).min(max_w) as i32,
        height: (s.height as i64).max(min_h).min(max_h) as i32,
    }
}

/// What is written when a resize ends.
pub fn remember_size(s: Size, work_w: i64, work_h: i64) -> Size {
    let c = clamp_size(s, work_w, work_h);
    Size {
        width: c.width.clamp(MIN_WIDTH, MAX_SAVED),
        height: c.height.clamp(MIN_HEIGHT, MAX_SAVED),
    }
}

/// What the window is given when shown.
pub fn restore_size(saved: Size, work_w: i64, work_h: i64) -> Size {
    let s = if size_valid(saved) && size_sane(saved) {
        saved
    } else {
        DEFAULT_SIZE
    };
    clamp_size(s, work_w, work_h)
}

/// Reads `menu-window.ini` the way `Load` does: strict, 256 bytes at most, the defaults on anything odd.
pub fn parse_size(bytes: &[u8]) -> Size {
    if bytes.len() > 256 {
        return DEFAULT_SIZE;
    }
    let Ok(text) = std::str::from_utf8(bytes) else {
        return DEFAULT_SIZE;
    };
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut size = DEFAULT_SIZE;
    let (mut section, mut w, mut h) = (false, false, false);
    for raw in text.split('\n') {
        let line = trim(raw);
        if line.is_empty() || line.starts_with('#') || line.starts_with(';') {
            continue;
        }
        if line == "[window]" {
            section = true;
            continue;
        }
        let Some((k, v)) = line.split_once('=') else {
            return DEFAULT_SIZE;
        };
        if !section {
            return DEFAULT_SIZE;
        }
        let (k, v) = (trim(k), trim(v));
        if v.is_empty() || !v.bytes().all(|c| c.is_ascii_digit() || c == b'-') {
            return DEFAULT_SIZE;
        }
        let Ok(n) = v.parse::<i32>() else {
            return DEFAULT_SIZE;
        };
        if k == "width" && !w {
            size.width = n;
            w = true;
        } else if k == "height" && !h {
            size.height = n;
            h = true;
        } else {
            return DEFAULT_SIZE;
        }
    }
    if w && h && size_valid(size) && size_sane(size) {
        size
    } else {
        DEFAULT_SIZE
    }
}

pub fn load_size(path: &Path) -> Size {
    std::fs::read(path)
        .map(|b| parse_size(&b))
        .unwrap_or(DEFAULT_SIZE)
}

pub fn save_size(path: &Path, s: Size) -> bool {
    if !size_valid(s) || !size_sane(s) {
        return false;
    }
    std::fs::write(
        path,
        format!("[window]\nwidth={}\nheight={}\n", s.width, s.height),
    )
    .is_ok()
}

// ---- resize edges of the borderless window (the Win32 HT* values of menu_window.h) ----

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Edge {
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
}

pub const EDGE_BASE: f32 = 5.0;

/// `x`, `y` relative to the window's top-left; `edge` in the same units.
pub fn hit_test(x: f32, y: f32, width: f32, height: f32, edge: f32) -> Option<Edge> {
    if width <= 0.0 || height <= 0.0 || x < 0.0 || y < 0.0 || x >= width || y >= height {
        return None;
    }
    let (l, r, t, b) = (x < edge, x >= width - edge, y < edge, y >= height - edge);
    if t {
        return Some(if l {
            Edge::TopLeft
        } else if r {
            Edge::TopRight
        } else {
            Edge::Top
        });
    }
    if b {
        return Some(if l {
            Edge::BottomLeft
        } else if r {
            Edge::BottomRight
        } else {
            Edge::Bottom
        });
    }
    if l {
        Some(Edge::Left)
    } else if r {
        Some(Edge::Right)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pages(ids: &[&str]) -> Vec<Page> {
        ids.iter()
            .enumerate()
            .map(|(i, id)| describe(id, "", i as i32, Source::Panel))
            .collect()
    }

    #[test]
    fn known_pages_get_their_section_title_and_summary() {
        let p = describe(
            "loadout.player",
            "Whatever the plugin says",
            0,
            Source::Panel,
        );
        assert_eq!(p.title, "Gear");
        assert_eq!(p.section, Section::Character);
        assert!(p.summary.starts_with("What your character wears"));
    }

    #[test]
    fn unknown_pages_go_to_more_plugins_under_their_own_title() {
        let p = describe("acme.tool", "Acme tool", 3, Source::Panel);
        assert_eq!(p.section, Section::Other);
        assert_eq!(p.title, "Acme tool");
        assert_eq!(p.summary, "Added by the acme plugin.");
        assert_eq!(
            describe("acme.tool", "", 0, Source::Panel).title,
            "acme.tool"
        );
        // A copied title does not move a page: the id decides.
        assert_eq!(
            describe("evil.gear", "Gear", 0, Source::Panel).section,
            Section::Other
        );
    }

    #[test]
    fn arrange_orders_by_section_rank_arrival_and_hides_developer_pages() {
        let list = pages(&[
            "inspector.main",
            "acme.tool",
            "crew.crew",
            "gear_presets.main",
            "loadout.player",
            "teleport.travel",
            "quantum.travel",
            "crew.crew",
        ]);
        let shown = arrange(list.clone(), false);
        let ids: Vec<&str> = shown.iter().map(|p| p.id.as_str()).collect();
        assert_eq!(
            ids,
            [
                "loadout.player",
                "gear_presets.main",
                "quantum.travel",
                "teleport.travel",
                "crew.crew",
                "acme.tool"
            ]
        );
        let dev = arrange(list, true);
        assert_eq!(dev.last().unwrap().id, "inspector.main");
    }

    #[test]
    fn choose_keeps_the_wanted_page_or_falls_back_to_the_first() {
        let list = arrange(pages(&["crew.crew", "loadout.player"]), false);
        assert_eq!(choose(&list, "crew.crew"), "crew.crew");
        assert_eq!(choose(&list, "gone.page"), "loadout.player");
        assert_eq!(choose(&[], "x"), "");
        let mut with_classic = list;
        with_classic.push(describe(CLASSIC_ENTRY, "Classic pages", 9, Source::Classic));
        assert_eq!(choose(&with_classic, CLASSIC_ENTRY), "loadout.player");
    }

    #[test]
    fn view_file_round_trip_and_refusals() {
        let v = ViewState {
            page: "crew.crew".into(),
            developer: true,
        };
        assert_eq!(parse_view(view_text(&v).as_bytes()), v);
        assert_eq!(parse_view(b"[view]\npage=Bad Id\n"), ViewState::default());
        assert_eq!(parse_view(b"page=crew.crew\n"), ViewState::default());
        assert_eq!(parse_view(b"[view]\ndeveloper=2\n"), ViewState::default());
        assert_eq!(
            parse_view(b"[view]\ndeveloper=1\ndeveloper=0\n"),
            ViewState::default()
        );
        assert_eq!(parse_view(&[b'a'; 300]), ViewState::default());
        // The exact bytes the C++ SaveView writes.
        assert_eq!(view_text(&v), "[view]\npage=crew.crew\ndeveloper=1\n");
        assert_eq!(view_text(&ViewState::default()), "[view]\ndeveloper=0\n");
        assert_eq!(
            parse_view(b"\xef\xbb\xbf# c\n[view]\r\npage = a.b \r\ndeveloper=0\r\n").page,
            "a.b"
        );
    }

    #[test]
    fn size_file_round_trip_and_refusals() {
        assert_eq!(
            parse_size(b"[window]\nwidth=900\nheight=640\n"),
            Size {
                width: 900,
                height: 640
            }
        );
        assert_eq!(
            parse_size(b"[window]\nwidth=100\nheight=640\n"),
            DEFAULT_SIZE
        );
        assert_eq!(parse_size(b"[window]\nwidth=900\n"), DEFAULT_SIZE);
        assert_eq!(
            parse_size(b"[window]\nwidth=9x0\nheight=640\n"),
            DEFAULT_SIZE
        );
        assert_eq!(
            parse_size(b"[window]\nwidth=900\nheight=640\nextra=1\n"),
            DEFAULT_SIZE
        );
        let dir = std::env::temp_dir().join(format!("scol-nav-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let p = dir.join("menu-window.ini");
        assert!(save_size(
            &p,
            Size {
                width: 1100,
                height: 700
            }
        ));
        assert_eq!(
            std::fs::read_to_string(&p).unwrap(),
            "[window]\nwidth=1100\nheight=700\n"
        );
        assert_eq!(
            load_size(&p),
            Size {
                width: 1100,
                height: 700
            }
        );
        assert!(!save_size(
            &p,
            Size {
                width: 10,
                height: 10
            }
        ));
        let v = dir.join("menu-view.ini");
        assert!(save_view(
            &v,
            &ViewState {
                page: "a.b".into(),
                developer: false
            }
        ));
        assert_eq!(load_view(&v).page, "a.b");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn size_rules_match_the_cpp_header() {
        // 90 percent of a 1920x1080 area, the minimum yields to a small one.
        assert_eq!(
            clamp_size(
                Size {
                    width: 5000,
                    height: 5000
                },
                1920,
                1080
            ),
            Size {
                width: 1728,
                height: 972
            }
        );
        assert_eq!(
            clamp_size(
                Size {
                    width: 10,
                    height: 10
                },
                1920,
                1080
            ),
            Size {
                width: 720,
                height: 520
            }
        );
        assert_eq!(
            clamp_size(
                Size {
                    width: 1000,
                    height: 800
                },
                800,
                600
            ),
            Size {
                width: 720,
                height: 540
            }
        );
        assert_eq!(
            restore_size(
                Size {
                    width: 1,
                    height: 1
                },
                1920,
                1080
            ),
            DEFAULT_SIZE
        );
        assert_eq!(
            remember_size(
                Size {
                    width: 800,
                    height: 600
                },
                1920,
                1080
            ),
            Size {
                width: 800,
                height: 600
            }
        );
        assert_eq!(
            remember_size(
                Size {
                    width: 300,
                    height: 300
                },
                640,
                480
            ),
            Size {
                width: 720,
                height: 520
            }
        );
    }

    #[test]
    fn edge_hit_test() {
        assert_eq!(hit_test(2.0, 2.0, 100.0, 80.0, 5.0), Some(Edge::TopLeft));
        assert_eq!(hit_test(50.0, 2.0, 100.0, 80.0, 5.0), Some(Edge::Top));
        assert_eq!(
            hit_test(99.0, 79.0, 100.0, 80.0, 5.0),
            Some(Edge::BottomRight)
        );
        assert_eq!(hit_test(2.0, 40.0, 100.0, 80.0, 5.0), Some(Edge::Left));
        assert_eq!(hit_test(50.0, 40.0, 100.0, 80.0, 5.0), None);
        assert_eq!(hit_test(-1.0, 40.0, 100.0, 80.0, 5.0), None);
    }

    /// The table of the ImGui menu is the source of truth: the Rust copy must equal it.
    #[test]
    fn known_table_equals_menu_navigation_h() {
        let path =
            std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../../src/menu_navigation.h");
        let Ok(text) = std::fs::read_to_string(&path) else {
            eprintln!("src/menu_navigation.h not found; parity check skipped");
            return;
        };
        let start = text.find("kKnown[] = {").expect("table start");
        let body = &text[start..];
        let end = body.find("};").expect("table end");
        let mut rows = 0;
        for line in body[..end].lines().skip(1) {
            let line = line.trim();
            if !line.starts_with('{') {
                continue;
            }
            // {"id", "title", "summary", Section::X, rank},
            let line = line
                .trim_start_matches('{')
                .trim_end_matches(',')
                .trim_end_matches('}');
            let (id, rest) = field(line);
            let (title, rest) = field(rest);
            let (summary, rest) = field(rest);
            let rest = rest.trim().trim_start_matches(',').trim();
            let (sec, rank) = rest.split_once(',').unwrap();
            let sec = sec.trim().trim_start_matches("Section::");
            let rank: i32 = rank.trim().parse().unwrap();
            let k = find_known(&id).unwrap_or_else(|| panic!("Rust table lacks {id}"));
            assert_eq!(k.title, title, "{id}");
            assert_eq!(k.summary, summary, "{id}");
            assert_eq!(format!("{:?}", k.section), sec, "{id}");
            assert_eq!(k.rank, rank, "{id}");
            rows += 1;
        }
        assert_eq!(rows, KNOWN.len());
    }

    /// Reads one C++ string argument (a literal or kSettingsPage) and returns it with the rest.
    fn field(s: &str) -> (String, &str) {
        let s = s.trim();
        if let Some(r) = s.strip_prefix("kSettingsPage") {
            return (SETTINGS_PAGE.to_string(), r.trim_start_matches(',').trim());
        }
        let r = s.strip_prefix('"').expect("a string literal");
        let mut out = String::new();
        let mut chars = r.char_indices();
        while let Some((i, c)) = chars.next() {
            match c {
                '\\' => {
                    let (_, n) = chars.next().unwrap();
                    out.push(n);
                }
                '"' => return (out, r[i + 1..].trim_start_matches(',').trim()),
                _ => out.push(c),
            }
        }
        panic!("unterminated literal");
    }
}
