# UI test material

`screens/` holds PNGs of the `scol-ui` window, saved by
`scol-ui --ui-screenshots tests/ui/screens` (build with `--features fixtures` so the Catalogue tab has
entries). One shot per page, plus the scrolled lower parts of Capabilities and Settings (the groups
"Works by itself" and "Not available yet" open) and Russian shots of Plugins and Capabilities.
Synthetic fixture install; the Home page shows the status of the PC it was taken on.
The checks themselves are in `crates/scol-ui/` (`cargo test -p scol-ui` and `scol-ui --ui-self-test`).
None of this has been run in game; see [docs/status.md](../../../docs/status.md).
