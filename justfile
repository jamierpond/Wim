# Wim dev command runner. Every recipe is a thin one-liner delegating to a
# Node script in Scripts/ — the dispatchers (compile.mjs, run.mjs) pick the
# platform variant (Scripts/<cmd>-<macos|windows>.mjs) from process.platform,
# so the same `just` commands work everywhere.

set windows-shell := ["cmd.exe", "/c"]

# Show the recipe list by default.
default:
    @just --list

[doc('Configure (if needed) + build Wim for the current platform')]
build:
    node Scripts/compile.mjs

[doc('Build + launch Wim')]
run:
    node Scripts/run.mjs

[doc('Delete the build directory')]
clean:
    node Scripts/clean.mjs

[doc('Run the palette UI vite dev server (configure with -DEACP_WEBVIEW_DEV=ON)')]
web-dev:
    node Scripts/web-dev.mjs
