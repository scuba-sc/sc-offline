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
