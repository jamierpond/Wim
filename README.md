# Wim

A keyboard-driven browser built on [eacp](https://github.com/jamierpond/eacp)
(fetched via CPM). Native shell (window, address bar, per-page vimium
bindings) with the browser chrome rendered in web tech: the **Go palette** is
a vite + React app in `web/`, talking to C++ over eacp's Miro bridge.

## Build

```bash
just build   # configure (if needed) + build (schema codegen → vite → C++)
just run     # build + launch
just clean   # delete the build directory
```

Recipes are thin wrappers over Node scripts in `Scripts/` — the dispatchers
(`compile.mjs`, `run.mjs`) pick the platform variant
(`Scripts/<cmd>-<macos|windows>.mjs`) from `process.platform`.

UI dev loop (hot reload): configure with `-DEACP_WEBVIEW_DEV=ON`, run
`just web-dev` (vite dev server), then run the app — it prefers the dev
server over the embedded bundle.

To develop against a local eacp checkout:
`cmake -B build -DCPM_eacp_SOURCE=$HOME/projects/eacp`.

## The Go palette

`t` opens a spotlight for *your places on the web* — tab open or not, you
just get there:

- **Places** are your saved destinations (GitHub, Linear, mail, …), stored as
  editable JSON in `~/.wim/places.json`. `b` bookmarks the current page; the
  ★ in the palette toggles too.
- The list merges open tabs (green dot) with places, fuzzy-filtered as you
  type. All matching/ranking/selection logic is native C++
  (`Source/FuzzyMatch.h`, `BrowserView` in `Source/Main.cpp`); the web app
  only renders results and forwards input.
- **Live preview**: moving the selection changes the page *underneath* the
  palette immediately — Enter just dismisses, so you can already see you're
  in the right place. Places that aren't open load into a reused preview
  tab; Escape reverts to where you were.
- No match? Enter opens a Google search (or the URL, if it looks like one).
- ✕ closes an open tab from the palette.

The wire surface lives in `Source/Types.h` (`Api::WimApi`): commands
(`setQuery`, `moveSelection`, `activate`, `choose`, `cancel`, `closeItem`,
`toggleBookmark`) plus a `results` event carrying the render model. CMake
(`eacp_add_webview_app`) generates the typed TS client into
`web/src/generated/`.

## Keybindings (in the page)

| Key  | Action                          |
| ---- | ------------------------------- |
| `j` / `k` | Scroll down / up           |
| `h` / `l` | Scroll left / right        |
| `d` / `u` | Half page down / up        |
| `gg` / `G` | Top / bottom of page      |
| `f`  | Link hints — type the label to follow a link |
| `H` / `L` | History back / forward     |
| `r`  | Reload                          |
| `t` / `⌘L` | Open the Go palette       |
| `b`  | Bookmark current page as a place |
| `o`  | Focus the address bar           |
| `x`  | Close the current tab           |
| `J` / `K` | Previous / next tab        |
| `Esc` | Blur text field / cancel hints; in the address bar, return focus to the page |

In the palette: type to filter, `↑`/`↓` or `ctrl-j`/`ctrl-k`/`ctrl-n`/`ctrl-p`
to move (the page follows live), Enter to keep, Esc to go back.

Add page bindings in the `bindings` map inside `vimiumScript`
(`Source/Main.cpp`); `post('...')` sends a command string to
`BrowserView::handleCommand`.

Pages that open popups (`target=_blank`, `window.open`) get their popup
adopted as a tab.
