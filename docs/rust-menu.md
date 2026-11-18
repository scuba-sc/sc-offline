# Rust in-game menu: design

Status: design proposal for owner review. Nothing in this page is built. Nothing is claimed to work in game. Every acceptance check below is source, build, synthetic or CI evidence. Docs keep the words "not run in game" until the owner records a session.

This page belongs to the [Rust launcher plan](rust-launcher.md). It covers owner decision 1 of 2026-11-17: the in-game menu is rebuilt in Rust now, reads `sco.widgets` panels through a new versioned C interface in sco-core, and the in-game host (dinput8.dll, hooks, Core host kit) stays C++. Decision 2 applies: sco-core 2.4.1 is held until the Rust launcher, Rust menu, catalogue and reference launcher are in it. So the new interface below is a 2.4.1 item, not a later one.

## Owner decisions applied (2026-11-17)

| Decision | How this page follows it |
|---|---|
| 1. Rust is the launcher and the in-game menu, both now. The host (dinput8.dll, hooks, Core host kit) stays C++. The Rust menu reads `sco.widgets` through a new versioned C interface in sco-core. | Section 2 is that interface. Section 3 is the menu. Nothing in the host moves to Rust. |
| 2. sco-core 2.4.1 is held until the Rust launcher, Rust menu, catalogue and reference launcher are in it. | Nothing here is "after 2.4.1". The interface (M1) and the menu (M2 to M8) are 2.4.1 work. The release job stays blocked until M8 is recorded. |
| 3. Catalogue: signed index, disabled on pull, native-code warning. | Not changed by this page. Plugin pages from a pulled plugin appear in the menu only after the player enables it. |
| 4. Capability switches are on by default in every release package. Each is a visible setting the player can turn off. Its text says it has not been run in game. | The menu never defaults a switch off and never calls one experimental. Section 3.10 adds a read-only readout. Changes are made in the launcher and apply at the next Play. |

## 0. Summary

1. sco-core gets a new host-side reader, `sco.widgets.view` 1.0 (section 2). It lists panels, copies one panel as a flat, relocatable byte blob (nodes, items and strings with offsets, no pointers), queues one event, and exposes revision counters. It is not a plugin service: only the host menu identity can get it.
2. The menu is a Rust cdylib, `scom.dll`, loaded by dinput8.dll. It owns its own window and thread and draws with egui through a small in-tree Direct3D 11 painter (section 3). A separate-process menu over shared memory is the documented fallback if the spike fails.
3. The old ImGui menu code stays compiled. It is the fallback when `scom.dll` is missing or refused, and it hosts the pages of plugins that still draw with `sco.ui` and ImGui (section 4).
4. Tests do not need the game: a blob fixture suite shared between C++ and Rust, golden PNGs from synthetic panel dumps through the same preview flow the ImGui menu already has, and synthetic window messages (section 5).
5. Eight phases, M0 to M8. Everything in game stays owner-run (section 6).

## 1. How the menu works today

Read from the worktree sources. The sidebar shell is on branch `feature/menu-player-organization` (`src/menu_shell.cpp`, `src/menu_navigation.h`, `tools/menu_preview/`). The `feature/rust-launcher` branch still has the older tab-bar shell in `src/menu.cpp`. The Rust menu targets the sidebar shell, so M2 starts only after that branch is merged or rebased in.

| Part | Today |
|---|---|
| Window | `menu.cpp`: a popup, thick-frame, topmost, tool window (no taskbar button), class `starcitzenofflinemods_menu`, created on its own menu thread. `M` is polled with `GetAsyncKeyState`. Esc, `M` and `WM_CLOSE` hide it and give focus back to the game. |
| Device | Its own D3D11 device and swap chain (hardware, then WARP). No Present hook in the game. `Present(1, 0)`. |
| Size | `menu_window.h`: outer size saved at 96 dpi in `menu-window.ini`, clamped to 720 x 520 minimum and 90 percent of the work area, scaled per monitor with `GetDpiForWindow`. |
| Cursor | `ClipCursor` to the window while it is foreground. Released on size/move, hide and focus loss. ImGui draws its own cursor (`MouseDrawCursor`). |
| Frame | ImGui draws run on the **game thread** only, because `sco.ui` draws and `sco.widgets` rendering are game-thread calls. The menu thread asks for a frame (`menu_frame_protocol.h`: Idle, Wanted, Building, Built, Cancelled), nudges the game with `WM_NULL`, and presents the built draw data. It never blocks on the game. |
| Pages | `menu_navigation.h`: pages come from `sco.ui` tabs, `sco.widgets` panels and the menu's own Settings page. Page id decides section, title, summary and rank. Developer pages are hidden unless `menu-view.ini` says `developer=1`. |
| Panels | Core's `ImGuiRenderer` (`sco/widgets_imgui.h`, `src/widgets/imgui_adapter.cpp`, layout rules in `imgui_layout.h`) draws a copied `sco::widgets::Panel`. Events are numeric candidates that Core validates against the live node before it calls the owner. |
| Host panels | With `SCO_HOST_MENU_CLIENT`, `menu.main` (background settings, About) and `menu.status` are panels owned by a reserved host identity (`sco/host_menu.h`). |
| Look | `MenuApplyTheme()`: soil green, leaf-green accent, Bahnschrift then Segoe UI from the Windows font folder at 17 px, headings 20 px. Optional `menu_background.png/.jpg` decoded with WIC under a green wash. |
| Lifetime | The module is pinned with `GetModuleHandleExW` and never freed after exposure (ProcessRetained). `Menu_RequestStop` / `Menu_Drain` close frame admission and join the worker. |

Core facts the design relies on (sco-core `4ba313d`, SDK 2.4.1-dev.6):

- `sco::widgets::Panels()`, `RenderPanel()` and every `sco_widgets_v1` call are game-thread only. `Panels()` returns nothing off-thread.
- Each panel entry holds a shared immutable snapshot, rebuilt lazily (at most once per change) and a `generation` that changes on structural change (replace, remove). `RenderPanel` drops the rest of a pass when the generation moved.
- A panel enters tagged-event mode (1.4) once any node has a tag. Node tags (1.2) are copied into snapshots but never rendered. They are the automation handle used in section 5.
- Limits: 64 panels, 256 nodes per `set_panel`, 4096 nodes total, 256 items, 16 columns, 255 bytes per text, 63 bytes per id and title, 256 events per pass.

## 2. The new sco-core C interface: `sco.widgets.view` 1.0

### 2.1 Why a new interface

The Rust menu runs on its own thread and cannot link the C++ host kit (STL types, no C ABI). It needs the same data `ImGuiRenderer` gets, as C. Three design choices follow.

1. **Flat blob, offsets not pointers.** One panel is copied into one caller-owned buffer. All strings are `(offset, length)` pairs into a string table in the same buffer. No pointer into plugin or Core memory ever reaches the menu. The blob is relocatable: it can be written to a file (fixtures), compared byte for byte, or sent over shared memory (fallback shape B) unchanged.
2. **Reads off the game thread, event dispatch on the game thread.** In 1.0 the read calls are specified for the menu thread only (`list_panels`, `copy_panel`, `submit_event`). "Any non-game thread" is proposed and is not promised until the M1 race test passes (2.4). Widening a thread rule later is compatible; narrowing it is not, so 1.0 starts narrow. `capabilities` and `stamps` touch only atomics and stay any-thread. Reading takes the widget lock for a pointer copy of the shared snapshot, then flattens outside plugin code. It runs no plugin callback. Events are queued by the menu thread and dispatched by the host on the game thread, through the same validation and callout guard as `RenderPanel`. This removes the per-frame game-thread hand-off for panels: the menu stays responsive when the game thread is busy.
3. **Not a plugin service.** If `query_service("sco.widgets.view")` worked, any plugin could read every other plugin's panels and inject player clicks into them. So the table is never published through `query_service`. The host gets it from the reserved host-menu identity (section 2.5). A test pins that a plugin's `query_service` answers `SCO_NOT_FOUND`.

The header is a host-source seam. Recommendation: ship it in the host-source kit only, not in the plugin SDK kit, so it is not a public plugin promise. Owner decision (section 7).

### 2.2 Proposed header text

File `include/sco_widgets_view.h` in sco-core. This is the exact proposed text.

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * sco_widgets_view.h: the host-side flat reader of "sco.widgets" panels, version 1.0.
 *
 * For the host's own menu only. It is NOT a plugin service and is never returned by
 * query_service. The host obtains it through a host-only entry point and may hand it to a
 * menu renderer written in any language. No renderer type, plugin pointer or Core pointer
 * crosses this interface.
 *
 * Rules (the rules of sco_api.h hold here too: 64-bit only, 4-byte enums, results not
 * exceptions):
 *  - All integers are little endian. Every struct is 8-byte aligned and pinned by
 *    tests/abi_widgets_view.c. Every size field is the size the producer built; a reader
 *    accepts a larger size and ignores the tail, and refuses a smaller one.
 *  - Thread rules, per function, are stated below. Reads run no plugin code, take no
 *    plugin lock and are bounded by the widget limits. dispatch_events is the only call
 *    that runs plugin callbacks.
 *  - Every call takes the Core runtime lease, as RenderPanel does, and answers
 *    SCO_UNAVAILABLE once stop has begun. The renderer's thread may still be running when
 *    the client's stop returns (the host joins it later), so a call after stop is expected
 *    and safe: it answers SCO_UNAVAILABLE.
 *  - Both the function table and the client handle are static for the life of the process.
 *    Their memory is never freed or reused, and the client is marked stopped, never torn
 *    down. A renderer may keep either pointer past stop.
 *  - Strings are UTF-8 and NUL-terminated inside the blob. A reader must still validate
 *    offsets, lengths and UTF-8, and must not trust the blob.
 */
#ifndef SCO_WIDGETS_VIEW_H
#define SCO_WIDGETS_VIEW_H
#include "sco_widgets.h"
#ifdef __cplusplus
extern "C" {
#endif

/* There is no service name: the table is never published by query_service. */
#define SCO_WIDGETS_VIEW_VERSION_1_0 0x00010000u

/* "SCVB" read as a little-endian u32. */
#define SCO_VIEW_BLOB_MAGIC 0x42564353u
#define SCO_VIEW_ID_BYTES 64u /* fixed id, title and owner fields, NUL included */

/* Opaque. One per host menu instance. */
typedef struct sco_view_client sco_view_client;

/* A string inside a blob: byte offset from strings_offset, and length without the NUL.
 * {0, 0} is the empty string. */
typedef struct sco_view_str {
    uint32_t offset;
    uint32_t length;
} sco_view_str;

/* ---- panel list ---- */
#define SCO_VIEW_PANEL_HOST_OWNED 0x1u    /* a panel owned by the host's own reserved identity */
#define SCO_VIEW_PANEL_TAGGED_EVENTS 0x2u /* the panel is in tagged-event mode (widgets 1.4) */
typedef struct sco_view_panel_entry {
    uint32_t size;       /* sizeof(sco_view_panel_entry) as the host built it: 224 */
    uint32_t flags;      /* SCO_VIEW_PANEL_* */
    uint64_t revision;   /* see "Counters" */
    uint64_t epoch;      /* see "Counters": changes only when the panel is replaced */
    uint32_t node_count;
    uint32_t _reserved;  /* zero */
    char id[SCO_VIEW_ID_BYTES];    /* panel id, NUL-terminated */
    char title[SCO_VIEW_ID_BYTES]; /* panel title, NUL-terminated */
    char owner[SCO_VIEW_ID_BYTES]; /* owning plugin id, NUL-terminated; "menu" for host panels */
} sco_view_panel_entry;

/* ---- one panel as a blob ----
 * Layout, from the start of the buffer:
 *   sco_view_blob_header                       (header.size bytes)
 *   node_count x sco_view_node                 (at nodes_offset, node_stride apart)
 *   item_total x sco_view_str                  (at items_offset)
 *   string table, strings_bytes long           (at strings_offset)
 * nodes_offset, items_offset and strings_offset are each a multiple of 8, and the three
 * ranges lie inside total_bytes without overlapping; a reader refuses the blob otherwise.
 * Nodes are in preorder, exactly the tree the plugin submitted (ids unique, parent 0 or an
 * earlier ROW/COLUMN). A node's items are item_count consecutive entries of the item table
 * starting at first_item: COMBO/LIST options, or TABLE cells row-major. All other fields
 * keep the meaning they have in sco_widget_node and its 1.1, 1.2 and 1.3 extensions. */
typedef struct sco_view_blob_header {
    uint32_t size;          /* sizeof(sco_view_blob_header): 96 */
    uint32_t magic;         /* SCO_VIEW_BLOB_MAGIC */
    uint32_t version;       /* SCO_WIDGETS_VIEW_VERSION_1_0 */
    uint32_t total_bytes;   /* bytes used in the buffer */
    uint64_t revision;
    uint64_t epoch;
    uint32_t node_count;
    uint32_t node_stride;   /* sizeof(sco_view_node): 112 */
    uint32_t nodes_offset;
    uint32_t items_offset;
    uint32_t item_total;
    uint32_t strings_offset;
    uint32_t strings_bytes;
    uint32_t flags;         /* SCO_VIEW_PANEL_* */
    sco_view_str panel_id;
    sco_view_str title;
    sco_view_str owner;
    uint32_t _reserved[2];  /* zero */
} sco_view_blob_header;

typedef struct sco_view_node {
    uint32_t size;          /* 112 */
    uint32_t kind;          /* SCO_WIDGET_* */
    uint32_t id;
    uint32_t parent;
    uint32_t flags;         /* SCO_WIDGET_DISABLED, SCO_WIDGET_PASSWORD */
    int32_t index;
    uint32_t severity;      /* SCO_WIDGET_SEVERITY_*; 0 for a 1.0 node */
    uint32_t first_row;     /* paging, LIST and TABLE only */
    uint32_t visible_rows;  /* 0 = no paging */
    uint32_t presentation;  /* SCO_WIDGET_NUMERIC_* */
    uint32_t columns;       /* TABLE only */
    uint32_t item_count;
    uint32_t first_item;    /* index into the item table */
    uint32_t _reserved;     /* zero */
    double value;
    double minimum;
    double maximum;
    sco_view_str label;
    sco_view_str text;
    sco_view_str tooltip;
    sco_view_str tag;       /* widgets 1.2 stable tag, empty when untagged */
} sco_view_node;

/* ---- one event, from the menu to a plugin ----
 * The same candidate a renderer gives RenderPanel, plus the epoch of the copy the player
 * acted on and, for a tagged node, its tag. Matching at dispatch:
 *  - epoch equal: the node is checked against the live tree (same id, same kind, enabled
 *    including its ancestors). If `tag` is not NULL the live node's tag must equal it, so
 *    a plugin that removed a node and added another under the same id cannot receive the
 *    click. A plugin changing other nodes does not drop the event.
 *  - epoch different (set_panel replaced the panel), `tag` not NULL, and the live panel is
 *    in tagged-event mode: the event is matched by tag instead of id. The live node with
 *    that tag must have the same kind and be enabled including its ancestors; the event
 *    is then delivered with the live node's id, and its value, selection or row is
 *    validated against the live node. No match: dropped and counted.
 *  - epoch different and `tag` NULL (or the panel is not tagged): dropped and counted.
 * The host validates the rest exactly as RenderPanel does (value in range, text <= 255
 * bytes and valid UTF-8, page row in range). The callback tag is always copied from the
 * live node, never taken from the menu. */
typedef struct sco_view_event {
    uint32_t size;      /* 56 */
    uint32_t node_id;
    uint32_t kind;      /* the node's kind, or SCO_WIDGET_EVENT_PAGE */
    int32_t index;      /* COMBO/LIST selection, else 0 */
    double value;       /* TOGGLE/SLIDER, else 0 */
    const char* text;   /* TEXT_INPUT only: UTF-8, NUL-terminated, copied during the call; else NULL.
                           The one non-relocatable field (64-bit hosts only). */
    int32_t row;        /* PAGE: the requested first row; else -1 */
    uint32_t _reserved; /* zero */
    uint64_t epoch;     /* the epoch of the blob the player acted on. 0 is invalid:
                           submit_event answers SCO_BAD_ARG (a real epoch is never 0) */
    const char* tag;    /* the tag of the target node in that blob, UTF-8, NUL-terminated,
                           at most SCO_WIDGETS_CAP_MAX_TAG bytes, copied during the call;
                           NULL for an untagged node. 64-bit hosts only. */
} sco_view_event;

/* capabilities(): caps[SCO_VIEW_CAP_COUNT] is the array length on entry (>= 2) and the
 * number written on return, as in sco.widgets: the host writes min(length, the entries it
 * has) and the caller reads only the entries counted. A caller with a short array gets a
 * prefix, never an error. Size negotiation: a consumer checks table `version` and `size`,
 * blob `version` and these counts; there is no minimum-version argument. */
#define SCO_VIEW_CAP_COUNT 0u
#define SCO_VIEW_CAP_FEATURES 1u
#define SCO_VIEW_CAP_MAX_PANELS 2u
#define SCO_VIEW_CAP_EVENT_QUEUE 3u  /* events the queue holds before submit_event answers SCO_TOO_MANY */
#define SCO_VIEW_CAP_NODE_STRIDE 4u  /* sizeof(sco_view_node) */
#define SCO_VIEW_CAP_ENTRY_STRIDE 5u /* sizeof(sco_view_panel_entry) */
#define SCO_VIEW_CAPS_1_0 6u         /* array length that covers every 1.0 index */
#define SCO_VIEW_FEATURE_TAGS 0x1u     /* blob tags are filled in (widgets 1.2 and later) */
#define SCO_VIEW_FEATURE_COALESCE 0x2u /* the host merges queued SLIDER and TEXT_INPUT events
                                          for one node, keeping the last */
#define SCO_VIEW_FEATURE_HOST_PANELS 0x4u /* SCO_VIEW_PANEL_HOST_OWNED is reported */

typedef struct sco_widgets_view_v1 {
    uint32_t size;    /* sizeof(sco_widgets_view_v1): 56 */
    uint32_t version; /* SCO_WIDGETS_VIEW_VERSION_1_0. A consumer checks this and `size`.
                         Functions are only ever appended. Each append raises the minor
                         macro (SCO_WIDGETS_VIEW_VERSION_1_1, and so on) and grows `size`:
                         `version` says which contract revision the host implements, `size`
                         says which functions exist. */

    /* Any thread. See SCO_VIEW_CAP_*. SCO_BAD_ARG: NULL client or caps, or caps[0] < 2.
     * SCO_UNAVAILABLE: client stopped. */
    sco_result (*capabilities)(sco_view_client* client, uint32_t* caps);

    /* Any thread, O(1), no allocation. Writes the counters (see "Counters"); any pointer
     * may be NULL. event_drops is the running total of events dropped by dispatch_events
     * (never reset). events_done is the sequence number (see submit_event) of the newest
     * event that has left the queue, whether delivered, dropped or merged by coalescing.
     * Events leave in the order submit_event accepted them, so every event with a sequence
     * number <= events_done is finished, and its effect on the panel (if any) is already in
     * that panel's revision. A caller polls this every frame and does more work only when a
     * counter moved. SCO_UNAVAILABLE: client stopped. */
    sco_result (*stamps)(sco_view_client* client, uint64_t* global_revision, uint64_t* list_revision,
                         uint64_t* event_drops, uint64_t* events_done);

    /* Menu thread in 1.0 (any non-game thread is proposed, not promised; see 2.4). Writes live panels in the host's listing order (registration order).
     * out/stride: stride >= sizeof(sco_view_panel_entry) and a multiple of 8 (BAD_ARG
     * otherwise); *inout_count is the capacity in entries on entry and the entries
     * written on return. If capacity is too small the host writes none and sets
     * *inout_count to the entries needed, with SCO_TOO_MANY. Panels whose owner was
     * released after a fault are not listed. SCO_WRONG_THREAD: the caller is not the
     * registered menu thread (see sco/host_menu.h); nothing is written. */
    sco_result (*list_panels)(sco_view_client* client, sco_view_panel_entry* out, uint32_t stride,
                              uint32_t* inout_count);

    /* Menu thread in 1.0 (any non-game thread is proposed, not promised; see 2.4). Flattens one panel into out (8-byte aligned, BAD_ARG otherwise). The
     * snapshot is built under the widget lock only if it is dirty; the flattening itself
     * runs outside the lock.
     * *inout_bytes is the capacity on entry and the bytes written on return; when too
     * small the host writes nothing and sets *inout_bytes to the bytes needed, with
     * SCO_TOO_MANY. The panel may change between the size answer and the next call: a
     * caller loops, and the blob it finally gets is one consistent snapshot of one
     * revision. SCO_NOT_FOUND: no such panel. SCO_CRASHED: its owner was disabled after
     * a fault. SCO_BAD_ARG: NULL or over-long id. SCO_WRONG_THREAD: the caller is not the
     * registered menu thread; nothing is written. */
    sco_result (*copy_panel)(sco_view_client* client, const char* panel_id, void* out,
                             uint32_t* inout_bytes);

    /* Menu thread in 1.0 (any non-game thread is proposed, not promised; see 2.4). Checks the shape of the event, copies it and queues it for the game
     * thread. It runs no plugin code and does not validate against the live node: that
     * happens in dispatch_events. Events are never dropped silently: a drop adds to the
     * event_drops counter of stamps. SCO_NOT_FOUND: no such panel. SCO_BAD_ARG: bad size,
     * unknown kind, non-finite value, text over 255 bytes or not UTF-8, nonzero reserved.
     * SCO_TOO_MANY: the queue is full (the event is not queued). SCO_UNAVAILABLE: client
     * stopped. SCO_WRONG_THREAD: not the registered menu thread; the event is not queued.
     * Returns before the plugin sees the event. On SCO_OK, *seq (may be NULL) receives the
     * event's sequence number: 1 for the first accepted event of the client, then strictly
     * increasing by 1. It is compared with events_done of stamps (the ack path). */
    sco_result (*submit_event)(sco_view_client* client, const char* panel_id, const sco_view_event* event,
                               uint64_t* seq);

    /* Game thread only (SCO_WRONG_THREAD otherwise); not from inside a widget callback
     * (SCO_FAILED). Drains the queue in order. Each event is matched as described at
     * sco_view_event: dropped when its panel is gone, when the epoch differs and no live
     * node matches by tag, or when its node is gone, changed kind, has another tag or is
     * now disabled; otherwise it is validated and delivered with
     * the same node update, tag rule, callout guard and reentrancy rules as RenderPanel.
     * A callback that replaces or removes its panel drops the remaining events of that
     * panel. *dispatched counts events delivered to a callback or applied to a
     * display-only panel; *dropped counts the rest; either pointer may be NULL.
     * events_done of stamps advances after each event's effect is in the panel revision.
     * SCO_CRASHED: at least one owner faulted (counts are still valid; remaining events of
     * that owner are dropped, other panels continue). */
    sco_result (*dispatch_events)(sco_view_client* client, uint32_t* dispatched, uint32_t* dropped);
} sco_widgets_view_v1;

#ifdef __cplusplus
}
#endif
#endif /* SCO_WIDGETS_VIEW_H */
```

Layout pins, to go in `tests/abi_widgets_view.c` and mirrored as `const` asserts in Rust (numbers computed from the text above):

| Struct | Size | Key offsets |
|---|---|---|
| `sco_view_str` | 8 | offset 0, length 4 |
| `sco_view_panel_entry` | 224 | flags 4, revision 8, epoch 16, node_count 24, id 32, title 96, owner 160 |
| `sco_view_blob_header` | 96 | total_bytes 12, revision 16, epoch 24, node_count 32, nodes_offset 40, items_offset 44, strings_offset 52, panel_id 64, title 72, owner 80 |
| `sco_view_node` | 112 | flags 16, index 20, value 56, minimum 64, maximum 72, label 80, text 88, tooltip 96, tag 104 |
| `sco_view_event` | 56 | node_id 4, value 16, text 24, row 32, epoch 40, tag 48 |
| `sco_widgets_view_v1` | 56 | version 4, capabilities 8, stamps 16, list_panels 24, copy_panel 32, submit_event 40, dispatch_events 48 |

### 2.3 Counters

All counters come from one monotonic 64-bit source, so a value is never reused, even for a panel removed and added again. Zero is never a valid revision. The single shared source is new Core work. Core today has only a per-panel `generation` that moves on every plugin mutation (`set_panel`, `update_nodes` and `remove_nodes` all call `Changed()`), so it cannot serve as `epoch`.

| Counter | Changes when | Used for |
|---|---|---|
| `panel.revision` | anything `copy_panel` would return changes: `set_panel`, `update_nodes`, `remove_nodes`, an event applied to a node (value, index, text, page row) | decide whether to re-copy one panel |
| `panel.epoch` | a new counter, bumped only by `set_panel` (replace, or remove and add). `update_nodes` and `remove_nodes` do not move it | tell dispatch that the panel was replaced: a tagged event is then rematched by tag, any other event is dropped. Everything else is checked per node against the live tree |
| event sequence (`seq`, from `submit_event`) | each accepted event gets the next number, from 1 | name one submitted event |
| `events_done` (from `stamps`) | an event leaves the queue (delivered, dropped or merged), after its effect is in the panel revision | the ack path: the event with `seq` <= `events_done` is finished |
| `list_revision` | the set of listed panels changes (add, remove, owner released) or a panel's title, owner or flags change | decide whether to re-list |
| `global_revision` | any of the above, for any panel | the per-frame poll: one value to compare |

Why not drop on any change: a click is copied on the menu thread and delivered on a later game-thread pass. A plugin that refreshes live values in that gap (an inspector Refresh, a status-like panel) must not lose the click. So a mutation of other nodes does not matter; only the clicked node is re-checked (same id, same kind, still enabled, ancestors enabled). A real replace of the panel (a new epoch) is different. Plugins that republish a panel with `set_panel` on a timer or after each status change are common, and the gap between copy and dispatch is one game pump, not one render pass. So a replace must not lose a click on a node that still exists. The event therefore carries the node's tag, and when the epoch differs and the live panel is tagged, the host matches the live node by tag, kind and enabled state and delivers there. Only an untagged panel, a tag that no longer exists, a changed kind or a disabled node drops the event. The drop is counted and shown (section 3.4). The same tag check at an equal epoch catches a plugin that removed a node and added a different one under the same id.

Ack path. `submit_event` returns a sequence number and `stamps` returns `events_done`. The queue is first in, first out, so "event k is finished" is the single test `events_done >= k`. The menu uses it to keep its optimistic local value (section 3.4) until the host has caught up, so a revision that arrives while an event is still queued cannot snap a slider or toggle back.

A menu frame is: `stamps`; if `global_revision` moved then `list_panels` (when `list_revision` moved) and `copy_panel` for the open page only (when its revision moved); draw; submit events.

### 2.4 Core implementation notes (not part of the header)

- `Entry` gains `revision` and `epoch`, both from the new shared counter. Flattening reuses the shared immutable snapshot that `RenderPanel` already builds. `copy_panel` takes the widget lock, builds the snapshot if dirty (the same coalescing counter `SnapshotBuilds`), copies the `shared_ptr`, releases the lock, then flattens. It never calls plugin code and never holds the lock while writing the caller's buffer.
- Cost and contention, stated: a dirty rebuild is a full `Panel` copy under `g_lock` and now may run on the menu thread, so the game thread's `set_panel` and `update_nodes` can wait on it. The rebuild is bounded by the widget limits (4096 nodes), and happens at most once per change. Option kept open: build the snapshot on the game thread at the end of a mutation and let the menu thread only copy. M1 measures both and picks. `SnapshotBuilds` keeps its meaning in the tests (builds, not callers).
- Thread check, source reading (not yet a test result). `Panels()` and `OwnedBy()` return nothing off the game thread today, so `copy_panel` must not use them and reads the widget table directly under the widget lock. The two owner reads it needs look safe off the game thread in the sco-core source: `detail::Released(owner)` takes only the owner lock; `host::PluginId(owner)` takes a `RuntimeLease` and reads an id that does not change after the handle is claimed. The lock order widget lock then owner lock already exists in `RenderPanel`. One catch: `PluginId` returns null during startup admission and while the runtime stops. In that case `list_panels` skips the panel and `copy_panel` answers `SCO_UNAVAILABLE`; neither crashes. M1 task one is to confirm this reading and to write the race test in 5.1 (threads call the view while plugins load, fault and unload). The thread column of 2.2 stays "proposed" until that test passes, and the header is not called final before then.
- Id lengths. Panel id and title limits are 63 bytes. The plugin id is at most 31 bytes (`kMaxIdLen` in `include/sco/host.h` and `plugins.h`, source-checked), so the 64-byte owner field always fits. `list_panels` still skips a panel whose owner id would not fit, with one log line, and never truncates it. That branch is defensive and cannot be reached today.
- Password nodes. `copy_panel` omits the text of a node with `SCO_WIDGET_PASSWORD` (length 0, flag kept). The blob therefore never carries a secret, in any shape. The Rust field shows no dots for an existing value; it only accepts new input.
- Admission and shutdown order. Every view call takes the `RuntimeLease` like `RenderPanel`. Stop closes admission first. The order in `src/dllmain.cpp` is: Core closure runs on the game thread, `DrainRequestedStop(1000)` (the Core owner drain) runs on the control thread, and only then `Menu_Drain(1000)` joins the menu thread. So the Rust thread can still call the table after the client's stop has returned. This is why the table and the client handle are static for the process lifetime (header rule) and why a call after stop answers `SCO_UNAVAILABLE` and is expected. Nothing in the design waits on the join for memory safety. A stop-versus-copy race test with the join after stop is in 5.1.
- Menu thread registration. The product calls `HostWidgetsViewSetMenuThread` (2.5) when the menu thread starts and clears it at drain. `list_panels`, `copy_panel` and `submit_event` compare the caller's platform thread id with the registered id (the same kind of check the game-thread test uses for `dispatch_events`) and answer `SCO_WRONG_THREAD` otherwise. In debug builds a mismatch also asserts. No registered thread means every caller is refused.
- Periodic `set_panel`, source reading in `plugins/` of this repository (not a test, not run in game): `set_panel` is called from a command, an event or a startup path in gear_presets, inspector, titanlink, voxel_bridge, build picker and travel. `plugins/loadout/loadout.cpp` registers a `tick` subscriber (`Tick`, near line 965) that ends in `Publish()`, which calls `set_panel` for `loadout.player` and `loadout.sq42`; M1 confirms which ticks reach it. So at least one shipped plugin can republish a whole panel on a tick, which is why the tag rematch above exists. M1 reports the exact count and re-counts after M6.
- Event drops: the queue keeps a running drop total, returned by `stamps`. `dispatch_events` logs at most one line per second.
- `RenderPanel`'s event half is split out as one function, `ApplyAndDispatch(entry, event)`, used by both `RenderPanel` and `dispatch_events`. Two code paths for validation would drift. Existing widget tests must stay green with the split. The node-level check (id, kind, enabled including ancestors) is the check `RenderPanel` already runs on the live node, so the split adds no new rule, only the epoch gate.
- The queue is per client, bounded at `SCO_WIDGETS_MAX_EVENTS` (256) entries, guarded by its own small mutex, never held during a callback. Coalescing is only for SLIDER and TEXT_INPUT, only for adjacent events of one node. BUTTON, TOGGLE, COMBO, LIST and PAGE are never merged or reordered.
- Behaviour change for plugins, stated: in Rust mode Core no longer renders panels, so a plugin gets no per-pass signal. Callbacks arrive only from `dispatch_events`, on the game thread, with no render context and no "panel is visible" hint. A plugin that relied on work done inside a render pass (a lazy refresh when its page is drawn, state set only on a render path) must refresh on its own events or on its own timer. Panels never gain a hidden or visible signal in this version. `docs/plugin-ui.md` says so, and a test plugin that keeps state only in a render-pass path (5.1) shows the effect.
- Host panels: the host-menu reserved owner's panels carry `SCO_VIEW_PANEL_HOST_OWNED`. The Rust menu does not render them as pages (section 3.8).
- Logs: a dropped event is counted, never logged per event. `dispatch_events` returns the counts and the host logs one line per second at most.

### 2.5 How the host gets the table (C++ seam, `sco/host_menu.h`)

This is the only place the host-menu names appear. The C header above does not mention them.

```cpp
// Game thread. Valid only for the reserved host-menu identity of an active HostMenuClient
// (HostMenuActive(context) is true). Returns false for any other caller or after stop.
// Both *table and *client are static: valid for the life of the process, never freed or
// reused. After the client's stop callback returns, every call through them answers
// SCO_UNAVAILABLE.
bool HostWidgetsView(const HostMenuContext& context, const sco_widgets_view_v1** table,
                     sco_view_client** client);

// Any thread of the host, for the same reserved identity. Registers the thread id that may
// call list_panels, copy_panel and submit_event. The menu thread calls it first thing when
// it starts; the control thread clears it (thread_id 0) after Menu_Drain. Returns false for
// any other caller. Until it is called, those three functions answer SCO_WRONG_THREAD.
bool HostWidgetsViewSetMenuThread(const HostMenuContext& context, uint32_t thread_id);
```

The product's `Client::Load` calls it and passes both pointers to the Rust module (section 3.2). A plugin has no way to name this identity.

### 2.6 Versioning and safety

- The table only grows: new functions are appended, each append raises the minor version macro and `size` tells a consumer what exists. The blob header, node and entry structs only grow at the end. A reader that sees a bigger `size` ignores the tail. A change to an existing field is a new major.
- The menu checks table `version` and `size`, blob `version`, `magic`, `size >= 96/112/224`, `node_stride >= 112`, that `nodes_offset`, `items_offset` and `strings_offset` are multiples of 8 and their ranges do not overlap, and every offset and length before it reads a byte. A malformed blob is a refused page ("This page could not be read"), never a crash. Section 5 fuzzes this reader.
- Least privilege: the event path is the same path a renderer uses. A Rust menu cannot do more than an ImGui menu could: it cannot press a disabled node, set an out-of-range slider, or choose the callback's tag.
- No new capability is switched on or off by this interface. Nothing here changes a default.

### 2.7 Contract record and provenance (M1 deliverables, draft text)

Draft entries for `docs/contracts.md`, in its Inventory plus per-method form, and for the catalogue/status data in sco-core.

Inventory row:

| Table | Methods | Size | Version | Kind |
|---|---|---|---|---|
| `sco_widgets_view_v1` | 6 | 56 bytes | 1.0 (`0x00010000`) | host-source seam; no service name; not published by `query_service` |

Per-method rows (signature, ownership, threads, errors, cancellation and lifetime; the full wording is in the header comment and is copied verbatim):

| Method | Ownership | Threads | Results | Lifetime and cancellation |
|---|---|---|---|---|
| `capabilities(client, caps)` | caller's array; written, not kept | any | OK, BAD_ARG, UNAVAILABLE | none; O(1) |
| `stamps(client, g, l, drops, done)` | caller's pointers; written, not kept | any, O(1), no allocation | OK, UNAVAILABLE | none |
| `list_panels(client, out, stride, count)` | caller's array; Core keeps nothing | menu thread in 1.0 (proposed: any non-game thread) | OK, BAD_ARG, TOO_MANY (count = needed), UNAVAILABLE, WRONG_THREAD | a snapshot of the list at one revision |
| `copy_panel(client, id, out, bytes)` | caller's 8-aligned buffer; Core keeps nothing | menu thread in 1.0 (proposed: any non-game thread); runs no plugin code | OK, TOO_MANY (bytes = needed), NOT_FOUND, CRASHED, BAD_ARG, UNAVAILABLE, WRONG_THREAD | one consistent snapshot of one revision |
| `submit_event(client, id, event, seq)` | event, text and tag copied during the call; `*seq` written on OK | menu thread in 1.0 (proposed: any non-game thread) | OK, NOT_FOUND, BAD_ARG, TOO_MANY, UNAVAILABLE, WRONG_THREAD | queued; finished when `events_done` >= `*seq`; may be dropped later and counted in `event_drops` |
| `dispatch_events(client, ok, dropped)` | none | game thread only; not inside a widget callback | OK, WRONG_THREAD, FAILED, CRASHED | drains the queue; a callback that faults stops its owner's events only |

Machine contract record. In Core the per-method authority is the machine records in `sdk/contracts/*.json` (one file per table, for example `sco_widgets_v1.json`). The cells above are checked against those records by `tools/sdk_catalogue.py check`, and CI refuses a disagreement. So M1 adds `sdk/contracts/sco_widgets_view_v1.json` in the existing format: six `table-method` records (`sco-widgets-view-v1-capabilities`, `-stamps`, `-list-panels`, `-copy-panel`, `-submit-event`, `-dispatch-events`), each with `symbol`, `table` (`sco_widgets_view_v1`), `version_macro` (`SCO_WIDGETS_VIEW_VERSION_1_0`), `source_header`, `signature`, `member_extent`, `table_size` (56), `since`, `purpose`, `buffer_contract`, `ownership`, `thread_affinity`, `results`, `reentrancy`, `cleanup`, `limitations`, `tests`, and the other fields the existing records carry. The Inventory row and the rows above are generated to match them. The extractor today reads public SDK headers and compares them with the records, and the existing records name a service in their `service` field. This table has no service name and is not published, so M1 first runs `tools/sdk_catalogue.py check` on the new header and decides one of two ways, recorded in the PR: add the header and records with an explicit host-source marker so the check passes, and keep the table out of the plugin-facing catalogue export, the status registry and the plugin kit; or list the header in an explicit allowlist of host-source headers that the check skips (then no record file is needed, and the prose rows above become the only text, which the owner must accept). The first way is recommended: the header is then still a checked contract.

Stability class. `docs/stability.md` classes only published service tables as Stable or Experimental. This table is not published (`query_service` answers `SCO_NOT_FOUND`), so it is outside that scheme: a host-source seam, pinned by size and golden, with no promise to plugins. `docs/stability.md` gets one sentence saying so. If the owner later ships it in the plugin kit (decision 2, section 7) it would need a class, and Experimental is the right start.

Golden pin. The header is host-source only, so it is not a plugin promise, but its bytes are still pinned. Core's released-header check (`tests/abi/released/sco_api.h`, the `abi_snapshot` target in `CMakeLists.txt`, `tools/abi_snapshot.py`) is built around `sco_api.h`. M1 extends it: `tools/abi_snapshot.py` takes a list of headers, the `abi_snapshot` target passes `sco_widgets_view.h` as a second one, and the first release that contains the header stores `tests/abi/released/sco_widgets_view.h`; from then a test fails if the live header and the golden differ without a version bump. Until that release, the only pin is `tests/abi_widgets_view.c` (the static asserts for every size and key offset), which lands in the same M1 change as the header, not after it. That is a stated gap: the owner accepts it in section 7. The `version` field and `size` follow the rule in 2.2: appending a function bumps the minor macro and grows `size`.

| Field | Value |
|---|---|
| Name | `sco.widgets.view` (a label for docs and logs; there is no service name) |
| Header | `include/sco_widgets_view.h`, host-source kit only (owner decision 2, section 7); record `sdk/contracts/sco_widgets_view_v1.json` |
| Game neutrality | no game type, no game name, no change to `sco_api.h` or `sco_widgets.h` |
| Tests | `tests/abi_widgets_view.c`, `tests/test_widgets_view.cpp`, shared fixtures |
| Status | specified and tested synthetically once M1 lands; **not run in game** |

Provenance: the header and its tests are independently authored from the public `sco.widgets` contract and the Core sources already in the repository. Each new file carries the per-file provenance metadata the repository requires and the SPDX line `GPL-3.0-only`. No private ledger id, private path, extracted game data, binary or dump is used or cited. Fixtures are synthetic panels only. Provenance review stays a human step.

## 3. The Rust menu: process shape, input, theme

### 3.1 Two shapes compared

| | A. cdylib `scom.dll` in the game process | B. separate `scom.exe` over shared memory |
|---|---|---|
| Data path | direct C calls to the view table | a C++ writer in the game serialises blobs into a shared region, a seqlock per panel, an event ring back. A second wire format to freeze and fuzz |
| Crash blast radius | same as today's C++ menu. A Rust panic is caught at the FFI edge (3.3). A memory-safety bug in `unsafe` D3D/Win32 code could still take the game down | menu crash cannot take the game down. Game crash takes the menu with it (parent wait) |
| New moving parts | one DLL, retained like the host | a second executable, process start and job object, ACL on the shared region, stale-region cleanup, antivirus and SmartScreen exposure |
| Graphics in the game process | one more D3D11 device, as today | none: the menu process owns its device (could use the launcher's wgpu/eframe stack) |
| Process-global side effects | must avoid: no DPI-awareness change, no window class clash | none, its own process |
| Focus and cursor | same as today (`OurProcessHasFocus` by pid) | foreground is another pid: `M` must accept game or menu pid, `ClipCursor` is still per-desktop and works across processes |
| Wine | the host already runs under Wine | one more process to get right |
| Reuse with the launcher UI | egui, theme and renderer crates shared, painter differs | the launcher's whole UI stack could be reused |
| Evidence needed | owner session: menu thread in game | owner session: process start, focus, shared memory |

Recommendation: **shape A first**, with the blob and the `PanelSource` trait (3.4) written so that shape B is a small transport swap. The panel blob is already relocatable and travels unchanged. The event path is not: `sco_view_event.text` is a raw pointer, so shape B needs a second event encoding (text by offset into a ring slot) and a seqlock or ring for events. That is bounded work, but it is a second wire format for events only. Reasons: it matches what the C++ menu already proves exists (its own thread, window and D3D11 device inside the game process), it adds no second wire format, and it keeps process count and install surface unchanged. Switch to B only if the M0 spike or the owner session shows one of: the in-process Rust DLL or its device conflicts with the game or its overlays; a panic or fault in the menu is not containable; `unsafe` surface in the painter and window glue proves hard to keep small. The decision is the owner's (section 7).

### 3.2 Module boundary (product-private, not an SDK ABI)

`scom.dll` is built from `menu-rs/` in sc-offline, staged next to dinput8.dll, hash-pinned in the release manifest like the other native modules. It exports one function. The C++ host stays the owner of lifetime.

```c
/* sc-offline private. Not an sco-core or plugin ABI. Thread of each callback is marked T:
 * any = any thread, ctl = control thread only (Menu_Start and Menu_Drain only), menu = called from the menu thread. */
typedef struct scom_host_v1 {
    uint32_t size, abi;                 /* abi = 1 */
    void* game_window;                  /* HWND */
    const sco_widgets_view_v1* view;    /* section 2 */
    sco_view_client* client;
    const wchar_t* data_dir;            /* where menu-window.ini, menu-view.ini, menu_background.* live */
    void (*log)(const char* utf8);      /* T any. mod.log line, "[menu] " added by the host */
    uint32_t (*status)(char* out, uint32_t cap);   /* T any. The status strip text, UTF-8. The only writer
                                           is Menu_GameThreadFrame (game thread), which builds the
                                           strip as today and stores it under a small mutex with a
                                           timestamp. This call copies it under the same mutex and
                                           never touches game-thread state. Staleness bound: one game
                                           pass; if the game pump stalls the text goes stale, so the
                                           host appends " (not updated)" when it is older than 2 s */
    void (*nudge_game)(void);           /* T any. PostMessage WM_NULL: runs the host's game-thread pump.
                                           Coalesced by a flag of its own (g_rustNudged), separate from
                                           the ImGui menu's g_nudged; Menu_GameThreadFrame clears it
                                           before dispatch_events. At most one WM_NULL is in flight */
    void (*legacy_open)(const char* page_id);      /* T menu. Non-blocking: stores the page id, sets an
                                           atomic request and signals the legacy window's own thread,
                                           creating that thread on first use (section 4.1). Returns in
                                           well under 1 ms. It does not use the control thread */
    uint64_t (*legacy_tabs)(char* out, uint32_t cap, uint32_t* needed); /* T menu. Calls the thread-safe
                                           `sco::ui::Tabs()` snapshot (any-thread by sco/ui.h), writes
                                           lines "id TAB title TAB badge" (out may be NULL: hash only),
                                           and returns a 64-bit hash of all lines, badges included.
                                           sco::ui has no change counter, so the hash is the change
                                           signal; the menu calls this every poll interval (33 ms
                                           visible) and re-reads only when the hash moved. Cost is one
                                           small vector copy per poll; M5 measures it. A Core revision
                                           counter for sco::ui is an option, not required */
    uint32_t (*switches)(char* out, uint32_t cap);   /* T any. Read-only lines "name TAB ON|OFF TAB text"
                                           for the game capability switches (section 3.10), UTF-8,
                                           from a copy the host fills once at start. Never read
                                           from game-thread state */
    void (*settings_changed)(void);     /* T menu. Sets an atomic dirty flag and returns. The legacy
                                           window's thread re-reads menu-view.ini, menu-window.ini and
                                           the background settings when it next shows (it also re-reads
                                           on every show). No game-thread or control-thread work */
} scom_host_v1;

typedef struct scom_module_v1 {
    uint32_t size, abi;
    int32_t (*start)(void);             /* ctl. creates the menu thread; 0 = ok */
    void    (*request_stop)(void);      /* any thread, never waits */
    int32_t (*drain)(uint32_t timeout_ms);   /* ctl. join; 0 = joined */
    int32_t (*is_typing)(void);         /* any. Reads one AtomicBool the menu thread writes once
                                           per frame; never touches egui state: a text box has the
                                           keyboard, so hotkeys stay quiet */
    void    (*show_legacy_closed)(void);/* any. Called by the legacy window's thread. Sets a flag and wakes the menu thread, which shows
                                           the Rust window; returns at once */
} scom_module_v1;

int32_t scom_open(const scom_host_v1* host, scom_module_v1* out);   /* the only export */
```

Rules for these tables:

- Nothing in them runs on the menu thread that needs the game thread or the control thread to answer. Every callback marked `menu` only stores a request or reads a snapshot, and the menu thread never waits for anyone. The control thread is not a target of any callback. Source check: in `src/dllmain.cpp` the control thread calls `Menu_Start`, then loops on `WaitForSingleObject(g_controlWakeEvent, 200)`, and that event means "stop". It cannot serve requests, would add up to 200 ms latency, and reusing the event would stop the game. So the design does not restructure that loop. The legacy window has its own thread with its own message pump (it must, to own a window) and its own wake event. `active_window` is an atomic enum in the host. The window that is closing writes it with a compare-exchange just before it moves focus to the other (4.1). `Menu_Drain` runs on the control thread and joins both threads.
- The sco.ui tab list is read through `sco::ui::Tabs()`, which `sco/ui.h` documents as an any-thread snapshot, so the menu thread may call it. It has no change counter and badges can change without a registration, so the host cannot bump a counter on registration. Instead `legacy_tabs` returns a hash of the whole list including badges (see the table above). A tab registered, removed or re-badged after the menu started therefore shows in "More plugins" within one poll interval (33 ms while visible). Rust copies the bytes out; the memory belongs to the host.
- Settings files. In Rust mode the Rust menu is the only writer of `menu-view.ini`, `menu-window.ini` and the background and developer-page settings (it writes to a temp file and renames, so a reader never sees half a file). `settings_changed` tells the host to re-read them. The host also re-reads them every time it shows the legacy window, so both windows show the same values. When the legacy window changes a setting it writes through the same helpers and the Rust menu re-reads the files when it is next shown. A test (5.4) changes a value in each window and checks that the other shows it.

The existing contract maps one to one: `Menu_Start`, `Menu_RequestStop`, `Menu_Drain` (same control-thread and `WrongThread` rules), `Menu_Typing`, and `Menu_GameThreadFrame`, which in Rust mode only calls `dispatch_events` (and runs the legacy frame when the legacy window is open). `scom.dll` is pinned with `GetModuleHandleExW` before `start` and never freed after exposure. `scom_open` and `start` do nothing in `DllMain`: no threads, no loader-lock work.

Selection at start: `menu_backend` in the existing settings file, values `rust` or `imgui`. Until the owner records an in-game session the default stays `imgui`. The host falls back to `imgui` for the whole session, with one `mod.log` line, when `scom.dll` is missing, fails its hash, is the wrong `abi`, `scom_open` or `start` returns nonzero, the view table lacks `capabilities`/features it needs, or the host menu client did not load.

### 3.3 Crate layout and fault containment

```
menu-rs/                         separate Cargo workspace from launcher-rs/ (own lockfile,
                                 own profile; shares crates by path or after promotion)
  crates/
    scom-view/       blob parser and safe model (no FFI beyond reading bytes), PanelSource trait
    scom-nav/        port of menu_navigation.h: sections, Known pages, Arrange, Choose, menu-view.ini
    scom-window/     port of menu_window.h: size, clamp, fit, remember, restore, menu-window.ini,
                     EscapeInput
    scom-theme/      theme tokens (loads the scuba-preview-theme/1 JSON), egui Visuals and metrics
    scom-render/     egui node-tree renderer: port of imgui_adapter.cpp + imgui_layout.h
    scom-d3d11/      egui painter for Direct3D 11 (mesh, textures, scissor, premultiplied alpha)
    scom-app/        the egui app: sidebar shell, page header, Settings page, status strip
    scom-win/        Win32 window, message pump, input to egui RawInput, focus, cursor, DPI, WIC
    scom/            the cdylib: scom_open, module table, catch_unwind wrappers
    scom-preview/    headless binary: synthetic panels in, PNG out (tests only, not shipped)
    scom-testhost/   a Rust fake of the sco_widgets_view_v1 table for tests
  profiles: release-menu = release with panic = "unwind" (the launcher profile uses abort)
```

- **No panic crosses the FFI edge.** The cdylib builds with `panic = "unwind"`. Every exported or callback function body runs under `catch_unwind`. On a panic the menu logs one line, hides its window, stops asking for frames, sets a broken flag and returns an error code. The same guard wraps the menu thread entry and the painter loop, not only the FFI edge. A drop guard on that thread always runs `ClipCursor(NULL)` and hides the window, also during unwinding. It does not try to restart. The player sees no menu for the rest of the session and `mod.log` says why. This is stated honestly in the docs; a hot fallback to ImGui after a runtime panic is not promised.
- `unsafe` is confined to `scom-win`, `scom-d3d11` and the FFI shims, each module with a short safety comment per block. `scom-view`, `scom-nav`, `scom-window`, `scom-theme` and `scom-render` use `#![forbid(unsafe_code)]`.
- `catch_unwind` does not cover an access violation or a stack overflow inside D3D or a driver. The blast radius equals today's C++ menu; the guard covers Rust panics only. Shape B exists for the case where that is not enough.
- Blob buffers handed to `copy_panel` and `list_panels` are 8-byte aligned: the Rust side uses a `Vec<u64>` (or an aligned wrapper) and never a plain `Vec<u8>`. A test passes a misaligned pointer and expects `SCO_BAD_ARG`.
- Static CRT (`+crt-static`), like every other sc-offline module. Rust's allocator is the system allocator; no allocation crosses the boundary (blobs are copied into Rust-owned buffers, strings are copied out of them).
- Dependencies are kept small and checked with the same cargo-deny rules as the launcher (decision 18). Expected: `egui`, `epaint`, `emath`, `ecolor`, the `windows` crate (D3D11, DXGI, WIC, Win32), `serde_json` for the theme file. No `winit`, no `eframe`, no `wgpu`, no `image`. Turn egui's default fonts off (decision 10).

### 3.4 Threads and data flow

```
 game thread                         menu thread (scom)                 plugin
 -----------                         ------------------                 ------
 message hook pass:                  loop (vsync, on demand):
   Menu_GameThreadFrame()              stamps()                    <--- any thread, O(1)
     dispatch_events()  ---------->    list_panels / copy_panel    <--- menu thread (1.0)
       validate, callout --------->    build page tree, draw egui         owner callback
                                       painter -> D3D11 -> Present
                                       submit_event()  --queue-->  (next game pass)
                                       nudge_game() (WM_NULL, once per burst)
```

- The menu thread never waits for the game thread. This is the main behavioural gain over today: a game that pumps messages rarely no longer makes the menu crawl. An event may still take until the game's next message-hook pass to reach the plugin; that is unchanged, and the existing `WM_NULL` nudge stays.
- `PanelSource` is a trait with three methods (`stamps`, `list`, `copy`) and one for events (`submit`). Implementations: the real FFI view, `scom-testhost` (fake table), and a dump-file source (section 5). The renderer and app never see which one they have.
- Optimistic local state, with an ack. While the player drags a slider, types, or flips a toggle, egui holds the value locally. The menu records, per node (keyed by tag when there is one, else by id), the value it sent and the `seq` that `submit_event` returned. A host revision never replaces that node's local value while the pointer is on it, nor while `events_done` < that `seq` (the event is still queued, so the copied value is old). When `events_done` reaches the `seq`, the menu marks the node settled, forces one `copy_panel` of the panel (the event may have been dropped, so the revision need not have moved), shows the copied value, and forgets the record. If the host dropped the event (`event_drops` moved) the copied value is simply the plugin's value and the node snaps to it, once, with the "try again" line. A newer event for the same node replaces the record and its `seq`. On a new epoch the local state is kept for tagged nodes that still exist (matched by tag) and dropped for the rest. Test: the fake host delays dispatch by 300 ms, a plugin revision bump arrives at 100 ms, and the slider value shown never goes back to the old value; see 5.2.
- Event pacing: at most one event per node per frame (last value wins), on top of the host's own coalescing. When `submit_event` answers `SCO_TOO_MANY`, the menu keeps BUTTON/TOGGLE/COMBO/LIST/PAGE events and retries them on the next frames (they are never discarded); only the last SLIDER and TEXT_INPUT value per node is kept. Drops after queueing are counted by the host (`event_drops` in `stamps`). When that total moves, the menu shows a transient line in the status strip, "A panel changed; try again", for three seconds, so a lost click is never silent. With the tag rematch (2.3) this line appears only for an untagged panel that was replaced, or a node that was removed or disabled.
- Poll interval: while visible, the menu thread calls `stamps` at least every 33 ms even with no input (and on every frame it draws), so a plugin-driven revision change appears within about 33 ms. While hidden or unfocused the interval is 50 ms. A test with `scom-testhost` bumps a revision with no input and checks the redraw within 40 ms (hidden: 60 ms).
- Repaint is on demand: the loop draws when an input event, a revision change, an animation or an egui repaint request is pending, and sleeps 50 ms while the window is hidden or unfocused, as today. The game is not asked for frames at all while the menu draws panels.

### 3.5 Window, focus, cursor

Port the C++ behaviour, not a library's idea of it. The Win32 window is created by `scom-win` directly (no winit). winit may set process-wide DPI awareness when its event loop is created (the spike must check the pinned version); that could change the game's own DPI behaviour, and the existing menu deliberately follows whatever the thread's awareness is. Own the window and keep these rules:

| Behaviour | Rule |
|---|---|
| Style | `WS_POPUP \| WS_THICKFRAME`, `WS_EX_TOPMOST \| WS_EX_TOOLWINDOW`, class `starcitzenofflinemods_menu` (same string, so nothing that finds it by class changes) |
| Show | restore saved size at the monitor's scale, 60 px inside the game window, `SetWindowPos(HWND_TOPMOST, SWP_SHOWWINDOW)`, `SetForegroundWindow`, center the cursor, clip |
| Hide | on Esc (key up after a down that the menu saw, as in `EscapeInput`), on `M` when no text box is active, on `WM_CLOSE`: `ClipCursor(NULL)`, `ShowWindow(SW_HIDE)`, `SetForegroundWindow(game)` |
| `M` | polled with `GetAsyncKeyState('M')` on the menu thread, edge detected, ignored while a text box has the keyboard, accepted only when the foreground window is in this process (same pid test as today) |
| Esc and `WM_CHAR` 0x1b | swallowed by the escape state machine, ported from `menu_window.h` with its vectors |
| Cursor clip | clipped to the window rect while the menu is foreground, released while sizing or moving, on focus loss and on hide. `ClipCursor` is called only when the rect changed |
| Cursor drawing | software cursor, like ImGui's `MouseDrawCursor`: the window hides the OS cursor in its client area and the app draws an arrow. Chosen for parity; the spike confirms it is reliable when the game has hidden the OS cursor |
| Size limits | `WM_GETMINMAXINFO`, `WM_SIZING`, `WM_EXITSIZEMOVE` -> `Remember` -> `menu-window.ini`, same file and format, so a saved size carries over in both directions |
| Frames while hidden or unfocused | none drawn; last picture stays (as today) |
| Stop | `request_stop` may be called from any thread. `drain` runs on the control thread and joins the menu thread. Once stop has begun the hide path does only `ClipCursor(NULL)` and `DestroyWindow` on the menu thread. It does not call `SetForegroundWindow`, `ShowWindow` or anything else that sends a message to the game window, because the game window's thread may be the very thread blocked in the join (activation sends cross-thread messages). If the game thread cannot take focus back by itself, Windows returns focus when the topmost window is destroyed. `drain` times out cleanly if the menu thread does not finish; the module stays pinned and no handle is freed |

### 3.6 Input

- Mouse: `WM_MOUSEMOVE`, buttons, wheel (and horizontal wheel), `WM_MOUSELEAVE`, capture on press. Positions are in client pixels divided by `pixels_per_point`.
- Keyboard: `WM_KEYDOWN/UP` (+ `SYS`) map to egui keys with modifier state; `WM_CHAR` supplies text as UTF-16 with surrogate pairs joined, control characters dropped. Cyrillic and other BMP/astral text works. No IME composition window (the ImGui Win32 backend has none either), no RTL or complex shaping (egui and ImGui share this limit). Stated as a known non-goal, not a regression.
- Focus: `WM_SETFOCUS/KILLFOCUS` reset egui's focus, held keys and the escape state, and release the cursor clip.
- Typing flag: once per frame the menu thread stores `visible && foreground && wants_keyboard_input()` into an `AtomicBool`. `is_typing` (called from the host's hotkey or game thread) only loads it. It never reads egui state. The host uses it to keep game hotkeys quiet, as `Menu_Typing()` does. A test calls `is_typing` from another thread while frames run.
- Clipboard: Ctrl+C/V/X inside the menu's own text boxes only, through the Win32 clipboard (`CF_UNICODETEXT`). No `arboard`.
- Messages sent to the window while another thread waits are always answered, and none reaches egui before the window is initialised or after `request_stop` (the existing "callbacks closed" rule).

### 3.7 DPI

- The menu does not change process or thread DPI awareness. `pixels_per_point = GetDpiForWindow / 96`. If the game process is not DPI aware that is 1.0 and Windows scales the window, exactly as today. If it is aware, text and lines are crisp at 125 or 150 percent. This is a small improvement over fixed 17 px ImGui text and is the one deliberate difference; the owner confirms it in game.
- `WM_DPICHANGED` (moved to another monitor) rescales, resizes through `Restore`/`Clamp`, and keeps the saved 96-dpi size.
- Body 17 px and heading 20 px are logical sizes at `pixels_per_point = 1`.

### 3.8 Theme and shell parity

One source of truth for the look is already planned outside this page: scuba-engine's `docs/engine/preview-themes/sc-offline.json` (`scuba-preview-theme/1`) transcribes `ApplyTheme()` and `LoadFonts()`. `scom-theme` loads that same JSON format. A CI check compares the JSON to the values in `menu_shell.cpp` (the preview flow already regenerates it), so the C++ theme, the preview host theme and the Rust theme cannot drift apart silently.

| ImGui token (menu_shell.cpp) | egui target | Note |
|---|---|---|
| `kSoil 0E1811` | panel/window fill, backdrop clear | background behind the image |
| `kPanel 172A1B`, `FrameBg` alpha .88/.95/1 | `widgets.inactive/hovered/active.bg_fill`, `extreme_bg_color` | alpha kept |
| `kLine 2F4B32`, `Border 4F7F51` | `noninteractive.bg_stroke`, popup stroke | popup border 1 px |
| `kText`, `kMuted` | `override_text_color`, weak text | muted for hints and sidebar section names |
| `kLeaf 86C64B`, `kLeafDeep 3E6B27` | selection, checkmark, slider grab, primary button | primary button hover `4F8732` |
| `Header 3F7A2A` at alpha 0.9, hovered `2D5832`, active `4F9433` | selected sidebar row and list row fills | text stays above 4.5:1 |
| `ScrollbarSize 11`, rounding 4, `FramePadding 9,5`, `ItemSpacing 8,7`, `WindowPadding 16,12` | `Spacing`, `button_padding`, `scroll`, `corner_radius` | window rounding 0 |
| `DisabledAlpha 0.8` | custom disabled blend at 0.8 | egui has no knob; `scom-render` paints disabled itself, and the golden PNGs pin it |
| Fonts: Bahnschrift, Segoe UI, from the Windows font folder, 17 px; heading 20 px, page title 24 px | `FontDefinitions` loaded from file at run time; fallback is the owner-approved bundled font | fonts are not shipped; the bundled fallback needs decision 10 (a Cyrillic OFL or MIT font) |

Known, accepted differences (each pinned by a golden so a change is visible): egui text rasterisation and hinting differ from ImGui's; the egui combo popup and table lines differ in detail; AccessKit is not wired (no winit adapter; the ImGui menu had none either).

Shell parity: `scom-nav` is a line-for-line port of `menu_navigation.h` (sections, `kKnown` rows, `Describe`, `Arrange`, `Choose`, `View`, `LoadView`/`SaveView`) tested with the same vectors as `tools/menu_navigation_test.cpp`. `menu-view.ini` keeps its name and format. The page layout is the same: sidebar (clamped 170 to 260 px, `11 x font size`), page header (title, muted summary, rule), page body with its own scroll position per page, status strip along the bottom with a 4 px leaf-green edge. The vehicles page keeps the "Your own ships" grouping of `spawn.vehicles` with `spawn.asop` (`menu_panel_group.h` ported with its test).

The Settings page and the status strip become native Rust. `menu.main` and `menu.status` (the host-menu panels that exist only because the ImGui shell needed a panel to draw them) are not used in Rust mode; the host menu client keeps its identity (it is how the view table is obtained) and skips publishing them when the Rust backend is active. The Settings page keeps the same controls: show background, darkness 20 to 95 percent, image position when the picture is wide, About text, the developer-pages switch, the bug-report line, and a new read-only line naming the backend and `scom.dll` hash, and the capability readout of 3.10. Background image: decoded with WIC (as today, so PNG and JPEG parity without an image crate), uploaded as a D3D11 texture, cover-cropped with the same position maths, under the same soil-green wash.

### 3.9 Rendering a panel (port of the ImGui adapter)

`scom-render` turns a parsed panel into egui calls. It ports `imgui_adapter.cpp` behaviour and the pure rules of `imgui_layout.h` (label column share 40 percent with a 100 px floor at 16 px reference, control width floor 140, wide-container cap at 900 and 560, combo popup 640 wide and 12 rows, tooltip wrap 480, `Ellipsize` on UTF-8 boundaries, `NeutralizeHashes` is not needed in egui). Per kind: label, button (full width rule as in the adapter), toggle, slider (number, integer, integer-percent presentations), combo, list (selection, paging), text input (password masking), table (paging with "Rows a-b of n", Previous/Next), row/column containers (equal columns rule), tooltips, severity as text plus colour ("Info:", "Warning:", "Error:"), disabled state through ancestors. A page event from paging is `SCO_WIDGET_EVENT_PAGE` with `row`. Accessible names are the labels the contract already requires; node tags are kept in the model so tests can address nodes.

### 3.10 Capability switch readout (read-only)

Decision 4 puts the switches in the launcher: on by default in every release package, each a visible setting the player can turn off. The in-game menu only shows the state the game started with.

- The Settings page has a "Game capabilities" section. One row per switch the host lists through `scom_host_v1.switches`: the switch name, `On` or `Off`, and the host's text. The list comes from the same registry the launcher reads (sco-core `sdk/status_registry.json` and the host's own table), so the menu hard-codes no switch name and a switch added in Core appears without a menu change.
- Every row ends with the words "Not run in game." until the owner records a session for that capability. The menu does not show "verified" for any row.
- A switch that is Off shows one line: "Turn it on in the launcher, then start the game again." The menu has no toggle. Core reads the switches when the game starts, so a toggle in the menu would be a button that does nothing.
- The section never describes a switch as experimental or default-off. It states what is true now: on or off, and not run in game.
- Test: a `scom-testhost` list of three rows (On, Off, long text) draws as a golden at 100 and 125 percent. The text of each row is cut on a UTF-8 boundary with a tooltip, like any label.

## 4. What happens to `sco.ui` ImGui tabs

`sco.ui` is part of the frozen plugin ABI. It is not removed and not changed. Third-party plugins keep working. What changes is where their ImGui draws.

Facts: a `sco.ui` draw callback gets sc-offline's `ImGuiContext*` and runs on the game thread inside an ImGui frame. A Rust window cannot host that without a bridge. A bridge (render ImGui to a shared texture and forward input into it) would keep a permanent coupling to ImGui versions and was rejected: more code than the problem is worth.

Options:

| | Description | Verdict |
|---|---|---|
| 1. Keep a fallback ImGui window | The C++ menu stays compiled. When a loaded plugin has a `sco.ui` tab, the Rust sidebar lists it under "More plugins" (title and badge from `sco.ui`, read on the menu thread through `sco::ui::Tabs()`, an any-thread snapshot, and passed to Rust as a small list with a change hash). Selecting it calls `legacy_open(page_id)`: the Rust window hands over to the ImGui menu window (its own thread, created on first use), which shows one page with a "Back to menu" button, and `show_legacy_closed` hands back. One menu window at a time. Handoff rules are in 4.1. | **Recommended for third-party plugins.** Small, honest, no ABI change |
| 2. Port every in-tree consumer | Move `sco.ui` users that ship in sc-offline to `sco.widgets` 1.4 | **Do it for the in-tree ones.** Needed anyway (launcher plan section 9) |
| 3. Keep the ImGui menu as the only menu for any plugin with `sco.ui` | Mixed mode is off: if any legacy tab exists, run the ImGui menu | Rejected: the player loses the Rust menu because of one plugin |
| 4. Drop legacy tabs | Break third-party plugins | Rejected: breaks the frozen ABI promise |

Plan: options 1 and 2 together.

- In-tree users to port to `sco.widgets` (found by grep; each file is confirmed in M6, some hits may be build files that only mention ImGui): `src/builtins/multiplayer_ui.cpp` and `src/builtins/quantum_ui.cpp` (live `register_tab` users), `src/builtins/build_ui.cpp` (legacy draw code the 2026-11-15 audit says is not registered: delete, not port), and `plugins/build`, `plugins/quantum`, `plugins/spatial`, `plugins/titanlink`, `plugins/voxel_bridge`. The test plugin `ui_probe` stays on `sco.ui` on purpose: it is the legacy-path test.
- Overlays (`register_overlay`, drawn over the menu while it is open): there is no overlay surface in the Rust window. Overlays draw only in the legacy window and the docs say so. The only in-tree overlay user is `ui_probe`.
- Hotkeys (`bind_hotkey`) are unaffected: they run through the command registry, not the menu.
- The ImGui window and its D3D11 device are created lazily on the first `legacy_open`. A player with no legacy plugin never creates them.
- `docs/plugin-ui.md` gets a section: `sco.widgets` is the supported way to add a page; `sco.ui` tabs keep working in a separate legacy window.
- The ImGui code is removed only when two things hold: no in-tree `sco.ui` tab remains, and the owner decides third-party legacy pages are no longer worth a second window. Not planned now.

### 4.1 Window handoff rules

- Direct focus transfer. The window that closes calls `SetForegroundWindow` on the window that opens, never on the game in between. No game focus step, so no flicker, no cursor release and re-grab, and no exclusive-fullscreen disturbance from the handoff. The clip rect moves from one window to the other in one step.
- Distinct class names. The legacy window gets its own class, `starcitzenofflinemods_menu_legacy`. Only the Rust window keeps `starcitzenofflinemods_menu`, so find-by-class is unambiguous. If the Rust backend is off, the ImGui window keeps the original class name as today.
- One owner of `M` at a time. The poll runs in the Rust menu while it is the active window. During a legacy handoff, ownership passes to the ImGui menu (its own poll, as today) and returns on `show_legacy_closed`. A single atomic variable, `active_window`, held by the host, says which one it is. It is changed only by the window that is closing, with a compare-exchange, immediately before focus moves. It is never changed from the control thread, which only joins.
- Frame pump. `Menu_GameThreadFrame` calls `dispatch_events` on every game-thread pass in both modes, so an event queued just before a handoff is still served. While the legacy window is active it also runs the ImGui frame protocol (`menu_frame_protocol.h`) and the Rust thread idles. `Menu_Typing` and the Rust `is_typing` report the state of the active window (the Rust text-field state when `active_window` is Rust, ImGui's text-input state when it is legacy), never an OR of a window that is not active.
- Overlays exist only in the legacy window.
- Failure: if the legacy window cannot be created, the Rust menu stays up and shows "This page needs the classic window, which could not start", and `mod.log` has one line.

## 5. Test plan without the game

Everything in this section runs on a Windows machine or CI runner with no game and no GPU (Direct3D WARP). None of it is in-game evidence.

### 5.1 Interface tests (sco-core)

- `tests/abi_widgets_view.c`: compile-only layout pins from section 2.2 in C11 and C++20, including the new `stamps` signature, the table `version` field, and the 8-byte multiples of the three blob offsets.
- `tests/test_widgets_view.cpp`: with a real Core and synthetic plugin panels: list order and counts; `copy_panel` blob for every node kind and every 1.1 to 1.3 extension; paging; tags; password flag; disabled ancestors; limits (256 nodes, 255-byte strings, 256 items); size handshake including a panel that grows between calls; revision rules (event moves `revision` not `epoch`, `set_panel` moves both, removal and re-add never reuses a value); released owner gives `SCO_CRASHED` and drops out of the list; `query_service` for the name answers `SCO_NOT_FOUND`; after stop every call answers `SCO_UNAVAILABLE`.
- Event path: submit from a test thread registered as the menu thread (`HostWidgetsViewSetMenuThread`), dispatch on the game thread; ordering; `seq` starts at 1 and rises by 1, and `events_done` reaches it after the effect is in the revision (a copy taken after seeing `events_done >= seq` shows the new value); a replaced untagged panel (new epoch) drops the queued event and moves `event_drops`; a replaced tagged panel delivers a queued BUTTON, TOGGLE, SLIDER, COMBO and PAGE event to the live node with the same tag (with the live node's id); a replaced tagged panel whose node lost its tag, changed kind or is disabled drops it; an equal epoch with a node removed and another added under the same id and a different tag drops the event; an `update_nodes` or `remove_nodes` of other nodes between submit and dispatch does NOT drop a BUTTON, TOGGLE, COMBO or LIST event (stress test: one thread calls `update_nodes` in a loop while events are queued and dispatched; every event for an untouched node is delivered); an event for a removed, re-kinded or disabled node is dropped and counted; disabled node, wrong kind, out-of-range slider, over-long or invalid UTF-8 text are refused exactly as `RenderPanel` refuses them (the same table of cases is run against both entry points and must give identical outcomes); queue full gives `SCO_TOO_MANY`; coalescing never merges BUTTON/TOGGLE/COMBO/LIST/PAGE; a callback that replaces its panel drops the rest; a callback that faults gives `SCO_CRASHED` and other panels still get theirs; `dispatch_events` off the game thread gives `SCO_WRONG_THREAD`. Thread rule: `list_panels`, `copy_panel` and `submit_event` from an unregistered thread, or before any thread is registered, give `SCO_WRONG_THREAD` and write nothing. The proposed any-thread rule (2.4) is a separate test, run only after M1 task one and only if the race test passes.
- No render pass: a test plugin keeps a value only on a path that runs in `RenderPanel` today (a lazy refresh on draw). In Rust mode it gets no such call; the test records this as the documented behaviour, and a second plugin that refreshes from its own events keeps working.
- Stop versus copy: the menu thread loops `copy_panel` and `submit_event` while the control thread runs the real order: Core stop, `DrainRequestedStop`, then (later) `Menu_Drain`. The join is deliberately after stop. Acceptance: calls made after stop answer `SCO_UNAVAILABLE`, the table and client memory are still valid (checked under the repository's sanitizer or a guard-page allocator for the client), every call returns a result code, no deadlock.
- Ack under delay (synthetic, no game): a delayed dispatch (the game pass runs 300 ms after the submit) and a plugin revision bump in the gap; `events_done` stays below `seq` until the pass, the copied value in the gap is the old one, and after the pass it is the new one.
- Alignment: `copy_panel` and `list_panels` with a misaligned pointer give `SCO_BAD_ARG`.
- Race test: one thread mutates panels while another loops `stamps`/`list_panels`/`copy_panel` and a third submits events. Run under the repository's existing sanitizer or stress setup. Acceptance: no torn blob (every blob parses and has one revision), no deadlock, no use after stop.
- Fixture export: the test writes the blobs it built to `tests/fixtures/widgets_view/*.bin` plus a `.json` description of the tree. These are the shared fixtures for the Rust side.

### 5.2 Rust unit and property tests (no window)

- `scom-view` parses every C++ fixture and the parsed tree equals the `.json` description. Layout constants asserted at compile time with the section 2.2 numbers.
- Parser fuzz: random bytes, truncated blobs, offsets past the end, `nodes_offset` or `items_offset` not a multiple of 8, overlapping ranges, invalid UTF-8, oversized counts, `node_stride` below the minimum, a cycle in parents. Property: never panics, never reads out of bounds, returns an error. `cargo fuzz` target kept in the repository, plus a bounded `proptest` run in normal CI.
- `scom-nav`, `scom-window` and the panel grouping: the vectors of the C++ tests (`menu_navigation_test`, the menu window tests, `menu_frame_windows_test` for the escape logic) become shared data files read by both languages. Same inputs, same outputs, including the `.ini` readers' refusal of unknown keys.
- `scom-render` layout rules: the pure functions are tested with the same vectors as `tests/test_widgets_layout.cpp` in sco-core (label column, control width, popup limits, `Ellipsize` including cut points inside multi-byte text).
- Event tests with `scom-testhost`: render a synthetic panel to egui with scripted input (no window: egui `RawInput` fed directly), assert the exact `sco_view_event` that comes out for each widget kind, one event per node per frame, no event for disabled nodes, paging events carry `row`, epoch is the epoch of the copy drawn, a tagged node's event carries its tag. Optimistic state: with a fake host that delays the ack by 300 ms and bumps the revision at 100 ms, the value egui shows for a dragged slider and a toggled toggle never returns to the old value, and equals the host's value one frame after `events_done` reaches the `seq`; with a host that drops the event, the node snaps to the host value once and the "try again" line shows.

### 5.3 Golden pictures

- Source of pages, two of them. (a) A blob-dump source of its own: the Core fixtures from 5.1 (`tests/fixtures/widgets_view/*.bin`) plus checked-in synthetic panels. This works in this branch and does not depend on any other. (b) The panel dumps that the plugins' synthetic workflow tests write (`SCO_PANEL_DUMP`, `tools/panel_dump.h`, used by `tools/menu_preview/run.py`). Those files exist only on the sidebar branch (`feature/menu-player-organization`), so source (b) comes with that merge; until then only (a) runs. `scom-preview` reads the same dump file as a `PanelSource` and draws the whole shell (sidebar, header, page, status strip) offscreen with the D3D11 painter on WARP at a fixed size (1000 x 800 at 96 dpi, plus 1250 x 1000 at 125 percent), then writes PNG with WIC. No window, no GPU.
- Fixed inputs so pictures are repeatable: a test-only font (the OFL font that egui already bundles, embedded behind a `test-font` cargo feature that a CI grep keeps out of every release build, the same check as `fault-injection`), not the machine's Bahnschrift. Goldens therefore do not wait for launcher decision 10 (the owner-approved Cyrillic font for release); a Cyrillic golden is added when that font is approved. Fixed clock, no background image except one checked-in synthetic image for the background test, status text fixed.
- Compare against checked-in goldens with a tolerance (per-channel difference at most 2 of 255 on at most 0.1 percent of pixels). Goldens are recorded on WARP only, and a manifest beside them records the runner image and the file version of `d3d10warp.dll` they were made with. A different WARP version on a runner fails the job with a clear message ("goldens were made with WARP x, this runner has y"), and the goldens are re-recorded by a human in a PR, not widened by tolerance. A failed comparison uploads the new picture and a diff as a CI artifact. Goldens are updated by a human in the same PR that changes the look.
- Cases: one picture per page id in the dump; each widget kind in each state (disabled, severity, paged table, password, long strings, empty panel, 256-node panel); sidebar with developer pages hidden and shown; unknown plugin panel under "More plugins"; combo popup open; background image on and off; 100, 125 and 150 percent scale.
- Parity review, not automatic: the existing ImGui preview PNGs and the Rust ones are put side by side in one contact sheet per page. Text rasterisation differs, so this is a human check for layout, spacing and colours. The owner signs off the sheet before the default flips (section 6, M7).
- The scuba-engine preview host (`--capture-dir`, `docs/engine/preview-host.md`) keeps rendering the ImGui look and stays the reference for the old renderer. A follow-up there (separate repository, separate review): a `--menu-module scom.dll` mode that runs real plugins against the synthetic mock game, loads `scom.dll` through `scom_open` with a real Core view table, and captures the menu texture. That is the first test where real plugins, real Core and the Rust menu meet without the game. It is not on the critical path: until it exists, `scom-preview` plus the Core tests above are the evidence.

### 5.4 Window and input tests

- A windows-only test creates the menu window hidden or off-screen and posts synthetic messages to its window procedure (mouse, wheel, keys, `WM_CHAR` including surrogate pairs, focus loss, `WM_DPICHANGED`, `WM_SIZING`, `WM_CLOSE`). It checks: the Esc state machine, `M` edge and typing gate, clip released on focus loss, saved size written once at the end of a resize and never on a pure move, hide gives focus back to a stand-in game window, no message reaches egui after `request_stop`.
- Panic on the menu thread: with the `fault-injection` feature, a panic on the menu thread entry and inside the painter loop (not only at the FFI edge) must hide the window, run `ClipCursor(NULL)` (checked through a stand-in clip counter) and mark the menu broken.
- Handoff (4.1) with synthetic windows: focus order Rust to legacy and back never passes through the stand-in game window; exactly one window visible at any time; clip rect owned by one window; `M` accepted by the active window only; the two class names differ.
- Shutdown with a blocked game window: a stand-in game window whose thread is blocked inside the join. `request_stop` then `drain` must return (joined) with the menu visible, with no cross-thread message sent to the stand-in window and no hang. Also run with the menu hidden.
- Settings and tabs: change a setting in the Rust window and check the legacy window shows it after `legacy_open`, and the reverse; a tab registered after the menu started appears in "More plugins" within one poll interval (33 ms visible), including a badge-only change; the host callbacks marked `menu` return without waiting for the control thread (the control thread is held blocked in the test); `legacy_open` returns in under 1 ms and the legacy window is visible well under 200 ms later (the 200 ms stop-event wait of the control loop is never involved).
- Lifecycle: `start`, `request_stop`, `drain` timing and the `WrongThread` / `Busy` cases ported from `menu_frame_windows_test`. A stop during a built frame and a drain with a stuck thread both leave handles and the module pin intact.
- Fault containment: a `fault-injection` cargo feature (never in a release build, checked by a CI grep of the release `Cargo` features) makes the menu panic on a command. Test: the panic is caught at the FFI edge, the host sees a nonzero code, no process abort, the window hides, later calls are refused cleanly.
- Fallback selection test in the C++ host test target: missing `scom.dll`, wrong hash, wrong `abi`, `scom_open` failure and a Core without the view table each give the ImGui menu and one log line.
- Legacy path: `ui_probe` (a `sco.ui` tab and overlay) listed under "More plugins", `legacy_open` shows the ImGui window, "Back to menu" returns.

### 5.5 CI and supply chain

A `rust-menu` job beside `rust-launcher`: build with the pinned toolchain, `cargo test`, `cargo deny`, the fuzz smoke run, golden compare on WARP, `unsafe` count report per crate (a diff above a threshold needs a comment in the PR), release build size report, and a check that `scom.dll` imports only expected DLLs.

## 6. Phases, acceptance checks, owner-run items

Order matters. M1 is a Core change and is the long pole: start it first, in parallel with M0.

| Phase | Work | Acceptance (all synthetic, build or CI) |
|---|---|---|
| M0. Spike, 1 to 2 weeks | In a throwaway `menu-rs/spikes/` crate: Win32 window + egui + the D3D11 painter drawing one hard-coded panel, hardware and WARP, software cursor, PNG capture, panic caught at an FFI edge in a test cdylib. Measure: DLL size, crate count, `cargo deny`, CPU per idle frame, whether the process DPI awareness changes (it must not). Also two go/no-go probes: (a) the software cursor still draws and tracks when the host has hidden the OS cursor (`ShowCursor` count below zero, `SetCursor(NULL)`), checked with a stand-in game window; (b) a second D3D11 device and its swapchain in a process that already has a stand-in game device, with a stand-in overlay hook on `Present`, causes no error or visible conflict. Both are synthetic; real overlays are owner-run (item 13). | Report committed with numbers, including the two probes. Written go/no-go for shape A versus B (a failed probe is a no-go for A). If no-go, M3 changes to shape B: `PanelSource` over shared memory, plus the second event encoding (text by offset) and its fuzz tests; the rest of the plan stands |
| M1. Core interface, 2 to 3 weeks | `sco_widgets_view.h`, the implementation, `RenderPanel` event split, `HostWidgetsView`, ABI pins, `test_widgets_view.cpp`, fixtures, `docs/widgets-view.md`, `sdk/contracts/sco_widgets_view_v1.json` and the Inventory row and six per-method rows in `docs/contracts.md` generated to match it, with `tools/sdk_catalogue.py check` passing (2.7), the second header in the released-golden check (2.7), the one-line `docs/stability.md` note, the count of in-tree `set_panel` calls made on a tick (2.4), status records, host-source kit includes the header. Held for 2.4.1 with the other items. The header text of 2.2 was compiled once as C11 and C++20 with clang against the repository's `sco_widgets.h`, with static asserts for every size and key offset in the pins table (all passed). That check is repeated by `tests/abi_widgets_view.c` in M1. Never run in game | All section 5.1 tests pass (including the stale-event stress, tag rematch, ack-under-delay and stop-versus-copy tests); existing widget tests unchanged and green; `query_service` refusal test; kit package check includes the header in host-source and not in the plugin kit (if the owner agrees); no change to `sco_api.h` or `sco_widgets.h` |
| M2. Rust model and renderer, 3 to 4 weeks | `scom-view`, `scom-nav`, `scom-window`, `scom-theme`, `scom-render`, `scom-testhost`; shared vector files; first goldens through a software path or the M0 painter. Needs the sidebar shell branch merged | Section 5.2 tests pass; fuzz smoke clean; layout vectors identical to sco-core's; `forbid(unsafe_code)` on these crates |
| M3. Window, painter, input, 3 to 4 weeks | `scom-d3d11`, `scom-win`, `scom-app`, the `scom` cdylib, `scom-preview`, the full shell, Settings page, status strip, background image | Section 5.3 goldens pass on WARP at 100/125/150 percent; section 5.4 window tests pass; cdylib exports only `scom_open`; no `DllMain` work; release size within the M0 budget |
| M4. Host integration, 2 weeks | C++ side: backend setting, `scom.dll` load with hash check, `scom_host_v1` and `scom_module_v1`, `Menu_Start/Drain/Typing/GameThreadFrame` mapping, fallback rules, retain-until-exit, release manifest and packaging, `docs/build.md`, `docs/features.md` notes | Host builds with and without the Rust menu; fallback tests pass; `menu_frame_windows_test` family passes for both backends; release profile lists `scom.dll` with its digest; default stays `imgui` |
| M5. Legacy pages, 1 to 2 weeks | `legacy_open` and `show_legacy_closed`, "More plugins" entries from `sco.ui` tabs (hash poll), lazy ImGui window on its own thread with its own wake event, atomic `active_window` handoff (the control loop in `dllmain.cpp` is not changed), `docs/plugin-ui.md` section | `ui_probe` test passes through the Rust menu; a build with no legacy plugin never creates the ImGui device (asserted by a test hook counter) |
| M6. Port in-tree `sco.ui` users, 3 to 4 weeks | Port the files listed in section 4 to `sco.widgets` 1.4 one plugin per PR, delete dead `build_ui.cpp`; each plugin's synthetic workflow test and panel dump updated | Each plugin's tests pass and its dump renders in `scom-preview` without visible loss against its ImGui picture; no in-tree `register_tab` left except `ui_probe` |
| M7. Parity and polish, 2 weeks | Contact-sheet parity review, DPI review, Russian and English strings audit of the new Settings text (including the capability rows), keyboard-only walkthrough, performance counters, docs ("not run in game" wording kept) | Owner signs the contact sheets; golden set complete; no open parity defect marked blocking |
| M8. Owner in-game session, then default flip | Owner runs the list below. The default changes from `imgui` to `rust` only after the owner records the result. Part of the 2.4.1 hold: the release job stays blocked until this is recorded | Owner-recorded result in `docs/status.md`; the pending acceptance ledger updated |

Total about 14 to 20 weeks of work, with M0 and M1 overlapping and M2 partly overlapping M1. The sco-core 2.4.1 release waits for all of it, per decision 2. This is a long hold. The cheaper cut that still meets decision 2 is to land M1 early as a 2.4.1 release candidate item and keep the Rust menu non-default until M8, which is the plan above.

### Owner-run in game (none of these has been run)

An agent cannot do these. Each result is recorded as pass, fail or not run, with the game build, the sc-offline build and the `scom.dll` digest.

1. `M` shows the menu above the game and gives it focus; Esc, `M` and the close button hide it and return focus and input to the game. Check windowed, borderless and exclusive fullscreen.
2. The cursor is clipped to the menu while it is open and free again on hide, Alt+Tab, size and move.
3. Typing in a text box does not trigger game or plugin hotkeys; `M` and Esc behave while typing (`M` types, Esc closes).
4. At 100, 125 and 150 percent scale, on the primary and on a second monitor, with the game's own DPI setting changed: the window opens at a sane size, text is readable, a drag to the other monitor rescales, the saved size comes back after restart.
5. Real panels do real things: each shipped page changes the game state it names (Gear, Saved outfits, Travel, Saved spots, Ships & vehicles, Build, Creative, Crew, Characters, Multiplayer) or says plainly that the capability is unavailable. This ties to the [pending acceptance ledger](status.md); the menu does not prove a feature works, it proves the click reaches the plugin.
6. The capability switches on the launcher's Capabilities page (on by default, each can be turned off, text says not run in game) match the "Game capabilities" rows on the menu's Settings page after a game start: switch one off in the launcher, start the game, and the row says Off.
7. Performance: game frame time with the menu hidden, open and idle, open and dragging a slider; a 100-node panel; a 20-minute soak with the menu open and closed repeatedly. Record numbers, not impressions.
8. Fault behaviour: with a fault-injection dev build of `scom.dll`, the game keeps running, the menu is gone, `mod.log` says why. With `scom.dll` deleted, the ImGui menu comes back by itself with one log line.
9. A `sco.ui` plugin (`ui_probe`): its page appears under More plugins, opens the ImGui window, and Back returns to the Rust menu. Overlays show only in the legacy window.
10. Game exit and `Menu_Drain`: the game closes cleanly with the menu open and closed; no hang, no crash dialog, no stuck topmost window.
11. Wine or Proton, best effort (launcher decision 17): `M` opens the menu, the window can be clicked and typed into, hide returns to the game. A failure here is recorded, not a release gate.
12. Anti-cheat and hosts: the menu adds no new file, process, network call or PC change. The existing PC changes (firewall block, EAC hosts line, EAC exe rename) are unchanged by this work and still need the owner security review before cutover (launcher decision 8).
13. Overlays and cursor: with each overlay the owner normally runs (graphics-card, chat, capture or monitoring overlays), the menu opens, draws, takes the click and closes without a crash, flicker or a lost game frame; and the menu's software cursor is visible and correct when the game has hidden the OS cursor. Record which overlays were present.

## 7. Open decisions for the owner

1. Shape A (in-process `scom.dll`, recommended) or shape B (separate process). Section 3.1. M0 gives the numbers; the owner decides after it.
2. The new interface ships in the host-source kit only (recommended) or also in the plugin SDK kit. Shipping it to plugins makes it a public promise and a privacy exposure (section 2.1).
3. Software cursor drawn by the menu (recommended, parity) or the OS cursor.
4. Release font: needs launcher decision 10 (an owner-approved Cyrillic OFL or MIT font). Goldens do not wait for it: they use egui's bundled OFL font behind the test-only `test-font` feature (5.3). The menu uses the machine fonts first until the release font is approved.
5. `scom-theme` reads the scuba-preview-theme JSON from scuba-engine, or sc-offline owns the file and scuba-engine copies it. Recommended: sc-offline owns it (the product is the source of the look), and a check keeps the engine copy equal.
6. Default backend flips to `rust` only after M8, or stays opt-in for one more release. Recommended: flip only after the owner records M8.
7. Whether M6 (porting in-tree `sco.ui` users) is a gate for 2.4.1 or can follow. Decision 2 says nothing is after 2.4.1, so the plan treats M6 as inside the hold. The owner may relax it for the plugins that are not shipped in the player package.

8. Password fields. The blob omits the text of a password node (2.4), so the Rust field shows no dots for an existing value and only accepts new input. Today's ImGui menu shows dots. This is a small parity loss that keeps secrets out of the blob; the owner accepts it or asks for a length-only field (a count of dots, no text).
9. Golden pin gap. Until the first release containing `sco_widgets_view.h`, only the compile-time asserts pin the header (2.7). The owner accepts the gap, or asks for the first golden to be recorded at the first release candidate.
10. Contract record route (2.7): add the table to `sdk/contracts` with a host-source marker (recommended) or allowlist the header and keep only prose rows.

Not decided here and not changed by this page: the capability switch defaults (decision 4 stays: on by default in every release package, each a visible setting, each saying it has not been run in game), the catalogue (decision 3), and the PC changes.
