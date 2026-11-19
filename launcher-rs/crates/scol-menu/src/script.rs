// SPDX-License-Identifier: GPL-3.0-only
//! The test driver's step language. A host (the Rust example or the C++ tool) sends one step line at
//! a time through `scol_menu_test_step`; the menu turns it into a list of micro operations and plays
//! them as real egui input events, one frame at a time (pointer move, press, release, text, key), so
//! a click goes through the same hit test a player's click does. Never used outside tests: the
//! export is refused unless the host started the menu with the test flag.
//!
//! Lines:
//!   select <panel id>             open a page in the sidebar
//!   click <tag>                   press a BUTTON, TOGGLE (or any control); `label:<text>` names a node by label
//!   slider <tag> <fraction>       press on the slider rail at 0.0 to 1.0 of its length
//!   combo <tag> <index>           open the combo and press item <index>
//!   list <tag> <index>            press row <index> (absolute row) of a LIST
//!   text <tag> <text...>          focus the box, select all, type the text
//!   scroll <fraction>             scroll the open page (0 top, 1 bottom)
//!   shot <name>                   save <name>.png in the screenshot folder
//!   wait <ms>
//!   check <tag>                   fail unless the node was drawn
//!   page                          report the open page id and node count
use egui::{Event, Key, Modifiers, PointerButton, Pos2};

#[derive(Debug, Clone, PartialEq)]
pub enum Target {
    Tag(String),
    Label(String),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Where {
    Node(Target),
    Slider(Target, f64),
    Item(Target, usize),
}

#[derive(Debug, Clone, PartialEq)]
pub enum Op {
    Select(String),
    AwaitPanel(String),
    MoveTo(Where),
    Down,
    Up,
    Frames(u32),
    Key(Key),
    Text(String),
    Shot(String),
    Pause(u64),
    /// Scroll the open page to a fraction (0 top, 1 bottom) of its range.
    Scroll(f64),
    Check(Target),
    Report,
}

fn target(word: &str) -> Target {
    match word.strip_prefix("label:") {
        Some(l) => Target::Label(l.to_string()),
        None => Target::Tag(word.to_string()),
    }
}

fn click(w: Where) -> Vec<Op> {
    vec![
        Op::MoveTo(w),
        Op::Frames(1),
        Op::Down,
        Op::Up,
        Op::Frames(3),
    ]
}

pub fn parse(line: &str) -> Result<Vec<Op>, String> {
    let line = line.trim();
    let (cmd, rest) = line
        .split_once(' ')
        .map_or((line, ""), |(a, b)| (a, b.trim()));
    let mut words = rest.split_whitespace();
    let mut need = |what: &str| {
        words
            .next()
            .map(str::to_string)
            .ok_or_else(|| format!("{cmd}: missing {what}"))
    };
    Ok(match cmd {
        "select" => vec![Op::Select(need("panel id")?), Op::Frames(2)],
        "click" => click(Where::Node(target(&need("tag")?))),
        "slider" => {
            let t = target(&need("tag")?);
            let f: f64 = need("fraction")?
                .parse()
                .map_err(|_| "slider: fraction is not a number".to_string())?;
            click(Where::Slider(t, f.clamp(0.0, 1.0)))
        }
        "combo" => {
            let t = target(&need("tag")?);
            let i: usize = need("index")?
                .parse()
                .map_err(|_| "combo: index is not a number".to_string())?;
            let mut ops = click(Where::Node(t.clone()));
            ops.extend(click(Where::Item(t, i)));
            ops
        }
        "list" => {
            let t = target(&need("tag")?);
            let i: usize = need("index")?
                .parse()
                .map_err(|_| "list: index is not a number".to_string())?;
            click(Where::Item(t, i))
        }
        "text" => {
            let t = target(&need("tag")?);
            let text = rest.split_once(' ').map_or("", |(_, b)| b).to_string();
            let mut ops = click(Where::Node(t));
            ops.extend([
                Op::Key(Key::A),
                Op::Frames(1),
                Op::Text(text),
                Op::Frames(3),
            ]);
            ops
        }
        "scroll" => vec![Op::Scroll(
            need("fraction")?
                .parse::<f64>()
                .map_err(|_| "scroll: fraction is not a number".to_string())?
                .clamp(0.0, 1.0),
        )],
        "shot" => vec![Op::Shot(need("name")?)],
        "wait" => vec![Op::Pause(
            need("ms")?
                .parse()
                .map_err(|_| "wait: ms is not a number".to_string())?,
        )],
        "check" => vec![Op::Check(target(&need("tag")?))],
        "page" => vec![Op::Report],
        "" => return Err("empty step".into()),
        other => return Err(format!("unknown step '{other}'")),
    })
}

pub fn pointer_moved(p: Pos2) -> Event {
    Event::PointerMoved(p)
}

pub fn pointer_button(p: Pos2, pressed: bool) -> Event {
    Event::PointerButton {
        pos: p,
        button: PointerButton::Primary,
        pressed,
        modifiers: Modifiers::NONE,
    }
}

pub fn ctrl_key(key: Key) -> Vec<Event> {
    let m = Modifiers {
        ctrl: true,
        command: true,
        ..Default::default()
    };
    vec![
        Event::Key {
            key,
            physical_key: Some(key),
            pressed: true,
            repeat: false,
            modifiers: m,
        },
        Event::Key {
            key,
            physical_key: Some(key),
            pressed: false,
            repeat: false,
            modifiers: m,
        },
    ]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_steps() {
        assert_eq!(parse("select a.b").unwrap()[0], Op::Select("a.b".into()));
        let c = parse("click go").unwrap();
        assert_eq!(c[0], Op::MoveTo(Where::Node(Target::Tag("go".into()))));
        assert!(
            matches!(parse("slider s 0.25").unwrap()[0], Op::MoveTo(Where::Slider(_, f)) if f == 0.25)
        );
        assert_eq!(parse("combo c 2").unwrap().len(), 10);
        let t = parse("text name hello world").unwrap();
        assert!(t.contains(&Op::Text("hello world".into())));
        assert_eq!(
            parse("click label:Save").unwrap()[0],
            Op::MoveTo(Where::Node(Target::Label("Save".into())))
        );
    }

    #[test]
    fn refuses_bad_lines() {
        assert!(parse("").is_err());
        assert!(parse("jump").is_err());
        assert!(parse("slider s x").is_err());
        assert!(parse("click").is_err());
    }
}
