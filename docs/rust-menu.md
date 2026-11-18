# Rust in-game menu: design

Status: design proposal for owner review. Nothing in this page is built. Nothing is claimed to work in game. Every acceptance check below is source, build, synthetic or CI evidence. Docs keep the words "not run in game" until the owner records a session.

Revised 2026-11-17 after review. Changes: the header moves to `include/sco/widgets_view.h` with a stated catalogue, ABI-test, packaging and provenance route (2.1, 2.7); the tag limit is `SCO_WIDGETS_MAX_TAG` (63), never the caps index 9 (2.2); the menu thread registers itself through the C table (2.2, 2.4); events are matched across a `set_panel` republish by a node key, with the exact rules and tests (2.3.1, 5.1; the key is compared at equal epoch too, owner decision 11); `events_done` under coalescing is defined with examples, and `node_key` is part of the merge condition (2.3.2); the capability readout follows the owner's switch defaults (3.10). Scope: design only, no code.

This page belongs to the [Rust launcher plan](rust-launcher.md). It covers owner decision 1 of 2026-11-17: the in-game menu is rebuilt in Rust now, reads `sco.widgets` panels through a new versioned C interface in sco-core, and the in-game host (dinput8.dll, hooks, Core host kit) stays C++. Decision 2 applies: sco-core 2.4.1 is held until the Rust launcher, Rust menu, catalogue and reference launcher are in it. So the new interface below is a 2.4.1 item, not a later one: nothing in this page is "after 2.4.1".

## Owner decisions applied (2026-11-17)

| Decision | How this page follows it |
|---|---|
| 1. Rust is the launcher and the in-game menu, both now. The host (dinput8.dll, hooks, Core host kit) stays C++. The Rust menu reads `sco.widgets` through a new versioned C interface in sco-core. | Section 2 is that interface. Section 3 is the menu. Nothing in the host moves to Rust. |
| 2. sco-core 2.4.1 is held until the Rust launcher, Rust menu, catalogue and reference launcher are in it. | Nothing here is "after 2.4.1". The interface (M1) and the menu (M2 to M8) are 2.4.1 work. The release job stays blocked until M8 is recorded. |
| 3. Catalogue: signed index, disabled on pull, native-code warning. | Not changed by this page. Plugin pages from a pulled plugin appear in the menu only after the player enables it. |
| 4. The five capability switches (`exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native`, `exp_audio_native`) are on by default in every release package. Each is a visible key the player can turn off, and each says it has not been run in game. `exp_entities_diag` (diagnostic hooks that patch game functions; not a feature) is off by default. | The menu never defaults a feature switch off and never calls one experimental. `exp_entities_diag` is shown apart, as a diagnostic that is off. Section 3.10 is a read-only readout. Changes are made in the launcher and apply at the next Play. |

## What the owner must run in game (short list)

None of this has been run. The full list with numbering is in section 6; the build, hash and result of each run go in `docs/status.md`.

1. `M` opens the menu above the game with focus; Esc, `M` and the close button hide it and return input to the game (windowed, borderless, exclusive fullscreen). The cursor is clipped to the menu while open and free after hide and Alt+Tab.
2. A click reaches a plugin exactly once, also on a page whose plugin republishes its panel on a timer (item 14). This is the check for the republish rule in 2.3.1.
3. Typing in a text box does not fire game hotkeys.
4. The window scales at 100, 125 and 150 percent and across two monitors.
5. The five capability switches show On after a game start, `exp_entities_diag` shows Off as a diagnostic, and turning one off in the launcher shows Off after the next start (items 6 and 15).
6. Overlays you normally run do not break the menu, and the software cursor is correct when the game hides the OS cursor.
7. With `scom.dll` deleted the ImGui menu returns by itself; a fault-injection build hides the menu and the game keeps running.
8. The game closes cleanly with the menu open and closed; a `sco.ui` page opens in the classic window and returns.

## 0. Summary

1. sco-core gets a new host-side reader, `sco.widgets.view` 1.0 (section 2), as a C11 host-source header at `include/sco/widgets_view.h`. It lists panels, copies one panel as a flat, relocatable byte blob (nodes, items and strings with offsets, no pointers), queues one event, and exposes revision counters. It is not a plugin service and not part of the plugin SDK: only the host menu identity can get it, and the menu thread registers itself through the table (2.2).
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
- Each panel entry holds a shared immutable snapshot, rebuilt lazily (at most once per change) and a `generation` that moves on every plugin mutation (`set_panel`, `update_nodes` and `remove_nodes` all call `Changed()`). `RenderPanel` drops the rest of a pass when the generation moved. `dispatch_events` does not keep that break (2.3.1, "Within one drain").
- A panel enters tagged-event mode (1.4) once any node has a tag. Node tags (1.2) are copied into snapshots but never rendered. They are the automation handle used in section 5.
- Limits: 64 panels, 256 nodes per `set_panel`, 4096 nodes total, 256 items, 16 columns, 255 bytes per text, 63 bytes per id and title, 256 events per pass.

## 2. The new sco-core C interface: `sco.widgets.view` 1.0

### 2.1 Why a new interface

The Rust menu runs on its own thread and cannot link the C++ host kit (STL types, no C ABI). It needs the same data `ImGuiRenderer` gets, as C. Three design choices follow.

1. **Flat blob, offsets not pointers.** One panel is copied into one caller-owned buffer. All strings are `(offset, length)` pairs into a string table in the same buffer. No pointer into plugin or Core memory ever reaches the menu. The blob is relocatable: it can be written to a file (fixtures), compared byte for byte, or sent over shared memory (fallback shape B) unchanged.
2. **Reads off the game thread, event dispatch on the game thread.** In 1.0 the read calls are specified for the menu thread only (`list_panels`, `copy_panel`, `submit_event`). "Any non-game thread" is proposed and is not promised until the M1 race test passes (2.4). Widening a thread rule later is compatible; narrowing it is not, so 1.0 starts narrow. `capabilities` and `stamps` touch only atomics and stay any-thread. Reading takes the widget lock for a pointer copy of the shared snapshot, then flattens outside plugin code. It runs no plugin callback. Events are queued by the menu thread and dispatched by the host on the game thread, through the same validation and callout guard as `RenderPanel`. This removes the per-frame game-thread hand-off for panels: the menu stays responsive when the game thread is busy.
3. **Not a plugin service.** If `query_service("sco.widgets.view")` worked, any plugin could read every other plugin's panels and inject player clicks into them. So the table is never published through `query_service`. The host gets it from the reserved host-menu identity (section 2.5). A test pins that a plugin's `query_service` answers `SCO_NOT_FOUND`.

Where the header lives. Source check of sco-core `4ba313d`: `tools/sdk_catalogue.py` collects only top-level `include/*.h` (`header_inventory` and `documentation_signatures` both glob `include/*.h`, and its docstring says top-level C headers are the public boundary, not `include/sco/`). Every table found there needs a record in `sdk/contracts`, a row in `docs/contracts.md`, a service name and a stability class. A host-only table must not enter that set, so the header is **`include/sco/widgets_view.h`**, beside `include/sco/host_menu.h`, the other host-source seam. Both are `core-source` entries in `sdk/components.json` without `public_header`; `host-source-sc` includes `core-source`; the plugin kits (`core`, `game-sc`, `legacy`) do not list them. sc-offline's `tools/sdk-headers.py` counts only `public_header: true` components of `core` and `game-sc`, so plugins and `src/builtins/` cannot include it, while the host glue under `src/` can.

The header is C, not C++, because a Rust consumer needs a C interface. Rust does not read the header at build time. It carries hand-written `#[repr(C)]` mirrors with compile-time size and offset asserts, and a probe that compiles the header from the pinned Core export keeps the mirrors honest (2.7). So the header does not need to be on any Rust include path, and no bindgen or C compiler is part of a product build. The kit decision (host-source only) is the owner's (section 7).

### 2.2 Proposed header text

File `include/sco/widgets_view.h` in sco-core. This is the exact proposed text. Plain C11 inside `extern "C"`; it compiles as C and as C++.

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * sco/widgets_view.h: the host-side flat reader of "sco.widgets" panels, version 1.0.
 *
 * Host-source seam, not a public SDK header: it lives in include/sco/, is not listed in the
 * plugin SDK kits and has no entry in the SDK catalogue. It is NOT a plugin service and is
 * never returned by query_service. The host obtains it through sco/host_menu.h and may hand it
 * to a menu renderer written in any language. No renderer type, plugin pointer or Core pointer
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
    uint32_t node_stride;   /* sizeof(sco_view_node): 120 */
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

/* Node key. A 64-bit FNV-1a hash (offset basis 0xcbf29ce484222325, prime 0x100000001b3) that
 * Core computes in one place, over these bytes in order:
 *   a tagged node:    the byte 'T', the kind as 4 bytes little endian, the tag bytes
 *   an untagged node: the byte 'U', then kind, id and parent as 4 bytes little endian each,
 *                     then the label bytes
 * (tag and label without a NUL; a computed 0 is stored as 1). A renderer never computes a
 * key: it copies the blob node's key into the event it sends. The key names "the same
 * control" across a set_panel republish (see sco_view_event). A tag is unique within a panel
 * (widgets 1.2) and at most SCO_WIDGETS_MAX_TAG (63) bytes. */
typedef struct sco_view_node {
    uint32_t size;          /* 120 */
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
    sco_view_str tag;       /* widgets 1.2 tag, at most SCO_WIDGETS_MAX_TAG (63) bytes;
                               empty when untagged */
    uint64_t key;           /* the node key above, as of this copy */
} sco_view_node;

/* ---- one event, from the menu to a plugin ----
 * The same candidate a renderer gives RenderPanel, plus the epoch of the copy the player
 * acted on and the key of the node acted on, both copied from the blob. There is no pointer
 * for a tag: the key carries the node's identity. Matching at dispatch, against the live
 * panel of that id at the moment of dispatch:
 *  1. The panel is gone or its owner was released: dropped.
 *  2. epoch equal (no set_panel since the copy): the live node with node_id must exist, have
 *     the same kind, be enabled including its ancestors, and have a key equal to node_key.
 *     The live key is the stored key of a tagged node and the computed key of an untagged
 *     one, so the check is the same for both. A node removed and replaced by another control
 *     under the same id (another tag, or another label or parent) cannot receive the click.
 *     update_nodes of other nodes, or a new value on this node, does not change its key and
 *     never drops the event. An update_nodes that relabels an untagged node does change its
 *     key: the event is dropped (a tag keeps a control clickable across a relabel).
 *     Residual, stated: a plugin that removes an untagged node and adds a node of the same
 *     kind, id, parent and label back between copy and dispatch gets the click; that node
 *     has the same key, so it is the same control.
 *  3. epoch different (set_panel replaced the panel after the copy): the target is found by
 *     key. First the live tagged node whose key equals node_key; failing that, the live
 *     node with id node_id whose computed key equals node_key (same kind, id, parent and
 *     label as the copy, untagged). The target must have the event's kind and be enabled
 *     including its ancestors. It receives the event with ITS id, whatever the id was in
 *     the copy. No target: dropped.
 *  4. The host then validates the rest exactly as RenderPanel does, against the live node:
 *     value in range, selection or page row in range of the live items, text <= 255 bytes
 *     and valid UTF-8. The callback tag is always copied from the live node, never taken
 *     from the menu. One event is delivered to at most one node and at most once. */
typedef struct sco_view_event {
    uint32_t size;      /* 56 */
    uint32_t node_id;   /* the node's id in the copy */
    uint32_t kind;      /* the node's kind, or SCO_WIDGET_EVENT_PAGE */
    int32_t index;      /* COMBO/LIST selection, else 0 */
    double value;       /* TOGGLE/SLIDER, else 0 */
    const char* text;   /* TEXT_INPUT only: UTF-8, NUL-terminated, copied during the call; else NULL.
                           The one non-relocatable field (64-bit hosts only). */
    int32_t row;        /* PAGE: the requested first row; else -1 */
    uint32_t _reserved; /* zero */
    uint64_t epoch;     /* the epoch of the blob the player acted on. 0 is invalid:
                           submit_event answers SCO_BAD_ARG (a real epoch is never 0) */
    uint64_t node_key;  /* the key of the target node in that blob. 0 is invalid
                           (a real key is never 0) */
} sco_view_event;

/* capabilities(): caps[SCO_VIEW_CAP_COUNT] is the array length on entry (>= 2) and the
 * number written on return, as in sco.widgets: the host writes min(length, the entries it
 * has) and the caller reads only the entries counted. A caller with a short array gets a
 * prefix, never an error. Size negotiation: a consumer checks table `version` and `size`,
 * blob `version` and these counts; there is no minimum-version argument. */
#define SCO_VIEW_CAP_COUNT 0u
#define SCO_VIEW_CAP_FEATURES 1u
#define SCO_VIEW_CAP_MAX_PANELS 2u
#define SCO_VIEW_CAP_EVENT_QUEUE 3u  /* slots the queue holds before submit_event answers SCO_TOO_MANY */
#define SCO_VIEW_CAP_NODE_STRIDE 4u  /* sizeof(sco_view_node) */
#define SCO_VIEW_CAP_ENTRY_STRIDE 5u /* sizeof(sco_view_panel_entry) */
#define SCO_VIEW_CAPS_1_0 6u         /* array length that covers every 1.0 index */
#define SCO_VIEW_FEATURE_TAGS 0x1u     /* blob tags are filled in (widgets 1.2 and later) */
#define SCO_VIEW_FEATURE_COALESCE 0x2u /* submit_event merges a SLIDER or TEXT_INPUT event into an
                                          adjacent queued one for the same node, keeping the last
                                          value (see submit_event) */
#define SCO_VIEW_FEATURE_HOST_PANELS 0x4u /* SCO_VIEW_PANEL_HOST_OWNED is reported */

typedef struct sco_widgets_view_v1 {
    uint32_t size;    /* sizeof(sco_widgets_view_v1): 72 */
    uint32_t version; /* SCO_WIDGETS_VIEW_VERSION_1_0. A consumer checks this and `size`.
                         Functions are only ever appended. Each append raises the minor
                         macro (SCO_WIDGETS_VIEW_VERSION_1_1, and so on) and grows `size`:
                         `version` says which contract revision the host implements, `size`
                         says which functions exist. */

    /* Any thread. See SCO_VIEW_CAP_*. SCO_BAD_ARG: NULL client or caps, or caps[0] < 2.
     * SCO_UNAVAILABLE: client stopped. */
    sco_result (*capabilities)(sco_view_client* client, uint32_t* caps);

    /* Any thread, O(1), no allocation. Writes the counters (see "Counters"); any pointer
     * may be NULL. event_drops is the running total of queue slots dropped by
     * dispatch_events (never reset; a slot that merged several events counts once).
     * events_done is the sequence number of the last event of the newest slot that has left
     * the queue, whether the slot was delivered or dropped. It is 0 before the first slot
     * leaves, it never decreases, and it can jump by more than 1 when a slot covers several
     * merged events. Every event with a sequence number <= events_done is finished: delivered
     * (the newest value of its slot), superseded by a merge, or dropped, and the effect of a
     * delivered event on the panel is already in that panel's revision. A caller polls this
     * every frame and does more work only when a counter moved. SCO_UNAVAILABLE: client
     * stopped. */
    sco_result (*stamps)(sco_view_client* client, uint64_t* global_revision, uint64_t* list_revision,
                         uint64_t* event_drops, uint64_t* events_done);

    /* Menu thread registration. The thread that will call list_panels, copy_panel and
     * submit_event calls menu_thread_enter first, before it does anything else with the
     * table; Core records the calling thread (the host passes no thread id).
     * SCO_OK: registered, or this thread already is. SCO_WRONG_THREAD: the caller is the
     * game thread. SCO_FAILED: another thread is registered (one at a time; it is released by
     * its own menu_thread_leave, or by the host after it has joined that thread). SCO_BAD_ARG:
     * NULL client. SCO_UNAVAILABLE: client stopped. Never runs plugin code. This is a
     * correctness guard (it keeps panel reads and event submission to one named thread), not a
     * security boundary: native code in the process could do anything anyway, and no plugin can
     * obtain this table. */
    sco_result (*menu_thread_enter)(sco_view_client* client);

    /* Releases the registration. SCO_OK: released, or nothing was registered.
     * SCO_WRONG_THREAD: another thread is registered; nothing changes. SCO_BAD_ARG: NULL
     * client. Works after stop (the thread must be able to leave) and never answers
     * SCO_UNAVAILABLE. The menu thread calls it on every exit path, including a panic. After it,
     * list_panels, copy_panel and submit_event answer SCO_WRONG_THREAD. */
    sco_result (*menu_thread_leave)(sco_view_client* client);

    /* The registered menu thread only (any non-game thread is proposed, not promised; see
     * 2.4). Writes live panels in the host's listing order (registration order).
     * out/stride: stride >= sizeof(sco_view_panel_entry) and a multiple of 8 (BAD_ARG
     * otherwise); *inout_count is the capacity in entries on entry and the entries
     * written on return. If capacity is too small the host writes none and sets
     * *inout_count to the entries needed, with SCO_TOO_MANY. Panels whose owner was
     * released after a fault are not listed. SCO_WRONG_THREAD: the caller is not the
     * registered menu thread; nothing is written. */
    sco_result (*list_panels)(sco_view_client* client, sco_view_panel_entry* out, uint32_t stride,
                              uint32_t* inout_count);

    /* The registered menu thread only (any non-game thread is proposed, not promised; see
     * 2.4). Flattens one panel into out (8-byte aligned, BAD_ARG otherwise). The
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

    /* The registered menu thread only (any non-game thread is proposed, not promised; see
     * 2.4). Checks the shape of the event, copies it and queues it for the game thread. It
     * runs no plugin code and does not validate against the live node: that happens in
     * dispatch_events. Events are never dropped silently: a drop adds to the event_drops
     * counter of stamps. SCO_NOT_FOUND: no such panel. SCO_BAD_ARG: bad size, unknown kind,
     * non-finite value, text over 255 bytes or not UTF-8, nonzero reserved, epoch 0 or
     * node_key 0. SCO_TOO_MANY: the queue is full and the event could not be merged (the
     * event is not queued and takes no sequence number). SCO_UNAVAILABLE: client stopped.
     * SCO_WRONG_THREAD: not the registered menu thread; the event is not queued. Returns
     * before the plugin sees the event.
     * Sequence numbers. On SCO_OK, *seq (may be NULL) receives the event's sequence number: 1
     * for the first accepted event of the client, then +1 for each accepted event, with no
     * gaps. The queue is a list of slots in first-in first-out order; every slot covers a range
     * of sequence numbers, and the ranges of consecutive slots follow each other without a gap.
     * Coalescing, at submit time. If the event is a SLIDER or TEXT_INPUT event and the newest
     * slot is still queued (dispatch_events has not taken it) and holds a SLIDER or TEXT_INPUT
     * event with the same panel id, node_id, kind, epoch and node_key, the new event is merged
     * into that slot: the slot keeps its place, takes the new value, and its range now ends at
     * *seq. node_key is part of the condition because a different key is a different control
     * (the matching rule at sco_view_event): an event with another key takes a new slot and is
     * matched on its own, and a slot never changes its key. A merge needs no free slot, so it
     * succeeds when the queue is full. Every other event takes a new slot; BUTTON, TOGGLE,
     * COMBO, LIST and PAGE events are never merged. */
    sco_result (*submit_event)(sco_view_client* client, const char* panel_id, const sco_view_event* event,
                               uint64_t* seq);

    /* Game thread only (SCO_WRONG_THREAD otherwise); not from inside a widget callback
     * (SCO_FAILED). Takes the slots that are queued when the call starts (at most
     * SCO_WIDGETS_MAX_EVENTS; later ones wait for the next call) and handles them in order.
     * Each slot is matched as described at sco_view_event: dropped when its panel is gone,
     * when no live node matches, or when the node is disabled or of another kind; otherwise it
     * is validated and delivered with the same node update, tag rule, callout guard and
     * reentrancy rules as RenderPanel. A callback that replaces or removes its panel drops the
     * remaining slots of that panel. *dispatched counts slots delivered to a callback or applied
     * to a display-only panel; *dropped counts the rest; either pointer may be NULL.
     * Merged-away events count in neither. events_done of stamps advances once per slot, to the
     * end of the slot's range, after the slot's effect is in the panel revision (or after it
     * was dropped). SCO_CRASHED: at least one owner faulted (counts are still valid; remaining
     * slots of that owner are dropped, other panels continue). */
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
| `sco_view_node` | 120 | flags 16, index 20, value 56, minimum 64, maximum 72, label 80, text 88, tooltip 96, tag 104, key 112 |
| `sco_view_event` | 56 | node_id 4, value 16, text 24, row 32, epoch 40, node_key 48 |
| `sco_widgets_view_v1` | 72 | version 4, capabilities 8, stamps 16, menu_thread_enter 24, menu_thread_leave 32, list_panels 40, copy_panel 48, submit_event 56, dispatch_events 64 |

The tag limit. The longest tag is `SCO_WIDGETS_MAX_TAG` (63 bytes, so it always fits the blob string table and the 64-byte id fields). `SCO_WIDGETS_CAP_MAX_TAG` (9) is an index into the `sco.widgets` capabilities array, not a length, and this header does not use it. Core's own `include/sco_widgets.h` has two misleading comments, and M1 fixes both in Core so the mistake is not repeated: at line 128, "SCO_WIDGETS_CAP_MAX_TAG is the host's tag byte limit", and on the define itself at line 176, `#define SCO_WIDGETS_CAP_MAX_TAG 9u /* longest accepted tag, in bytes */`. The real byte limit is `SCO_WIDGETS_MAX_TAG` at line 177. Both comments are changed to say the value is an index into the capabilities array and to point at `SCO_WIDGETS_MAX_TAG` for the length. Comments only, no value or layout change; M1 runs the existing ABI snapshot test to confirm. An earlier draft of this page used it as a byte limit in the event comment. The event no longer carries a tag at all (it carries the key), so the only tag limit left is the one on the blob node.

### 2.3 Counters, event matching and acknowledgement

All counters come from one monotonic 64-bit source, so a value is never reused, even for a panel removed and added again. Zero is never a valid revision. The single shared source is new Core work. Core today has only a per-panel `generation` that moves on every plugin mutation (`set_panel`, `update_nodes` and `remove_nodes` all call `Changed()`), so it cannot serve as `epoch`.

| Counter | Changes when | Used for |
|---|---|---|
| `panel.revision` | anything `copy_panel` would return changes: `set_panel`, `update_nodes`, `remove_nodes`, an event applied to a node (value, index, text, page row) | decide whether to re-copy one panel |
| `panel.epoch` | a new counter, bumped only by `set_panel` (replace, or remove and add). `update_nodes` and `remove_nodes` do not move it | tell dispatch that the panel was republished after the copy: the event is then matched by node key (2.3.1). With an equal epoch it is checked per node against the live tree, including the node key (2.3.1) |
| event sequence (`seq`, from `submit_event`) | each accepted event gets the next number, from 1, with no gaps | name one submitted event |
| `events_done` (from `stamps`) | a queue slot leaves the queue (delivered or dropped); set to the last sequence number the slot covers, after its effect is in the panel revision | the ack path: every event with `seq` <= `events_done` is finished (2.3.2) |
| `event_drops` (from `stamps`) | a slot is dropped by `dispatch_events` | make a lost click visible (3.4) |
| `list_revision` | the set of listed panels changes (add, remove, owner released) or a panel's title, owner or flags change | decide whether to re-list |
| `global_revision` | any of the above, for any panel | the per-frame poll: one value to compare |

A menu frame is: `stamps`; if `global_revision` moved then `list_panels` (when `list_revision` moved) and `copy_panel` for the open page only (when its revision moved); draw; submit events. `stamps` is read before `copy_panel`, so a copy taken after seeing `events_done >= seq` shows the effect of that event.

#### 2.3.1 Matching events across a republish

The problem. A click is copied on the menu thread, from a blob of epoch E, and is delivered on a later game-thread pass. Today ImGui copies and dispatches in one game-thread pass, so a plugin cannot change its panel in between. With the view table there is a gap of up to one game pump. A plugin that republishes a whole panel with `set_panel` on a timer or after each status change (source reading: `plugins/loadout/loadout.cpp` ends a `tick` subscriber in `Publish()`, which calls `set_panel`; 2.4) moves the panel to epoch E+1 inside that gap. Dropping the click would be a regression against ImGui, and the plugin cannot avoid it. So a republish must not lose a click on a control that is still there.

Identity. Core gives every node a 64-bit key (the header text of 2.2 defines it, with one function used for both the blob and the live tree):

| Node | Key covers | Survives a republish when |
|---|---|---|
| tagged (widgets 1.2) | kind and tag | the tag and kind are the same. The id, the label, the position and the parent may all change. The supported guarantee |
| untagged | kind, id, parent id, label | the same node is republished with the same id, parent and label. Not guaranteed if the plugin relabels the node or renumbers ids in the same republish |

Rule at dispatch (the full text is the comment at `sco_view_event`):

| Case | Outcome |
|---|---|
| epoch equal, the live node with id `node_id` is present, same kind, enabled with its ancestors, and its computed key = `node_key` (tagged and untagged alike) | delivered |
| epoch equal, any of those fails | dropped, counted |
| epoch different, a live tagged node has key = `node_key` | that node is the target |
| epoch different, no such tagged node, the live node with id `node_id` has computed key = `node_key` | that node is the target |
| epoch different, no target, or the target has another kind, or is disabled including its ancestors | dropped, counted |
| a target was found | delivered to it with the live node's id; value, selection and page row are validated against the live node as in `RenderPanel`; at most once, to at most one node |

Why a key and not "the same id". Ids are plugin-chosen and can be renumbered by a republish, and a different control can reuse an id. The key makes the click land on the control the player saw. A wrong delivery (Delete instead of Apply) is worse than a dropped click, which is why an untagged node whose label changed in the republish is dropped, counted, and shown by the "try again" line (3.4) and not delivered by id alone. The same rule holds at equal epoch. `update_nodes` can replace a subtree without moving the epoch, so an id can come to name a different untagged control (a Delete button under the id Apply had) with no `set_panel` at all. Checking id, kind and enabled state alone would deliver the click to it. So the key is compared at equal epoch too, for untagged nodes as well as tagged ones. The cost is stated: an untagged control that `update_nodes` relabels between copy and dispatch loses that click (counted, shown) where ImGui, which matches by id alone in one pass, would deliver it. This is an accepted divergence from `RenderPanel` and is owner decision 11 (section 7). The supported way to keep a control clickable across a relabel is a tag.

Examples, with an in-game meaning:

| What the plugin does between copy and dispatch | Tagged button | Untagged button |
|---|---|---|
| nothing, or `update_nodes` of other nodes | delivered | delivered |
| `update_nodes` relabels this button (same epoch) | delivered (the label is not in the key) | dropped, counted, shown (the key covers the label; same rule as a `set_panel` relabel) |
| `update_nodes` replaces the subtree so that a different button now has this id (same epoch) | dropped (no tag match) | dropped, counted (key differs; never delivered to the other button) |
| `set_panel` with the same tree and values changed elsewhere | delivered | delivered (same id, parent, label) |
| `set_panel` with ids renumbered | delivered, with the new id | dropped unless an id still carries the same key |
| `set_panel` with this button relabelled | delivered (label is not in the key) | dropped, counted, shown |
| `set_panel` with the button removed | dropped, counted | dropped, counted |
| `set_panel` with the button disabled | dropped, counted | dropped, counted |
| `set_panel` with a different control under the old id | dropped (no tag match) | dropped (key differs) |
| `update_nodes` changes other nodes, or this node's value, and leaves it enabled (same epoch) | delivered | delivered (label unchanged, so the key is equal) |

Plugin guidance (goes to `docs/plugin-ui.md`): give a tag to every control that must survive a republish, and republish only when something changed. M1 reports which in-tree panels republish on a tick and whether they are tagged; M6 tags the ones that are not (owner-run item 14 checks it in game).

How it is tested without the game (all in `tests/test_widgets_view.cpp`, deterministic because the test calls `dispatch_events` itself):

1. Table-driven: for each row of the example table above and each interactive kind (BUTTON, TOGGLE, SLIDER, COMBO, LIST, PAGE, TEXT_INPUT), submit with epoch E, republish, dispatch, and assert delivered or dropped, the id the callback saw, and `dispatched`, `event_drops` and `events_done`.
2. Exactly once: a callback counter shows a delivered event is seen once, and a slot that matched nothing is not delivered to a neighbour with the same id.
3. Stress: one thread republishes the same panel with `set_panel` in a loop (identical tree, tagged and untagged variants) while the test thread submits and dispatches 10 000 BUTTON events. Tagged: every event delivered, `event_drops` stays 0. Untagged with an identical republish: the same. Untagged with the label changed on every republish: every event dropped and counted, none delivered to a wrong node. Run under the repository's sanitizer or stress setup.
4. Equal-epoch relabel and id reuse by `update_nodes`: a relabel between copy and dispatch delivers the click for a tagged button and drops it (counted) for an untagged one; an `update_nodes` that replaces the subtree so that a different untagged button, and then a different tagged button, carries the old id drops the click and runs neither callback. Each case runs for every interactive kind and asserts the id the callback saw, or that no callback ran.
4a. Parity with `RenderPanel`, two deliberate divergences: (i) the equal-epoch key check of case 4, where `RenderPanel` would deliver by id alone; (ii) no generation break inside one drain (below), where `RenderPanel` breaks. A table-driven test runs the same copy, mutation and event sequence through `RenderPanel` and through `dispatch_events`, asserts the outcome is identical for every row that is not one of these two cases, and asserts that the two cases differ exactly as documented. `RenderPanel` keeps its break and its id-only match on purpose: its copy and dispatch happen in one pass, so neither hole exists for it, and changing it would alter ImGui behaviour for no gain.
5. Key vectors: the key function has golden vectors (tag, id, parent, label, kind; the empty label; a 63-byte tag; a 255-byte label) that the Rust test host reproduces, so the C++ and Rust sides cannot disagree. The menu never computes a key in product code.

Within one drain. `RenderPanel` today stops its pass when the panel's generation moves, which a callback's own `update_nodes`, `remove_nodes` or `set_panel` does. `dispatch_events` does not do that. It handles every slot taken at its start, and each slot is checked on its own against the live panel when its turn comes, by the table above: the epoch gate (a callback's `set_panel` moves the epoch, so the later slots of that panel are key-matched) and the per-node check, which at equal epoch includes the key (a callback's `update_nodes` or `remove_nodes` does not move the epoch, so the later slots are checked against the live tree: present, same kind, enabled, same key). One rule for all of these cases: a queued click is delivered if its control is still there, enabled and of the same kind, and dropped and counted if not. No callback, whatever it changes, causes a slot to be dropped only because it ran earlier in the same drain. Why: a button handler that calls `update_nodes` is the common case, and dropping the player's next queued clicks for it would be the click loss this section removes. Cost: a handler can now see a second click that ImGui would have discarded; the click is still one the player made on a control that was visible. Termination: the drain is bounded by the slots taken at its start, so a handler that republishes cannot make it loop. Case 4a above pins the difference against `RenderPanel`, which keeps its generation break deliberately and is not changed; the two entry points differ in this one stated way (owner decision 11). The tests below include this case.

#### 2.3.2 `events_done` under coalescing

Definitions.

- `seq`: assigned by `submit_event` when it accepts an event: 1, 2, 3 and so on for the client. A refused event takes no number, so there are no gaps.
- Queue: a list of slots, first in first out. A slot covers a range of sequence numbers `[first, last]`. The ranges of consecutive slots follow each other with no gap and no overlap.
- Merge (only at submit time): a SLIDER or TEXT_INPUT event merges into the newest slot if that slot is still queued and holds a SLIDER or TEXT_INPUT event for the same panel id, node id, kind, epoch and node key. A different key is a different control (2.3.1), so it never merges and takes its own slot; a slot never changes its key. The slot keeps its place, takes the newer value, and `last` becomes the new `seq`. Nothing else merges: a BUTTON, TOGGLE, COMBO, LIST or PAGE event always takes a new slot, and nothing merges across another event (the merge target is the tail only). A slot that `dispatch_events` has already taken is never merged into.
- `dispatch_events` takes the slots queued when it starts (at most 256) and handles them in order. For each slot it delivers or drops it, then publishes `events_done = slot.last`. `events_done` is therefore monotonic, can jump by more than 1, and is never a number inside a slot.
- `dispatched` and `event_drops` count slots, not events. A merged-away event counts in neither: it is superseded, not dropped.
- The guarantee: `events_done >= k` means event `k` is finished. If `k` was the last event of its slot, it was delivered (with its own value) or dropped. If `k` was merged away, its slot carries a newer value and `events_done >= k` only after that slot is finished, so "finished" never claims that an old value reached the plugin. A caller that waits on the newest `seq` it sent for a node is always waiting on a slot's `last`, because a newer event for the same node either merged (its `seq` is the new `last`) or took its own slot.

Examples.

| Step | Queue after the step (`[first,last]` kind node) | `events_done` after a full dispatch |
|---|---|---|
| `submit` BUTTON A returns 1 | `[1,1] BUTTON A` | |
| `submit` SLIDER S 0.3 returns 2 | `[1,1] BUTTON A`, `[2,2] SLIDER S` | |
| `submit` SLIDER S 0.5 returns 3 (tail is SLIDER S, same epoch) | `[1,1] BUTTON A`, `[2,3] SLIDER S 0.5` | |
| `dispatch_events` | empty. Two slots handled: `dispatched` is 2 | 3 (the poll sees 0, 1 or 3; it never sees 2) |

| Case | What happens |
|---|---|
| Not adjacent: SLIDER S 0.3 (1), BUTTON X (2), SLIDER S 0.5 (3) | three slots, no merge. S is set to 0.3, X runs, S is set to 0.5. `events_done` ends at 3 |
| Different epoch: SLIDER S (1, epoch E), then SLIDER S (2, epoch E+1) | two slots. They are matched separately (2.3.1) |
| Different key, same node id and epoch: untagged SLIDER S (1, key K1), `update_nodes` relabels it, the menu copies again, SLIDER S (2, key K2) | two slots, no merge. Slot 1 is matched with K1 and is dropped (the live key is now K2, 2.3.1); slot 2 is matched with K2 and delivered. `events_done` ends at 2. The same holds for a tagged node whose tag was replaced under the same id |
| Same key, same node id and epoch, tail slot: SLIDER S (1, key K1), SLIDER S (2, key K1) | one slot `[1,2]` that keeps K1 |
| Queue holds 256 slots and the tail is SLIDER S | a SLIDER S event merges and returns `SCO_OK` with the next `seq`; a BUTTON returns `SCO_TOO_MANY` and takes no `seq` |
| A merged slot `[2,3]` is dropped because the node was disabled | `event_drops` +1 (one slot), `events_done` goes to 3 anyway; the menu sees `events_done >= 3` and `event_drops` moved, copies the panel, and snaps the node to the plugin's value (3.4) |
| A callback at slot 1 replaces its own panel with `set_panel` (epoch E+1); slots 2 and 3 are BUTTON events for tagged nodes still in the new tree | slots 2 and 3 are key-matched (2.3.1) and delivered, `dispatched` +3, `event_drops` unchanged, `events_done` advances after each slot. If a node is gone from the new tree, its slot is dropped and `event_drops` +1 for it |
| A callback at slot 1 calls `update_nodes` or `remove_nodes` (epoch unchanged); slots 2 and 3 are for other nodes | slots 2 and 3 are checked against the live tree and delivered unless their node was removed, re-kinded, disabled or now has another key (an untagged node relabelled by that `update_nodes`); there is no generation break |
| Slots arrive during a drain | the drain handles the slots queued at its start; a later `submit_event` waits for the next call. A new SLIDER event after a slot was taken cannot merge into it; it takes a new slot |
| The client stops | `events_done` stops moving; `submit_event` answers `SCO_UNAVAILABLE`; the menu is stopping anyway |

How the menu uses it (3.4): per node it keeps the `seq` of the newest event it sent and its optimistic value. It treats the node as settled when `events_done >= seq`, forces one `copy_panel`, shows the copied value, and forgets the record. It never needs to know whether a merge happened.

Tests: `tests/test_widgets_view.cpp` runs every row of the two tables above (the sequence in the first table is checked step by step with the real `seq` and `events_done` values), plus: `seq` is gapless across refused events; `events_done` never decreases and never lands inside a slot; two SLIDER and two TEXT_INPUT events with the same node id and epoch but different `node_key` take two slots (no merge, each slot keeps its own key, each is matched with it, the first is dropped when the live key is the second), and with equal keys they merge; a test thread submits while a callback is running (the callback is gated) and the test checks that the new event waits for the next call; a slot that is taken is never merged into (a gate in the test callback holds the slot open); `dispatched + event_drops` equals the slot count; and a property test with a random mix of SLIDER, TEXT_INPUT and BUTTON events on two nodes checks the invariants (the nodes include two with the same id but different keys): ranges contiguous, a slot's events all carry its key, `events_done` equals the last `last` of the finished prefix, and the value the plugin sees for each node last is the value of the newest event for that node that was not dropped.

### 2.4 Core implementation notes (not part of the header)

- `Entry` gains `revision` and `epoch`, both from the new shared counter. Flattening reuses the shared immutable snapshot that `RenderPanel` already builds. `copy_panel` takes the widget lock, builds the snapshot if dirty (the same coalescing counter `SnapshotBuilds`), copies the `shared_ptr`, releases the lock, then flattens. It never calls plugin code and never holds the lock while writing the caller's buffer.
- Cost and contention, stated: a dirty rebuild is a full `Panel` copy under `g_lock` and now may run on the menu thread, so the game thread's `set_panel` and `update_nodes` can wait on it. The rebuild is bounded by the widget limits (4096 nodes), and happens at most once per change. Option kept open: build the snapshot on the game thread at the end of a mutation and let the menu thread only copy. M1 measures both and picks. `SnapshotBuilds` keeps its meaning in the tests (builds, not callers).
- Thread check, source reading (not yet a test result). `Panels()` and `OwnedBy()` return nothing off the game thread today, so `copy_panel` must not use them and reads the widget table directly under the widget lock. The two owner reads it needs look safe off the game thread in the sco-core source: `detail::Released(owner)` takes only the owner lock; `host::PluginId(owner)` takes a `RuntimeLease` and reads an id that does not change after the handle is claimed. The lock order widget lock then owner lock already exists in `RenderPanel`. One catch: `PluginId` returns null during startup admission and while the runtime stops. In that case `list_panels` skips the panel and `copy_panel` answers `SCO_UNAVAILABLE`; neither crashes. M1 task one is to confirm this reading and to write the race test in 5.1 (threads call the view while plugins load, fault and unload). The thread column of 2.2 stays "proposed" until that test passes, and the header is not called final before then.
- Id lengths. Panel id and title limits are 63 bytes. The plugin id is at most 31 bytes (`kMaxIdLen` in `include/sco/host.h` and `plugins.h`, source-checked), so the 64-byte owner field always fits. `list_panels` still skips a panel whose owner id would not fit, with one log line, and never truncates it. That branch is defensive and cannot be reached today.
- Password nodes. `copy_panel` omits the text of a node with `SCO_WIDGET_PASSWORD` (length 0, flag kept). The blob therefore never carries a secret, in any shape. The Rust field shows no dots for an existing value; it only accepts new input.
- Admission and shutdown order. Every view call takes the `RuntimeLease` like `RenderPanel`. Stop closes admission first. The order in `src/dllmain.cpp` is: Core closure runs on the game thread, `DrainRequestedStop(1000)` (the Core owner drain) runs on the control thread, and only then `Menu_Drain(1000)` joins the menu thread. So the Rust thread can still call the table after the client's stop has returned. This is why the table and the client handle are static for the process lifetime (header rule) and why a call after stop answers `SCO_UNAVAILABLE` and is expected. Nothing in the design waits on the join for memory safety. A stop-versus-copy race test with the join after stop is in 5.1.
- Menu thread registration. The Rust menu thread calls `menu_thread_enter` (2.2) as the first act of its entry function, and `menu_thread_leave` from a drop guard on every exit path, panic unwinding included. Core stores the caller's platform thread id in the client under the client's own small mutex (never the widget lock). `list_panels`, `copy_panel` and `submit_event` compare the caller's thread id with it (the same kind of check `OnGameThread()` makes for `dispatch_events`) and answer `SCO_WRONG_THREAD` otherwise; no registered thread means every caller is refused. `menu_thread_enter` from the game thread is refused and a second thread is refused with `SCO_FAILED`, so a stale registration cannot be taken over silently. If the menu thread dies without leaving (a fault, not a panic), `Menu_Drain` clears it with `HostWidgetsViewReleaseMenuThread` (2.5) after the join. In debug builds a mismatch also asserts. If `menu_thread_enter` fails, the Rust thread logs one line, marks the menu broken and draws nothing.
- Periodic `set_panel`, source reading in `plugins/` of this repository (not a test, not run in game): `set_panel` is called from a command, an event or a startup path in gear_presets, inspector, titanlink, voxel_bridge, build picker and travel. `plugins/loadout/loadout.cpp` registers a `tick` subscriber (`Tick`, near line 965) that ends in `Publish()`, which calls `set_panel` for `loadout.player` and `loadout.sq42`; M1 confirms which ticks reach it. So at least one shipped plugin can republish a whole panel on a tick, which is why the key rematch of 2.3.1 exists. M1 reports the exact count and re-counts after M6.
- Event drops: the queue keeps a running drop total, returned by `stamps`. `dispatch_events` logs at most one line per second.
- `RenderPanel`'s event half is split out as one function, `ApplyAndDispatch(entry, event)`, used by both `RenderPanel` and `dispatch_events`. Two code paths for validation would drift. Existing widget tests must stay green with the split. The node-level check (id, kind, enabled including ancestors) is the check `RenderPanel` already runs on the live node. Three things are new for `dispatch_events` and are stated in 2.3.1: the epoch gate with the key match, the key compare at equal epoch (so a reused id is never delivered to another control), and the removal of the generation break (`RenderPanel` keeps its id-only match and its break, so its behaviour does not change). The key compare is a parameter of the shared function, which `RenderPanel` passes as off. The split therefore takes the break as a parameter or leaves it in `RenderPanel`'s loop; the shared function never reads `generation`. The key comes from one function, `NodeKey(const Node&)`, used by the snapshot flattener and by dispatch.
- The queue is per client, a list of slots bounded at `SCO_WIDGETS_MAX_EVENTS` (256), guarded by its own small mutex, never held during a callback. `submit_event` assigns the sequence number, merges a SLIDER or TEXT_INPUT event into the tail slot when the rules of 2.3.2 allow it, and otherwise appends a slot. `dispatch_events` takes the queued slots under the mutex, releases it, handles each slot, and stores `events_done` (release) after each slot. BUTTON, TOGGLE, COMBO, LIST and PAGE are never merged or reordered.
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
// SCO_UNAVAILABLE (menu_thread_leave excepted: it still works so a thread can leave).
bool HostWidgetsView(const HostMenuContext& context, const sco_widgets_view_v1** table,
                     sco_view_client** client);

// Control thread, for the same reserved identity, and only after Menu_Drain has joined the menu
// thread: clears a menu-thread registration that the thread did not clear itself (it died on a
// fault). Returns false for any other caller, and does nothing while a thread is still running
// (a drain that timed out leaves the registration to that thread's own menu_thread_leave).
bool HostWidgetsViewReleaseMenuThread(const HostMenuContext& context);
```

The product's `Client::Load` calls `HostWidgetsView` and passes both pointers to the Rust module (section 3.2). A plugin has no way to name this identity. The menu thread registers itself: it is the Rust thread that calls `menu_thread_enter` on the table, so the host never has to know the Rust thread's id, and the earlier `HostWidgetsViewSetMenuThread(context, thread_id)` is dropped (a Rust thread could not call a C++ function that needs a `HostMenuContext`, and passing an id from another thread invites a mismatch). The only host-side call is the backstop above.

### 2.6 Versioning and safety

- The table only grows: new functions are appended, each append raises the minor version macro and `size` tells a consumer what exists. The blob header, node and entry structs only grow at the end. A reader that sees a bigger `size` ignores the tail. A change to an existing field is a new major.
- The menu checks table `version` and `size`, blob `version`, `magic`, `size >= 96/120/224`, `node_stride >= 120`, that `nodes_offset`, `items_offset` and `strings_offset` are multiples of 8 and their ranges do not overlap, and every offset and length before it reads a byte. A malformed blob is a refused page ("This page could not be read"), never a crash. Section 5 fuzzes this reader.
- Least privilege: the event path is the same path a renderer uses. A Rust menu cannot do more than an ImGui menu could: it cannot press a disabled node, set an out-of-range slider, or choose the callback's tag. The node key only picks which live node the player's click goes to, and the host re-checks kind, enabled state and values on that node.
- No new capability is switched on or off by this interface. Nothing here changes a default.

### 2.7 Catalogue, ABI test, packaging and provenance (M1 deliverables, draft text)

What the SDK catalogue sees. `tools/sdk_catalogue.py` reads top-level `include/*.h` only (2.1). The new header is under `include/sco/`, so it creates no record in `sdk/contracts`, no row in the `docs/contracts.md` Inventory, no service name, no status-registry row and no plugin-facing catalogue export. That is the intent, and not a loophole: the table is not a plugin contract. M1 proves it by running `python tools/sdk_catalogue.py check` before and after the change; the output must be the same. If the check ever starts to scan `include/sco/`, the header would need the records of a published table, and that is a sign it is in the wrong place.

The contract text for this seam is the header comment (normative) and one page, `docs/widgets-view.md`, which carries the per-method table below. The same page carries the one stability statement: the view table is a host-source seam, outside the Stable and Experimental classes of published service tables, pinned by size and layout tests, with no promise to plugins. `docs/contracts.md` is not touched, and neither is `docs/stability.md`: `tools/sdk_catalogue.py check` pins the rows of `docs/contracts.md`, and `sdk/components.json` lists both files, so an edit there is a packaging-visible change this seam does not need. `docs/widgets-view.md` is a new file with its own `components.json` entry (`core-source`). If the owner later ships it to plugins it needs a top-level header, a record per method, a service name and a class (Experimental to start).

| Method | Ownership | Threads | Results | Lifetime and cancellation |
|---|---|---|---|---|
| `capabilities(client, caps)` | caller's array; written, not kept | any | OK, BAD_ARG, UNAVAILABLE | none; O(1) |
| `stamps(client, g, l, drops, done)` | caller's pointers; written, not kept | any, O(1), no allocation | OK, UNAVAILABLE | none |
| `menu_thread_enter(client)` | none; Core records the calling thread | the future menu thread; not the game thread | OK, BAD_ARG, WRONG_THREAD, FAILED, UNAVAILABLE | held until `menu_thread_leave` or the host's release after a join |
| `menu_thread_leave(client)` | none | the registered thread | OK, BAD_ARG, WRONG_THREAD | works after stop |
| `list_panels(client, out, stride, count)` | caller's array; Core keeps nothing | registered menu thread in 1.0 (proposed: any non-game thread) | OK, BAD_ARG, TOO_MANY (count = needed), UNAVAILABLE, WRONG_THREAD | a snapshot of the list at one revision |
| `copy_panel(client, id, out, bytes)` | caller's 8-aligned buffer; Core keeps nothing | registered menu thread in 1.0 (proposed: any non-game thread); runs no plugin code | OK, TOO_MANY (bytes = needed), NOT_FOUND, CRASHED, BAD_ARG, UNAVAILABLE, WRONG_THREAD | one consistent snapshot of one revision |
| `submit_event(client, id, event, seq)` | event and text copied during the call; `*seq` written on OK | registered menu thread in 1.0 (proposed: any non-game thread) | OK, NOT_FOUND, BAD_ARG, TOO_MANY, UNAVAILABLE, WRONG_THREAD | queued, possibly merged (2.3.2); finished when `events_done` >= `*seq`; may be dropped later and counted in `event_drops` |
| `dispatch_events(client, ok, dropped)` | none | game thread only; not inside a widget callback | OK, WRONG_THREAD, FAILED, CRASHED | drains the slots queued at its start; a callback that faults stops its owner's slots only |

Where the files go, and how they are packaged. `sdk/package.py` builds every zip from `sdk/components.json` alone, and the repository check requires that every file under the folders in `covers` is listed there or in `excluded`. So M1 adds one entry per new file, each with `profiles: ["core-source"]` (the same profile as `include/sco/host_menu.h`; `host-source-sc` includes it), `origin: "project-authored"`, `license: "GPL-3.0-only"` and no `public_header` key:

| New file | Role |
|---|---|
| `include/sco/widgets_view.h` | the header (2.2) |
| `src/widgets/view.cpp` (and a private header if needed) | the implementation; the `RenderPanel` event split (2.4) |
| `tests/abi_widgets_view.c` | layout pins, built as C11, as C++20 and with short enums, the same three ways as `tests/abi_widgets.c`, and run with the `abi_v1` target list |
| `tests/test_widgets_view.cpp` | the section 5.1 tests |
| `tests/fixtures/widgets_view/` | shared blobs and `.json` descriptions, plus the key vectors |

Packaging checks added to `sdk/test_package.py`: the host-source kit (`host-source-sc`) and the kernel source kit (`core-source`) contain the header and its tests; the `core`, `game-sc` and `legacy` zips do not contain `include/sco/widgets_view.h`; the plugin zips' header list is unchanged. A plugin author therefore cannot see the table, and the SDK catalogue and plugin docs are unchanged. The sc-offline `external/sco-core` pin (and the host-source kit digest it records) moves when the commit that adds the header is pinned. That is the normal bump for any Core change in 2.4.1.

ABI test, and pin. `tests/abi_widgets_view.c` asserts, in C11 and C++20, every size and key offset of the pins table of 2.2, the macro values, that `SCO_VIEW_ID_BYTES` is larger than `SCO_WIDGETS_MAX_TAG`, and that all three blob offsets are multiples of 8. It is a new CMake object library set beside `sco_abi_widgets_*`. Core's released-header freeze (`tools/abi_snapshot.py`, `tests/abi/released/sco_api.h`) is built around `sco_api.h` and is not extended: this header has no release yet, and stretching a plugin-ABI freeze to a host-source seam would blur what the freeze promises. Instead, until the first release that contains the header, the only pin on the Core side is `tests/abi_widgets_view.c`; at the first release candidate its golden (a probe's printed sizes and offsets) is checked in under `tests/abi/` and compared by a test. That is a stated gap, and owner decision 9 (section 7) accepts it or asks for the golden earlier.

The Rust side. The menu does not include the header. `scom-view` has hand-written `#[repr(C)]` mirrors (`ffi.rs`) with `const` asserts for every size and offset in the pins table, and a list of those numbers in `menu-rs/crates/scom-view/pins.txt`. sc-offline gets a check, `tools/view-abi-check.py`, that compiles a small C probe including `sco/widgets_view.h` from the pinned Core (`SCO_HOST_SOURCE_DIR` or `external/sco-core`) with the compiler the repository already uses, prints `sizeof`, `offsetof` and the macros, and compares them with `pins.txt`. It also records the SHA-256 of the pinned header in `menu-rs/ABI_PIN`. A Core header edit therefore fails the sc-offline CI until a person has reviewed the Rust mirrors and re-recorded the hash. The C++ host glue in `src/` includes the header directly (it is a host, not a plugin or a builtin, so `tools/sdk-headers.py` does not apply), and passes the table and client pointers to `scom_open` (3.2).

| Field | Value |
|---|---|
| Name | `sco.widgets.view` (a label for docs and logs; there is no service name) |
| Header | `include/sco/widgets_view.h`; `core-source` profile, no `public_header`; not in the SDK catalogue |
| Game neutrality | no game type, no game name, no change to `sco_api.h` or `sco_widgets.h` |
| Tests | `tests/abi_widgets_view.c`, `tests/test_widgets_view.cpp`, shared fixtures, `tools/view-abi-check.py` (sc-offline) |
| Status | specified and tested synthetically once M1 lands; **not run in game** |

Provenance. The header and its tests are independently authored from the public `sco.widgets` contract and the Core sources already in the repository. Each new file carries the SPDX line `GPL-3.0-only` and the per-file provenance metadata the repository requires (`origin` and `license` in `sdk/components.json`, and whatever `IP_POLICY.md` asks for at review time). No private ledger id, private path, extracted game data, binary or dump is used or cited. Fixtures are synthetic panels only. The key function and its vectors are invented for this interface. Provenance review stays a human step.

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
- Menu thread registration (finding: the Rust thread cannot call a C++ `HostMenuContext` function). The first statement of the menu thread's entry function, before any window, D3D or egui work, is `view->menu_thread_enter(client)` on the table the host passed in `scom_host_v1`. A drop guard calls `menu_thread_leave` on every exit path, panic unwinding included, then `ClipCursor(NULL)` and hide. If enter fails (`SCO_FAILED`, `SCO_WRONG_THREAD`, `SCO_UNAVAILABLE`), the thread logs one line through `log`, sets the broken flag, and never calls `list_panels`, `copy_panel` or `submit_event`. The host does not know or pass the thread id. After `Menu_Drain` has joined the thread, the control thread calls `HostWidgetsViewReleaseMenuThread` (2.5) as a backstop for a thread that died without leaving; if the join timed out it does not call it.

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
- Optimistic local state, with an ack. While the player drags a slider, types, or flips a toggle, egui holds the value locally. The menu records, per node (keyed by tag when there is one, else by id), the value it sent and the `seq` that `submit_event` returned. A host revision never replaces that node's local value while the pointer is on it, nor while `events_done` < that `seq` (the event is still queued, so the copied value is old). When `events_done` reaches the `seq`, the menu marks the node settled, forces one `copy_panel` of the panel (the event may have been dropped, so the revision need not have moved), shows the copied value, and forgets the record. If the host dropped the event (`event_drops` moved) the copied value is simply the plugin's value and the node snaps to it, once, with the "try again" line. A newer event for the same node replaces the record and its `seq`. On a new epoch the local state is kept for nodes that still exist, matched by node key (a tagged node by its tag and kind; an untagged node only while its id, parent and label are unchanged, so a relabelled untagged node is treated as new), and dropped for the rest. Test: the fake host delays dispatch by 300 ms, a plugin revision bump arrives at 100 ms, and the slider value shown never goes back to the old value; see 5.2.
- Event pacing: at most one event per node per frame (last value wins), on top of the host's own coalescing. When `submit_event` answers `SCO_TOO_MANY`, the menu keeps BUTTON/TOGGLE/COMBO/LIST/PAGE events and retries them on the next frames (they are never discarded); only the last SLIDER and TEXT_INPUT value per node is kept. Drops after queueing are counted by the host (`event_drops` in `stamps`). When that total moves, the menu shows a transient line in the status strip, "A panel changed; try again", for three seconds, so a lost click is never silent. With the key rematch (2.3.1) this line appears only for a node that a republish removed, re-kinded or disabled, or, if it is untagged, relabelled or re-parented.
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

The switches live in the launcher. The in-game menu only shows the state the game started with.

Owner defaults (2026-11-17), as the menu must show them in a release package: the five capability switches `exp_loadout_async`, `exp_inventory_read`, `exp_actor_stats_write`, `exp_cvars_native` and `exp_audio_native` are **on** by default, each a visible key the player can turn off, each saying it has not been run in game. `exp_entities_diag` is **off** by default: it enables diagnostic hooks that patch game functions, and it is a diagnostic, not a feature.

- The Settings page has a "Game capabilities" section with one row per feature switch the host lists through `scom_host_v1.switches`: the switch name, `On` or `Off`, and the host's text. Below it, a separate "Diagnostics" group holds `exp_entities_diag` the same way. The list comes from the same registry the launcher reads (sco-core `sdk/status_registry.json` and the host's own table), so the menu hard-codes no switch name and a switch added in Core appears without a menu change. Which group a switch is in is a field the host sends (`feature` or `diagnostic`), not a name the menu knows.
- A feature row ends with the words "Not run in game." until the owner records a session for that capability. The menu does not show "verified" for any row.
- A feature switch that is Off shows one line: "Turn it on in the launcher, then start the game again." The menu has no toggle. Core reads the switches when the game starts, so a toggle in the menu would be a button that does nothing.
- A diagnostic row says what it does in plain words ("Patches game functions to collect diagnostic data. For problem reports only."). When it is Off it says nothing more; when it is On the row carries a visible "On: diagnostic hooks active" line, so a player who forgot it was on can see it.
- The section never calls a feature switch experimental or default-off, and never calls the diagnostic a feature. It states what is true now: on or off, and not run in game.
- Test: a `scom-testhost` list of four rows (feature On, feature Off, a long text, one diagnostic Off) and a variant with the diagnostic On draws as goldens at 100 and 125 percent. The text of each row is cut on a UTF-8 boundary with a tooltip, like any label.

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

- `tests/abi_widgets_view.c`: compile-only layout pins from section 2.2 in C11 and C++20 (and with short enums), including the 120-byte node with `key` at 112, the event with `node_key` at 48, the 72-byte table with `menu_thread_enter` and `menu_thread_leave`, the new `stamps` signature, the table `version` field, the 8-byte multiples of the three blob offsets, and `SCO_VIEW_ID_BYTES > SCO_WIDGETS_MAX_TAG`.
- `tests/test_widgets_view.cpp`: with a real Core and synthetic plugin panels: list order and counts; `copy_panel` blob for every node kind and every 1.1 to 1.3 extension; paging; tags; password flag; disabled ancestors; limits (256 nodes, 255-byte strings, 256 items); size handshake including a panel that grows between calls; revision rules (event moves `revision` not `epoch`, `set_panel` moves both, removal and re-add never reuses a value); released owner gives `SCO_CRASHED` and drops out of the list; `query_service` for the name answers `SCO_NOT_FOUND`; after stop every call answers `SCO_UNAVAILABLE`.
- Menu thread registration: `menu_thread_enter` from the game thread gives `SCO_WRONG_THREAD`; from a test thread it gives `SCO_OK` (and again from the same thread); a second thread gets `SCO_FAILED`; `list_panels`, `copy_panel` and `submit_event` give `SCO_WRONG_THREAD` before any enter, from any other thread, and after `menu_thread_leave`; `menu_thread_leave` from another thread gives `SCO_WRONG_THREAD` and changes nothing; leave works after stop; enter after stop gives `SCO_UNAVAILABLE`; `HostWidgetsViewReleaseMenuThread` after a join clears a registration the thread left behind and does nothing while the thread still runs. A plugin cannot obtain the table (`query_service` gives `SCO_NOT_FOUND`).
- Event path: submit from the registered menu thread, dispatch on the game thread; ordering; the republish, key-matching and stress tests of 2.3.1 (tagged and untagged, every interactive kind, exactly once, label changed in the republish, equal-epoch relabel by `update_nodes`, the key vectors); the `seq`, merge and `events_done` tests of 2.3.2, including the step-by-step sequence of its first table; `events_done` reaches `seq` after the effect is in the revision (a copy taken after seeing `events_done >= seq` shows the new value); a replaced untagged panel whose node kept id, parent and label delivers, one whose label changed drops and moves `event_drops`; an `update_nodes` or `remove_nodes` of other nodes between submit and dispatch does NOT drop a BUTTON, TOGGLE, COMBO or LIST event (stress test: one thread calls `update_nodes` in a loop while events are queued and dispatched; every event for an untouched node is delivered); an event for a removed, re-kinded or disabled node is dropped and counted; disabled node, wrong kind, out-of-range slider, over-long or invalid UTF-8 text are refused exactly as `RenderPanel` refuses them (the same table of cases is run against both entry points and must give identical outcomes); queue full gives `SCO_TOO_MANY` except for a merge, which succeeds; coalescing never merges BUTTON/TOGGLE/COMBO/LIST/PAGE, and never merges SLIDER or TEXT_INPUT events whose `node_key` differs; a callback that replaces its panel with `set_panel` with slots 2 and 3 queued for that panel delivers them by key match (tagged) and drops only those whose control is gone; a callback that calls `update_nodes` (relabel, disable another node) or `remove_nodes` with further slots queued delivers the slots whose nodes are still enabled and still have the same key (an untagged node that callback relabelled is dropped) and drops the others, with no generation break, and the drain still ends after the slots taken at its start; a callback that faults gives `SCO_CRASHED` and other panels still get theirs; `dispatch_events` off the game thread gives `SCO_WRONG_THREAD`. The proposed any-thread rule (2.4) is a separate test, run only after M1 task one and only if the race test passes.
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
- Event tests with `scom-testhost`: render a synthetic panel to egui with scripted input (no window: egui `RawInput` fed directly), assert the exact `sco_view_event` that comes out for each widget kind, one event per node per frame, no event for disabled nodes, paging events carry `row`, epoch is the epoch of the copy drawn, the event's `node_key` is the key of the blob node (copied, never computed; the test host builds blobs with the shared key vectors of 2.3.1). The menu thread's entry registers with `menu_thread_enter` before its first `copy_panel`, a failed enter leaves the menu broken with no calls to the table, and the leave guard runs on a panic. Optimistic state: with a fake host that delays the ack by 300 ms and bumps the revision at 100 ms, the value egui shows for a dragged slider and a toggled toggle never returns to the old value, and equals the host's value one frame after `events_done` reaches the `seq`; with a host that drops the event, the node snaps to the host value once and the "try again" line shows.

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
| M1. Core interface, 2 to 3 weeks | `include/sco/widgets_view.h`, the implementation (`src/widgets/view.cpp`), the `RenderPanel` event split, the node key and the epoch/key matching, the slot queue with coalescing, menu-thread registration, `HostWidgetsView` and `HostWidgetsViewReleaseMenuThread`, `tests/abi_widgets_view.c`, `test_widgets_view.cpp`, fixtures and key vectors, `docs/widgets-view.md` (with the stability statement; `docs/contracts.md` and `docs/stability.md` unchanged), the two comment fixes in `include/sco_widgets.h`, `sdk/components.json` entries (`core-source`, no `public_header`) and the package checks of 2.7, a `tools/sdk_catalogue.py check` run before and after with identical output, the count and tagged state of in-tree `set_panel` calls made on a tick (2.4), and the sc-offline `tools/view-abi-check.py` with `pins.txt` and `ABI_PIN`. Held for 2.4.1 with the other items. The header text of 2.2 was compiled once as C11 and C++20 with clang against the repository's `sco_widgets.h`, with static asserts for every size and key offset in the pins table. That check is repeated by `tests/abi_widgets_view.c` in M1. Never run in game | All section 5.1 tests pass (registration, republish and key matching, `events_done` under coalescing, stale-event stress, ack-under-delay and stop-versus-copy); existing widget tests unchanged and green; `query_service` refusal test; package check: the header is in the host-source and kernel source kits and not in the `core`, `game-sc` or `legacy` zips; `tools/sdk_catalogue.py check` output unchanged; no change to `sco_api.h` or `sco_widgets.h` |
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
6. The five capability switches on the launcher's Capabilities page (on by default, each can be turned off, text says not run in game) match the "Game capabilities" rows on the menu's Settings page after a game start: switch one off in the launcher, start the game, and the row says Off.
7. Performance: game frame time with the menu hidden, open and idle, open and dragging a slider; a 100-node panel; a 20-minute soak with the menu open and closed repeatedly. Record numbers, not impressions.
8. Fault behaviour: with a fault-injection dev build of `scom.dll`, the game keeps running, the menu is gone, `mod.log` says why. With `scom.dll` deleted, the ImGui menu comes back by itself with one log line.
9. A `sco.ui` plugin (`ui_probe`): its page appears under More plugins, opens the ImGui window, and Back returns to the Rust menu. Overlays show only in the legacy window.
10. Game exit and `Menu_Drain`: the game closes cleanly with the menu open and closed; no hang, no crash dialog, no stuck topmost window.
11. Wine or Proton, best effort (launcher decision 17): `M` opens the menu, the window can be clicked and typed into, hide returns to the game. A failure here is recorded, not a release gate.
12. Anti-cheat and hosts: the menu adds no new file, process, network call or PC change. The existing PC changes (firewall block, EAC hosts line, EAC exe rename) are unchanged by this work and still need the owner security review before cutover (launcher decision 8).
13. Overlays and cursor: with each overlay the owner normally runs (graphics-card, chat, capture or monitoring overlays), the menu opens, draws, takes the click and closes without a crash, flicker or a lost game frame; and the menu's software cursor is visible and correct when the game has hidden the OS cursor. Record which overlays were present.
14. Clicks across a republish: on a page whose plugin republishes its panel on a timer (M1 lists them; the loadout pages are the known case), press the same button 20 times at normal pace and then once a second for a minute. Each press acts exactly once. On a tagged page none may be lost; on an untagged page write down any lost press and whether the "A panel changed; try again" line showed. This is the only in-game check of 2.3.1.
15. `exp_entities_diag`: a fresh release package starts with it Off and the menu lists it as a diagnostic that is Off. The five feature switches show On. The menu has no control that changes any of them.

## 7. Open decisions for the owner

1. Shape A (in-process `scom.dll`, recommended) or shape B (separate process). Section 3.1. M0 gives the numbers; the owner decides after it.
2. The new interface ships as a host-source seam (`include/sco/widgets_view.h`, `core-source` profile, outside the SDK catalogue; recommended) or also in the plugin SDK kit. Shipping it to plugins would need a top-level header, a catalogue record per method, a service name and a stability class, and it makes the table a public promise and a privacy exposure (section 2.1).
3. Software cursor drawn by the menu (recommended, parity) or the OS cursor.
4. Release font: needs launcher decision 10 (an owner-approved Cyrillic OFL or MIT font). Goldens do not wait for it: they use egui's bundled OFL font behind the test-only `test-font` feature (5.3). The menu uses the machine fonts first until the release font is approved.
5. `scom-theme` reads the scuba-preview-theme JSON from scuba-engine, or sc-offline owns the file and scuba-engine copies it. Recommended: sc-offline owns it (the product is the source of the look), and a check keeps the engine copy equal.
6. Default backend flips to `rust` only after M8, or stays opt-in for one more release. Recommended: flip only after the owner records M8.
7. Whether M6 (porting in-tree `sco.ui` users) is a gate for 2.4.1 or can follow. Decision 2 says nothing is after 2.4.1, so the plan treats M6 as inside the hold. The owner may relax it for the plugins that are not shipped in the player package.

8. Password fields. The blob omits the text of a password node (2.4), so the Rust field shows no dots for an existing value and only accepts new input. Today's ImGui menu shows dots. This is a small parity loss that keeps secrets out of the blob; the owner accepts it or asks for a length-only field (a count of dots, no text).
9. Golden pin gap. Until the first release that contains `include/sco/widgets_view.h`, the Core-side pin is `tests/abi_widgets_view.c` only, and the sc-offline `view-abi-check` pins the header hash (2.7). The owner accepts the gap, or asks for the first golden to be recorded at the first release candidate.
10. Identity of untagged controls across a republish (2.3.1). Recommended: the key of an untagged node covers kind, id, parent and label, so a relabelled untagged node loses the click (counted, shown) and is never mis-delivered. The owner accepts this, or asks M6 to tag every in-tree panel that republishes so the question does not arise for shipped plugins. Third-party panels keep the rule. Source reading, not a test: `plugins/loadout/loadout.cpp` builds `loadout.player` and `loadout.sq42` from plain `sco_widget_node` entries (no tag field) and republishes them from a `tick` subscriber, so those two panels are untagged republishers today. Their ids are fixed numbers, so a click survives unless a button's label changes in the republish. Until the owner answers, the click-loss fix is guaranteed only for tagged controls; for the loadout pages it holds while labels are stable. M1 counts the rest.
11. Two deliberate differences from ImGui's `RenderPanel` (2.3.1, case 4a). First, `dispatch_events` compares the node key at equal epoch, so an untagged control relabelled by `update_nodes` loses a click that ImGui would deliver; the alternative, delivering by id alone, can press the wrong control after an id is reused. Second, `dispatch_events` keeps no generation break inside one drain, so a handler can see a second queued click that ImGui would have discarded. `RenderPanel` is left as it is. The owner accepts both, or asks for `RenderPanel` to change to match (which alters ImGui behaviour).

Not decided here and not changed by this page: the capability switch defaults (decision 4: the five feature switches on by default in every release package, each a visible key the player can turn off, each saying it has not been run in game; `exp_entities_diag` off by default), the catalogue (decision 3), and the PC changes.
