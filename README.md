# Wim

A very basic keyboard-driven browser built on
[eacp](https://github.com/jamierpond/eacp) (fetched via CPM). One window, an
address bar, a WKWebView, and vimium-style keybindings injected into every
page.

## Build

```bash
just build   # configure (if needed) + build
just run     # build + launch
just clean   # delete the build directory
```

Recipes are thin wrappers over Node scripts in `Scripts/` — the dispatchers
(`compile.mjs`, `run.mjs`) pick the platform variant
(`Scripts/<cmd>-<macos|windows>.mjs`) from `process.platform`. Without `just`:

```bash
node Scripts/compile.mjs && node Scripts/run.mjs
```

Or plain CMake:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
open build/Wim.app
```

To develop against a local eacp checkout:

```bash
cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug \
      -DCPM_eacp_SOURCE=$HOME/projects/eacp
```

## Keybindings

Active whenever the page has focus and no text field is focused.

| Key  | Action                          |
| ---- | ------------------------------- |
| `j` / `k` | Scroll down / up           |
| `h` / `l` | Scroll left / right        |
| `d` / `u` | Half page down / up        |
| `gg` / `G` | Top / bottom of page      |
| `f`  | Link hints — type the label to follow a link |
| `H` / `L` | History back / forward     |
| `r`  | Reload                          |
| `o`  | Focus the address bar           |
| `t`  | Omnibar: fuzzy-find a tab, or search / open a URL in a new tab |
| `x`  | Close the current tab           |
| `J` / `K` | Previous / next tab        |
| `Esc` | Blur text field / cancel hints; in the address bar, return focus to the page |

## Tabs and the omnibar

`t` opens an fzf-style omnibar over the page (`Source/TabSwitcher.h`). Typing
fuzzy-filters the open tabs by title + URL (`Source/FuzzyMatch.h`); arrows or
`ctrl-j`/`ctrl-k` move the selection and Enter switches to it. When nothing
matches, Enter opens a **new tab** with a Google search for the query — or the
URL itself if it looks like one. `Esc` dismisses.

Pages that open popups (`target=_blank`, `window.open`) get their popup
adopted as a new tab. Each tab is a `WebView` owned by `BrowserView`
(`Source/Main.cpp`); only the active tab's view is in the hierarchy.

Add bindings in the `bindings` map inside `vimiumScript` in
`Source/Main.cpp` — keys are keypress sequences (`gg` works), values are the
action. `post('...')` sends a command string to the native side
(`BrowserView::handleCommand`) for actions the page can't do itself.
