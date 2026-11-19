// SPDX-License-Identifier: GPL-3.0-only
//! Synthetic panels for the Rust test host and the tests. Shapes follow the plugin workflow tests
//! (a loadout page, a spawn list, a settings form): every node kind, severity, tooltips, a disabled
//! control, number, integer and percent sliders, a paged list and a paged table.
use crate::blob::build::NodeSpec;
use crate::ffi::*;

fn node(kind: u32, id: u32, parent: u32, label: &str, tag: &str) -> NodeSpec {
    NodeSpec {
        kind,
        id,
        parent,
        label: label.into(),
        tag: tag.into(),
        ..Default::default()
    }
}

pub fn controls() -> Vec<NodeSpec> {
    let mut n = vec![
        node(WIDGET_COLUMN, 1, 0, "Flight", ""),
        node(
            WIDGET_LABEL,
            2,
            1,
            "Tune how the ship handles. Changes apply at once.",
            "",
        ),
        NodeSpec {
            value: 1.0,
            ..node(WIDGET_TOGGLE, 3, 1, "Flight assist", "flight.assist")
        },
        NodeSpec {
            minimum: 0.0,
            maximum: 2.0,
            value: 0.5,
            tooltip: "Scales every thruster.".into(),
            ..node(WIDGET_SLIDER, 4, 1, "Thruster gain", "flight.gain")
        },
        NodeSpec {
            minimum: 1.0,
            maximum: 10.0,
            value: 3.0,
            presentation: NUMERIC_INTEGER,
            ..node(WIDGET_SLIDER, 5, 1, "Seats", "flight.seats")
        },
        NodeSpec {
            minimum: 20.0,
            maximum: 95.0,
            value: 60.0,
            presentation: NUMERIC_INTEGER_PERCENT,
            ..node(WIDGET_SLIDER, 6, 1, "Mix", "flight.mix")
        },
        NodeSpec {
            items: ["Arcade", "Simulation", "Hover"].map(String::from).to_vec(),
            index: 0,
            ..node(WIDGET_COMBO, 7, 1, "Mode", "flight.mode")
        },
        NodeSpec {
            text: "Rook".into(),
            ..node(WIDGET_TEXT_INPUT, 8, 1, "Callsign", "flight.name")
        },
        NodeSpec {
            flags: WIDGET_PASSWORD,
            text: "hunter2".into(),
            ..node(WIDGET_TEXT_INPUT, 9, 1, "Access code", "flight.code")
        },
        node(WIDGET_ROW, 10, 1, "", ""),
        node(WIDGET_BUTTON, 11, 10, "Reset to defaults", "flight.reset"),
        NodeSpec {
            flags: WIDGET_DISABLED,
            tooltip: "Not available in this build.".into(),
            ..node(WIDGET_BUTTON, 12, 10, "Calibrate", "flight.calibrate")
        },
        NodeSpec {
            flags: WIDGET_DISABLED,
            minimum: 0.0,
            maximum: 10.0,
            value: 4.0,
            ..node(WIDGET_SLIDER, 13, 1, "Locked value", "flight.locked")
        },
    ];
    n.push(NodeSpec {
        severity: SEVERITY_INFO,
        ..node(
            WIDGET_LABEL,
            20,
            0,
            "Info lines show a prefix as well as a colour.",
            "",
        )
    });
    n.push(NodeSpec {
        severity: SEVERITY_WARNING,
        ..node(WIDGET_LABEL, 21, 0, "The engine must be off.", "")
    });
    n.push(NodeSpec {
        severity: SEVERITY_ERROR,
        ..node(WIDGET_LABEL, 22, 0, "The last request failed.", "")
    });
    n
}

pub fn lists() -> Vec<NodeSpec> {
    let ships: Vec<String> = [
        "Aurora MR",
        "Avenger Titan",
        "Cutlass Black",
        "Freelancer",
        "Gladius",
        "Hornet F7C",
        "Mustang Alpha",
        "Nomad",
    ]
    .iter()
    .map(|s| s.to_string())
    .collect();
    let cells: Vec<String> = (0..6)
        .flat_map(|r| {
            [
                format!("Row {r}"),
                format!("{}", r * 11),
                if r % 2 == 0 {
                    "ready".into()
                } else {
                    "busy".into()
                },
            ]
        })
        .collect();
    vec![
        NodeSpec {
            items: ships,
            index: 1,
            visible_rows: 4,
            ..node(WIDGET_LIST, 1, 0, "Your ships", "list.ships")
        },
        NodeSpec {
            items: cells,
            columns: 3,
            visible_rows: 3,
            ..node(WIDGET_TABLE, 2, 0, "Status table", "table.stats")
        },
        node(WIDGET_ROW, 3, 0, "", ""),
        node(WIDGET_COLUMN, 4, 3, "Left column", ""),
        node(WIDGET_LABEL, 5, 4, "Two columns share the row.", ""),
        node(WIDGET_COLUMN, 6, 3, "Right column", ""),
        NodeSpec {
            value: 0.0,
            ..node(WIDGET_TOGGLE, 7, 6, "Show details", "cols.details")
        },
    ]
}

/// The host's `menu.main` panel as `src/menu_widgets.cpp` publishes it with no background image.
pub fn host_main() -> Vec<NodeSpec> {
    vec![
        node(WIDGET_LABEL, 10, 0, "Background", ""),
        node(
            WIDGET_LABEL,
            11,
            0,
            "To use a background image, save it as menu_background.png (or .jpg) here, then restart the game:",
            "",
        ),
        node(WIDGET_LABEL, 100, 0, r"data\menu_background.png", ""),
        node(WIDGET_LABEL, 20, 0, "About", ""),
        node(
            WIDGET_LABEL,
            21,
            0,
            "sc-offline is a work in progress, based on ChrisWareOffline 0.9.0-rc1.",
            "",
        ),
        node(
            WIDGET_LABEL,
            22,
            0,
            "Bug reports: github.com/scuba-sc/sc-offline/issues",
            "",
        ),
    ]
}

/// The host's `menu.status` panel: one or two label lines.
pub fn host_status() -> Vec<NodeSpec> {
    vec![node(
        WIDGET_LABEL,
        1,
        0,
        "Game found. Mods: 9 loaded, 0 failed. Core 2.4.1.",
        "",
    )]
}

/// Shapes the number, label and ellipsis rules are about: whole and fractional sliders, an empty
/// label that must draw no blank line, a long label and a long list row that are cut with an ellipsis.
pub fn shapes() -> Vec<NodeSpec> {
    let long = "A very long label that does not fit in the label column of this page and so is cut with an ellipsis";
    vec![
        node(WIDGET_COLUMN, 1, 0, "Numbers", ""),
        NodeSpec {
            minimum: 0.0,
            maximum: 100.0,
            value: 30.0,
            ..node(WIDGET_SLIDER, 2, 1, "Whole number", "num.whole")
        },
        NodeSpec {
            minimum: 0.0,
            maximum: 1.0,
            value: 0.125,
            ..node(WIDGET_SLIDER, 3, 1, "Three decimals", "num.three")
        },
        NodeSpec {
            minimum: 0.0,
            maximum: 3.0,
            value: 1.23456,
            ..node(WIDGET_SLIDER, 4, 1, "Rounded to three", "num.round")
        },
        NodeSpec {
            minimum: 0.0,
            maximum: 10.0,
            value: 4.0,
            presentation: NUMERIC_INTEGER,
            ..node(WIDGET_SLIDER, 5, 1, "Integer", "num.int")
        },
        NodeSpec {
            minimum: 0.0,
            maximum: 100.0,
            value: 45.0,
            presentation: NUMERIC_INTEGER_PERCENT,
            ..node(WIDGET_SLIDER, 6, 1, "Percent", "num.pct")
        },
        node(WIDGET_LABEL, 7, 0, "", ""),
        node(
            WIDGET_LABEL,
            8,
            0,
            "The empty label above draws no blank line.",
            "",
        ),
        NodeSpec {
            value: 1.0,
            tooltip: "The whole label is in this tooltip.".into(),
            ..node(WIDGET_TOGGLE, 9, 0, long, "long.toggle")
        },
        NodeSpec {
            items: vec![
                "Short".into(),
                long.into(),
                "Another quite long entry that is cut at the edge of the list".into(),
            ],
            index: 0,
            ..node(WIDGET_LIST, 10, 0, "Long rows", "long.list")
        },
    ]
}
