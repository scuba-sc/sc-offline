// SPDX-License-Identifier: GPL-3.0-only
//! Reads the panel dumps that the plugin workflow tests write when `SCO_PANEL_DUMP=<file>` is set
//! (`tools/panel_dump.h`, the same files `tools/menu_preview/run.py` draws). Each record is one state
//! of one panel as the plugin published it. The dump has no node tags, so the reader gives every node
//! the tag `n<id>`, which the test driver can click. Used by the test host and the tests; nothing in
//! the in-game path reads a dump.
//!
//! Format (UTF-8, tab separated, `\\ \t \n \r` escaped):
//!   panel <seq> <id> <title> <node count>
//!   node <kind> <id> <parent> <flags> <index> <value> <min> <max> <columns> <severity> <first_row>
//!        <visible_rows> <presentation> <label> <text> <tooltip> <items separated by \x1f>
use crate::blob::build::NodeSpec;

#[derive(Debug, Clone)]
pub struct DumpPanel {
    pub seq: u64,
    pub id: String,
    pub title: String,
    pub nodes: Vec<NodeSpec>,
}

fn unescape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut it = s.chars();
    while let Some(c) = it.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match it.next() {
            Some('t') => out.push('\t'),
            Some('n') => out.push('\n'),
            Some('r') => out.push('\r'),
            Some('\\') => out.push('\\'),
            Some(o) => {
                out.push('\\');
                out.push(o);
            }
            None => out.push('\\'),
        }
    }
    out
}

pub fn parse(text: &str) -> Result<Vec<DumpPanel>, String> {
    let mut out: Vec<DumpPanel> = Vec::new();
    for (no, line) in text.lines().enumerate() {
        let f: Vec<&str> = line.split('\t').collect();
        match f.first().copied() {
            Some("panel") => {
                if f.len() != 5 {
                    return Err(format!("line {}: a panel record has 5 fields", no + 1));
                }
                out.push(DumpPanel {
                    seq: f[1].parse().map_err(|_| format!("line {}: seq", no + 1))?,
                    id: unescape(f[2]),
                    title: unescape(f[3]),
                    nodes: Vec::new(),
                });
            }
            Some("node") => {
                if f.len() != 18 {
                    return Err(format!("line {}: a node record has 18 fields", no + 1));
                }
                let p = out
                    .last_mut()
                    .ok_or_else(|| format!("line {}: node before panel", no + 1))?;
                let u = |i: usize| -> Result<u32, String> {
                    f[i].parse()
                        .map_err(|_| format!("line {}: field {i}", no + 1))
                };
                let d = |i: usize| -> Result<f64, String> {
                    f[i].parse()
                        .map_err(|_| format!("line {}: field {i}", no + 1))
                };
                let id = u(2)?;
                p.nodes.push(NodeSpec {
                    kind: u(1)?,
                    id,
                    parent: u(3)?,
                    flags: u(4)?,
                    index: f[5]
                        .parse()
                        .map_err(|_| format!("line {}: index", no + 1))?,
                    value: d(6)?,
                    minimum: d(7)?,
                    maximum: d(8)?,
                    columns: u(9)?,
                    severity: u(10)?,
                    first_row: u(11)?,
                    visible_rows: u(12)?,
                    presentation: u(13)?,
                    label: unescape(f[14]),
                    text: unescape(f[15]),
                    tooltip: unescape(f[16]),
                    items: if f[17].is_empty() {
                        Vec::new()
                    } else {
                        f[17].split('\u{1f}').map(unescape).collect()
                    },
                    tag: format!("n{id}"),
                });
            }
            _ if line.is_empty() => {}
            _ => return Err(format!("line {}: unknown record", no + 1)),
        }
    }
    Ok(out)
}

/// The last published state of every panel, in the order each panel first appeared.
pub fn last_states(all: &[DumpPanel]) -> Vec<DumpPanel> {
    let mut order: Vec<&str> = Vec::new();
    for p in all {
        if !order.contains(&p.id.as_str()) {
            order.push(&p.id);
        }
    }
    order
        .into_iter()
        .filter_map(|id| all.iter().rev().find(|p| p.id == id).cloned())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ffi::*;

    #[test]
    fn parses_one_state_and_keeps_the_last() {
        let text = "panel\t1\ta.one\tOne\t2\nnode\t10\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\tHead\t\t\t\nnode\t5\t2\t1\t0\t-1\t0\t0\t0\t0\t0\t0\t0\t0\tPick\t\ttip\\tx\tA\x1fB\\n\npanel\t2\ta.one\tOne\t1\nnode\t1\t1\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\tOnly\t\t\t\n";
        let all = parse(text).unwrap();
        assert_eq!(all.len(), 2);
        assert_eq!(all[0].nodes[1].kind, WIDGET_COMBO);
        assert_eq!(all[0].nodes[1].items, ["A", "B\n"]);
        assert_eq!(all[0].nodes[1].tooltip, "tip\tx");
        assert_eq!(all[0].nodes[1].tag, "n2");
        let last = last_states(&all);
        assert_eq!(last.len(), 1);
        assert_eq!(last[0].nodes.len(), 1);
    }

    #[test]
    fn refuses_bad_records() {
        assert!(parse("node\t1\n").is_err());
        assert!(parse("panel\t1\tx\n").is_err());
        assert!(parse("junk\n").is_err());
    }
}
