# UI test material

`screens/` holds one PNG per page of the `scol-ui` window, saved by
`scol-ui --ui-screenshots tests/ui/screens` (build with `--features fixtures` so the Catalogue tab has
entries). Synthetic fixture install; the Home page shows the status of the PC it was taken on.
The checks themselves are in `crates/scol-ui/` (`cargo test -p scol-ui` and `scol-ui --ui-self-test`).
Not run in game.
