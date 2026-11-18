// SPDX-License-Identifier: GPL-3.0-only
//! Reads one panel blob (see `sco/widgets_view.h`) into owned Rust values. The blob is untrusted:
//! every offset, length, stride and string is checked, and a bad blob is refused as a whole.
use crate::ffi::*;

#[derive(Debug, Clone, PartialEq)]
pub struct Node {
    pub kind: u32,
    pub id: u32,
    pub parent: u32,
    pub flags: u32,
    pub index: i32,
    pub severity: u32,
    pub first_row: u32,
    pub visible_rows: u32,
    pub presentation: u32,
    pub columns: u32,
    pub value: f64,
    pub minimum: f64,
    pub maximum: f64,
    pub label: String,
    pub text: String,
    pub tooltip: String,
    pub tag: String,
    pub items: Vec<String>,
    pub key: u64,
}

impl Node {
    pub fn disabled(&self) -> bool {
        self.flags & WIDGET_DISABLED != 0
    }
    pub fn password(&self) -> bool {
        self.flags & WIDGET_PASSWORD != 0
    }
    /// Rows of a LIST or TABLE.
    pub fn rows(&self) -> usize {
        if self.kind == WIDGET_TABLE {
            if self.columns == 0 {
                0
            } else {
                self.items.len() / self.columns as usize
            }
        } else {
            self.items.len()
        }
    }
    /// Rows `[first, last)` shown; every row without paging.
    pub fn window(&self) -> (usize, usize) {
        let rows = self.rows();
        if self.visible_rows == 0 {
            (0, rows)
        } else {
            let first = (self.first_row as usize).min(rows);
            (first, rows.min(first + self.visible_rows as usize))
        }
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Panel {
    pub id: String,
    pub title: String,
    pub owner: String,
    pub revision: u64,
    pub epoch: u64,
    pub flags: u32,
    pub nodes: Vec<Node>,
}

#[derive(Debug, PartialEq, Eq)]
pub enum BlobError {
    Short,
    Magic,
    Version,
    Size(&'static str),
    Range(&'static str),
    Utf8,
    Tree(&'static str),
}

impl std::fmt::Display for BlobError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "bad panel blob: {self:?}")
    }
}

struct Reader<'a>(&'a [u8]);

impl Reader<'_> {
    fn u32(&self, at: usize) -> u32 {
        u32::from_le_bytes(self.0[at..at + 4].try_into().unwrap())
    }
    fn i32(&self, at: usize) -> i32 {
        i32::from_le_bytes(self.0[at..at + 4].try_into().unwrap())
    }
    fn u64(&self, at: usize) -> u64 {
        u64::from_le_bytes(self.0[at..at + 8].try_into().unwrap())
    }
    fn f64(&self, at: usize) -> f64 {
        f64::from_le_bytes(self.0[at..at + 8].try_into().unwrap())
    }
}

/// The blob is NUL-terminated inside, but the reader trusts only (offset, length) and checks both.
fn string(strings: &[u8], offset: u32, length: u32) -> Result<String, BlobError> {
    let (offset, length) = (offset as usize, length as usize);
    let end = offset
        .checked_add(length)
        .ok_or(BlobError::Range("string"))?;
    if end > strings.len() {
        return Err(BlobError::Range("string"));
    }
    std::str::from_utf8(&strings[offset..end])
        .map(str::to_owned)
        .map_err(|_| BlobError::Utf8)
}

pub fn parse(bytes: &[u8]) -> Result<Panel, BlobError> {
    if bytes.len() < HEADER_SIZE {
        return Err(BlobError::Short);
    }
    let r = Reader(bytes);
    if r.u32(4) != BLOB_MAGIC {
        return Err(BlobError::Magic);
    }
    if r.u32(8) >> 16 != WIDGETS_VIEW_VERSION_1_0 >> 16 {
        return Err(BlobError::Version);
    }
    let header_size = r.u32(0) as usize;
    if header_size < HEADER_SIZE || header_size > bytes.len() {
        return Err(BlobError::Size("header"));
    }
    let total = r.u32(12) as usize;
    if total > bytes.len() || total < header_size {
        return Err(BlobError::Range("total_bytes"));
    }
    let bytes = &bytes[..total];
    let r = Reader(bytes);
    let node_count = r.u32(32) as usize;
    let stride = r.u32(36) as usize;
    let (nodes_off, items_off, item_total) =
        (r.u32(40) as usize, r.u32(44) as usize, r.u32(48) as usize);
    let (strings_off, strings_len) = (r.u32(52) as usize, r.u32(56) as usize);
    if stride < NODE_SIZE {
        return Err(BlobError::Size("node stride"));
    }
    if node_count > 4096 || item_total > 4096 * 256 {
        return Err(BlobError::Range("counts"));
    }
    if nodes_off % 8 != 0 || items_off % 8 != 0 || strings_off % 8 != 0 {
        return Err(BlobError::Range("alignment"));
    }
    let nodes_end = node_count
        .checked_mul(stride)
        .and_then(|n| nodes_off.checked_add(n))
        .ok_or(BlobError::Range("nodes"))?;
    let items_end = items_off
        .checked_add(item_total * 8)
        .ok_or(BlobError::Range("items"))?;
    let strings_end = strings_off
        .checked_add(strings_len)
        .ok_or(BlobError::Range("strings"))?;
    if nodes_off < header_size || nodes_end > total || items_end > total || strings_end > total {
        return Err(BlobError::Range("section"));
    }
    // The three ranges must not overlap.
    let mut spans = [
        (nodes_off, nodes_end),
        (items_off, items_end),
        (strings_off, strings_end),
    ];
    spans.sort();
    if spans[0].1 > spans[1].0 || spans[1].1 > spans[2].0 {
        return Err(BlobError::Range("overlap"));
    }
    let strings = &bytes[strings_off..strings_end];
    let str_at = |at: usize| string(strings, r.u32(at), r.u32(at + 4));

    let mut items_table = Vec::with_capacity(item_total);
    for i in 0..item_total {
        let at = items_off + i * 8;
        items_table.push(string(strings, r.u32(at), r.u32(at + 4))?);
    }

    let mut nodes: Vec<Node> = Vec::with_capacity(node_count);
    for i in 0..node_count {
        let at = nodes_off + i * stride;
        let item_count = r.u32(at + 44) as usize;
        let first_item = r.u32(at + 48) as usize;
        let end = first_item
            .checked_add(item_count)
            .ok_or(BlobError::Range("items"))?;
        if end > items_table.len() {
            return Err(BlobError::Range("node items"));
        }
        let node = Node {
            kind: r.u32(at + 4),
            id: r.u32(at + 8),
            parent: r.u32(at + 12),
            flags: r.u32(at + 16),
            index: r.i32(at + 20),
            severity: r.u32(at + 24),
            first_row: r.u32(at + 28),
            visible_rows: r.u32(at + 32),
            presentation: r.u32(at + 36),
            columns: r.u32(at + 40),
            value: r.f64(at + 56),
            minimum: r.f64(at + 64),
            maximum: r.f64(at + 72),
            label: str_at(at + 80)?,
            text: str_at(at + 88)?,
            tooltip: str_at(at + 96)?,
            tag: str_at(at + 104)?,
            items: items_table[first_item..end].to_vec(),
            key: r.u64(at + 112),
        };
        // Preorder: the parent is 0 or an earlier ROW/COLUMN, ids are unique.
        if node.parent != 0 {
            let ok = nodes
                .iter()
                .any(|p| p.id == node.parent && (p.kind == WIDGET_ROW || p.kind == WIDGET_COLUMN));
            if !ok {
                return Err(BlobError::Tree("parent"));
            }
        }
        if nodes.iter().any(|p| p.id == node.id) {
            return Err(BlobError::Tree("duplicate id"));
        }
        nodes.push(node);
    }
    Ok(Panel {
        id: str_at(64)?,
        title: str_at(72)?,
        owner: str_at(80)?,
        revision: r.u64(16),
        epoch: r.u64(24),
        flags: r.u32(60),
        nodes,
    })
}

/// Fixed-size NUL-terminated field of a panel entry.
pub fn fixed_str(field: &[u8]) -> String {
    let end = field.iter().position(|&b| b == 0).unwrap_or(field.len());
    String::from_utf8_lossy(&field[..end]).into_owned()
}

/// Builds blobs by the layout in the header. Used by the fake host and the tests; the real producer
/// is sco-core.
pub mod build {
    use super::*;

    #[derive(Default, Clone)]
    pub struct NodeSpec {
        pub kind: u32,
        pub id: u32,
        pub parent: u32,
        pub flags: u32,
        pub index: i32,
        pub severity: u32,
        pub first_row: u32,
        pub visible_rows: u32,
        pub presentation: u32,
        pub columns: u32,
        pub value: f64,
        pub minimum: f64,
        pub maximum: f64,
        pub label: String,
        pub text: String,
        pub tooltip: String,
        pub tag: String,
        pub items: Vec<String>,
    }

    impl NodeSpec {
        pub fn password(&self) -> bool {
            self.flags & WIDGET_PASSWORD != 0
        }
    }

    /// FNV-1a as the header defines it for tagged and untagged nodes.
    pub fn node_key(n: &NodeSpec) -> u64 {
        let mut h: u64 = 0xcbf2_9ce4_8422_2325;
        let mut feed = |bytes: &[u8]| {
            for &b in bytes {
                h ^= u64::from(b);
                h = h.wrapping_mul(0x0000_0100_0000_01b3);
            }
        };
        if n.tag.is_empty() {
            feed(b"U");
            feed(&n.kind.to_le_bytes());
            feed(&n.id.to_le_bytes());
            feed(&n.parent.to_le_bytes());
            feed(n.label.as_bytes());
        } else {
            feed(b"T");
            feed(&n.kind.to_le_bytes());
            feed(n.tag.as_bytes());
        }
        if h == 0 {
            1
        } else {
            h
        }
    }

    struct Strings {
        bytes: Vec<u8>,
    }
    impl Strings {
        fn add(&mut self, s: &str) -> (u32, u32) {
            if s.is_empty() {
                return (0, 0);
            }
            let at = self.bytes.len() as u32;
            self.bytes.extend_from_slice(s.as_bytes());
            self.bytes.push(0);
            (at, s.len() as u32)
        }
    }

    fn put32(v: &mut [u8], at: usize, x: u32) {
        v[at..at + 4].copy_from_slice(&x.to_le_bytes());
    }
    fn put64(v: &mut [u8], at: usize, x: u64) {
        v[at..at + 8].copy_from_slice(&x.to_le_bytes());
    }
    fn putf(v: &mut [u8], at: usize, x: f64) {
        v[at..at + 8].copy_from_slice(&x.to_le_bytes());
    }
    fn putstr(v: &mut [u8], at: usize, s: (u32, u32)) {
        put32(v, at, s.0);
        put32(v, at + 4, s.1);
    }

    pub fn panel(
        id: &str,
        title: &str,
        owner: &str,
        revision: u64,
        epoch: u64,
        flags: u32,
        nodes: &[NodeSpec],
    ) -> Vec<u8> {
        // Offset 0 stays the empty string; the table is padded to a multiple of 8 at the end.
        let mut strings = Strings { bytes: vec![0] };
        let pid = strings.add(id);
        let ptitle = strings.add(title);
        let powner = strings.add(owner);
        let mut item_table: Vec<(u32, u32)> = Vec::new();
        let mut node_bytes = vec![0u8; nodes.len() * NODE_SIZE];
        for (i, n) in nodes.iter().enumerate() {
            let at = i * NODE_SIZE;
            let first_item = item_table.len() as u32;
            for it in &n.items {
                let s = strings.add(it);
                item_table.push(s);
            }
            let label = strings.add(&n.label);
            let text = if n.password() {
                (0, 0)
            } else {
                strings.add(&n.text)
            };
            let tooltip = strings.add(&n.tooltip);
            let tag = strings.add(&n.tag);
            let b = &mut node_bytes;
            put32(b, at, NODE_SIZE as u32);
            put32(b, at + 4, n.kind);
            put32(b, at + 8, n.id);
            put32(b, at + 12, n.parent);
            put32(b, at + 16, n.flags);
            put32(b, at + 20, n.index as u32);
            put32(b, at + 24, n.severity);
            put32(b, at + 28, n.first_row);
            put32(b, at + 32, n.visible_rows);
            put32(b, at + 36, n.presentation);
            put32(b, at + 40, n.columns);
            put32(b, at + 44, n.items.len() as u32);
            put32(b, at + 48, first_item);
            putf(b, at + 56, n.value);
            putf(b, at + 64, n.minimum);
            putf(b, at + 72, n.maximum);
            putstr(b, at + 80, label);
            putstr(b, at + 88, text);
            putstr(b, at + 96, tooltip);
            putstr(b, at + 104, tag);
            put64(b, at + 112, node_key(n));
        }
        let nodes_off = HEADER_SIZE;
        let items_off = nodes_off + node_bytes.len();
        let strings_off = items_off + item_table.len() * 8;
        let total = strings_off + strings.bytes.len();
        let mut out = vec![0u8; total];
        put32(&mut out, 0, HEADER_SIZE as u32);
        put32(&mut out, 4, BLOB_MAGIC);
        put32(&mut out, 8, WIDGETS_VIEW_VERSION_1_0);
        put32(&mut out, 12, total as u32);
        put64(&mut out, 16, revision);
        put64(&mut out, 24, epoch);
        put32(&mut out, 32, nodes.len() as u32);
        put32(&mut out, 36, NODE_SIZE as u32);
        put32(&mut out, 40, nodes_off as u32);
        put32(&mut out, 44, items_off as u32);
        put32(&mut out, 48, item_table.len() as u32);
        put32(&mut out, 52, strings_off as u32);
        put32(&mut out, 56, strings.bytes.len() as u32);
        put32(&mut out, 60, flags);
        putstr(&mut out, 64, pid);
        putstr(&mut out, 72, ptitle);
        putstr(&mut out, 80, powner);
        out[nodes_off..items_off].copy_from_slice(&node_bytes);
        for (i, s) in item_table.iter().enumerate() {
            putstr(&mut out, items_off + i * 8, *s);
        }
        out[strings_off..].copy_from_slice(&strings.bytes);
        out
    }
}

#[cfg(test)]
mod tests {
    use super::build::*;
    use super::*;

    fn sample() -> Vec<NodeSpec> {
        vec![
            NodeSpec {
                kind: WIDGET_COLUMN,
                id: 1,
                label: "Group".into(),
                ..Default::default()
            },
            NodeSpec {
                kind: WIDGET_SLIDER,
                id: 2,
                parent: 1,
                label: "Speed".into(),
                value: 5.0,
                maximum: 10.0,
                tag: "speed".into(),
                ..Default::default()
            },
            NodeSpec {
                kind: WIDGET_COMBO,
                id: 3,
                parent: 1,
                label: "Pick".into(),
                items: vec!["a".into(), "b\u{e9}".into()],
                index: 1,
                ..Default::default()
            },
            NodeSpec {
                kind: WIDGET_TABLE,
                id: 4,
                columns: 2,
                items: ["a", "b", "c", "d", "e", "f"].map(String::from).to_vec(),
                visible_rows: 2,
                first_row: 1,
                ..Default::default()
            },
        ]
    }

    #[test]
    fn round_trip() {
        let bytes = panel(
            "p.main",
            "Main",
            "owner",
            7,
            3,
            PANEL_TAGGED_EVENTS,
            &sample(),
        );
        let p = parse(&bytes).unwrap();
        assert_eq!(
            (p.id.as_str(), p.title.as_str(), p.owner.as_str()),
            ("p.main", "Main", "owner")
        );
        assert_eq!((p.revision, p.epoch, p.flags), (7, 3, PANEL_TAGGED_EVENTS));
        assert_eq!(p.nodes.len(), 4);
        assert_eq!(p.nodes[1].tag, "speed");
        assert_eq!(p.nodes[2].items, vec!["a", "b\u{e9}"]);
        assert_eq!(p.nodes[3].rows(), 3);
        assert_eq!(p.nodes[3].window(), (1, 3));
        assert_ne!(p.nodes[1].key, 0);
    }

    #[test]
    fn refuses_bad_blobs() {
        let good = panel("p.main", "Main", "owner", 7, 3, 0, &sample());
        assert_eq!(parse(&good[..40]), Err(BlobError::Short));
        let mut bad = good.clone();
        bad[4] ^= 1;
        assert_eq!(parse(&bad), Err(BlobError::Magic));
        // A string range pointing past the table.
        let mut bad = good.clone();
        bad[64 + 4..64 + 8].copy_from_slice(&9999u32.to_le_bytes());
        assert!(matches!(parse(&bad), Err(BlobError::Range(_))));
        // Total larger than the buffer.
        let mut bad = good.clone();
        bad[12..16].copy_from_slice(&(good.len() as u32 + 8).to_le_bytes());
        assert!(matches!(parse(&bad), Err(BlobError::Range(_))));
        // Node stride below the 1.0 size.
        let mut bad = good.clone();
        bad[36..40].copy_from_slice(&16u32.to_le_bytes());
        assert!(matches!(parse(&bad), Err(BlobError::Size(_))));
        // Invalid UTF-8 in the string table.
        let mut bad = good.clone();
        let strings_off = u32::from_le_bytes(good[52..56].try_into().unwrap()) as usize;
        bad[strings_off + 1] = 0xff;
        assert_eq!(parse(&bad), Err(BlobError::Utf8));
        // Node items beyond the item table.
        let mut bad = good.clone();
        let at = HEADER_SIZE + 2 * NODE_SIZE;
        bad[at + 44..at + 48].copy_from_slice(&500u32.to_le_bytes());
        assert!(matches!(parse(&bad), Err(BlobError::Range(_))));
    }

    #[test]
    fn refuses_a_broken_tree() {
        let mut nodes = sample();
        nodes[1].parent = 9;
        assert_eq!(
            parse(&panel("p", "t", "o", 1, 1, 0, &nodes)),
            Err(BlobError::Tree("parent"))
        );
        let mut nodes = sample();
        nodes[2].id = 2;
        assert_eq!(
            parse(&panel("p", "t", "o", 1, 1, 0, &nodes)),
            Err(BlobError::Tree("duplicate id"))
        );
    }

    #[test]
    fn password_text_is_not_in_the_blob() {
        let nodes = vec![NodeSpec {
            kind: WIDGET_TEXT_INPUT,
            id: 1,
            flags: WIDGET_PASSWORD,
            text: "secret".into(),
            ..Default::default()
        }];
        let bytes = panel("p", "t", "o", 1, 1, 0, &nodes);
        assert!(!bytes.windows(6).any(|w| w == b"secret"));
        assert_eq!(parse(&bytes).unwrap().nodes[0].text, "");
    }
}
