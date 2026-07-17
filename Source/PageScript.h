#pragma once

// Vimium-style bindings, injected into every page at document start -- which
// includes the native error page, so a tab whose load failed stays fully
// drivable. Add new bindings to the `bindings` map below -- keys are keypress
// sequences ('gg' works, as long as no binding is a strict prefix of
// another), values run when the sequence completes. `post(command)` sends a
// command string to the native side, handled in BrowserView::handleCommand.
static constexpr auto vimiumScript = R"JS(
(() => {
    if (window.__wimInstalled)
        return;
    window.__wimInstalled = true;

    const post = command =>
        window.webkit?.messageHandlers?.wim?.postMessage(command);

    const step = 60;
    const half = () => window.innerHeight / 2;

    const bindings = {
        'j': () => scrollBy(0, step),
        'k': () => scrollBy(0, -step),
        'h': () => scrollBy(-step, 0),
        'l': () => scrollBy(step, 0),
        'd': () => scrollBy(0, half()),
        'u': () => scrollBy(0, -half()),
        'gg': () => scrollTo(0, 0),
        'G': () => scrollTo(0, document.documentElement.scrollHeight),
        'H': () => history.back(),
        'L': () => history.forward(),
        'r': () => location.reload(),
        'o': () => post('openPalette'),
        't': () => post('openPalette'),
        'b': () => post('toggleBookmark'),
        'x': () => post('closeTab'),
        'J': () => post('prevTab'),
        'K': () => post('nextTab'),
        'f': () => enterHintMode(),
        '/': () => enterFindMode(),
        'n': () => findNext(false),
        'N': () => findNext(true),
    };

    // --- Find in page ('/') ------------------------------------------------

    let lastFind = '';

    function findNext(backwards) {
        if (lastFind)
            window.find(lastFind, false, backwards, true, false, false, false);
    }

    function enterFindMode() {
        const box = document.createElement('input');
        box.placeholder = 'Find in page…';
        box.style.cssText =
            'position:fixed;top:10px;right:10px;z-index:2147483647;'
            + 'width:200px;padding:6px 10px;'
            + 'font:13px -apple-system,sans-serif;'
            + 'color:#111;background:#fff;border:1px solid #999;'
            + 'border-radius:6px;box-shadow:0 2px 8px rgba(0,0,0,.25);'
            + 'outline:none;';

        // Enter searches (n/N continue), Escape cancels. The global handler
        // ignores keys while an input has focus, and its Escape-blurs-inputs
        // rule triggers the blur cleanup below.
        box.addEventListener('keydown', event => {
            event.stopPropagation();

            if (event.key === 'Enter') {
                lastFind = box.value;
                box.remove();
                findNext(false);
            } else if (event.key === 'Escape') {
                box.remove();
            }
        });

        box.addEventListener('blur', () => box.remove());
        document.documentElement.appendChild(box);
        box.focus();
    }

    // --- Link hints ('f') ------------------------------------------------

    const hintChars = 'asdfghjkl';
    let hintState = null;

    function hintLabels(count) {
        let length = 1;
        while (Math.pow(hintChars.length, length) < count)
            length++;

        return Array.from({length: count}, (_, i) => {
            let n = i, label = '';
            for (let d = 0; d < length; d++) {
                label = hintChars[n % hintChars.length] + label;
                n = Math.floor(n / hintChars.length);
            }
            return label;
        });
    }

    function clickables() {
        const selector = 'a[href], button, input, select, textarea, summary, '
            + '[onclick], [role="button"], [role="link"], [tabindex]';
        return [...document.querySelectorAll(selector)].filter(el => {
            const r = el.getBoundingClientRect();
            return r.width > 0 && r.height > 0
                && r.bottom > 0 && r.top < innerHeight
                && r.right > 0 && r.left < innerWidth
                && getComputedStyle(el).visibility !== 'hidden';
        });
    }

    function enterHintMode() {
        const els = clickables();
        if (!els.length)
            return;

        const labels = hintLabels(els.length);
        const overlay = document.createElement('div');
        overlay.style.cssText =
            'position:fixed;inset:0;z-index:2147483647;pointer-events:none;';

        els.forEach((el, i) => {
            const r = el.getBoundingClientRect();
            const tag = document.createElement('span');
            tag.textContent = labels[i].toUpperCase();
            tag.dataset.label = labels[i];
            tag.style.cssText =
                `position:fixed;left:${Math.max(0, r.left)}px;`
                + `top:${Math.max(0, r.top)}px;`
                + 'background:#ffd76e;color:#302505;'
                + 'font:bold 11px/1.3 -apple-system,sans-serif;'
                + 'padding:1px 3px;border:1px solid #c38a22;border-radius:3px;'
                + 'box-shadow:0 1px 2px rgba(0,0,0,.4);';
            overlay.appendChild(tag);
        });

        document.documentElement.appendChild(overlay);
        hintState = {els, labels, typed: '', overlay};
    }

    function exitHintMode() {
        hintState?.overlay.remove();
        hintState = null;
    }

    function activate(el) {
        el.focus();
        const editable = el.matches(
            'textarea, select, [contenteditable], input:not([type=button])'
            + ':not([type=submit]):not([type=checkbox]):not([type=radio])');
        if (!editable)
            el.click();
    }

    function handleHintKey(event) {
        if (event.key === 'Escape' || !hintChars.includes(event.key)) {
            exitHintMode();
            return;
        }

        hintState.typed += event.key;
        const matches = hintState.labels.filter(
            label => label.startsWith(hintState.typed));

        if (matches.length === 1 && matches[0] === hintState.typed) {
            const el = hintState.els[hintState.labels.indexOf(matches[0])];
            exitHintMode();
            activate(el);
            return;
        }

        if (!matches.length) {
            exitHintMode();
            return;
        }

        [...hintState.overlay.children].forEach(tag => {
            tag.style.display =
                tag.dataset.label.startsWith(hintState.typed) ? '' : 'none';
        });
    }

    // --- Key dispatch -----------------------------------------------------

    let pending = '';
    let pendingTimer = 0;

    function isEditable(el) {
        return el && (el.isContentEditable
            || ['INPUT', 'TEXTAREA', 'SELECT'].includes(el.tagName));
    }

    addEventListener('keydown', event => {
        if (event.metaKey && !event.ctrlKey && !event.altKey
            && event.key === 'l') {
            event.preventDefault();
            event.stopImmediatePropagation();
            post('openPalette');
            return;
        }

        if (event.metaKey || event.ctrlKey || event.altKey)
            return;

        if (hintState) {
            event.preventDefault();
            event.stopImmediatePropagation();
            handleHintKey(event);
            return;
        }

        if (isEditable(document.activeElement)) {
            if (event.key === 'Escape')
                document.activeElement.blur();
            return;
        }

        if (event.key.length !== 1) {
            pending = '';
            return;
        }

        const candidate = pending + event.key;
        clearTimeout(pendingTimer);

        if (bindings[candidate]) {
            pending = '';
            event.preventDefault();
            event.stopImmediatePropagation();
            bindings[candidate]();
            return;
        }

        const isPrefix = Object.keys(bindings)
            .some(seq => seq.startsWith(candidate));

        if (isPrefix) {
            pending = candidate;
            event.preventDefault();
            event.stopImmediatePropagation();
            pendingTimer = setTimeout(() => { pending = ''; }, 800);
            return;
        }

        pending = '';
    }, true);
})();
)JS";
