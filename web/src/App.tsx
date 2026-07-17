import { useEffect, useRef, useState } from 'react';
import { backend } from './generated/backend';
import type { GoItem } from './generated/schema';
import { useGoResults } from './state';

// Dumb renderer over the native palette model: draw `results`, forward input.
// Matching, ranking, selection and live preview all happen in C++.
export default function App() {
    const results = useGoResults();
    const [query, setQuery] = useState('');
    const inputRef = useRef<HTMLInputElement>(null);
    const selectedRef = useRef<HTMLLIElement>(null);

    useEffect(() => {
        setQuery('');
        const focus = () => inputRef.current?.focus();
        focus();
        const late = setTimeout(focus, 60);
        return () => clearTimeout(late);
    }, [results.generation]);

    useEffect(() => {
        selectedRef.current?.scrollIntoView({ block: 'nearest' });
    }, [results.selected, results.items]);

    const onQueryChange = (text: string) => {
        setQuery(text);
        void backend.setQuery({ text });
    };

    const onKeyDown = (e: React.KeyboardEvent) => {
        const ctrl = e.ctrlKey;

        if (e.key === 'ArrowDown' || (ctrl && (e.key === 'j' || e.key === 'n'))) {
            e.preventDefault();
            void backend.moveSelection({ delta: 1 });
        } else if (e.key === 'ArrowUp' || (ctrl && (e.key === 'k' || e.key === 'p'))) {
            e.preventDefault();
            void backend.moveSelection({ delta: -1 });
        } else if (e.key === 'Enter') {
            e.preventDefault();
            void backend.activate();
        } else if (e.key === 'Escape') {
            e.preventDefault();
            void backend.cancel();
        }
    };

    return (
        <div className="backdrop" onMouseDown={() => void backend.cancel()}>
            <div className="palette" onMouseDown={(e) => e.stopPropagation()}>
                <input
                    ref={inputRef}
                    className="query"
                    value={query}
                    placeholder="Go to a place, tab, search, or URL…"
                    spellCheck={false}
                    autoFocus
                    onChange={(e) => onQueryChange(e.target.value)}
                    onKeyDown={onKeyDown}
                />

                <ul className="items">
                    {results.items.map((item, index) => (
                        <Row
                            key={item.tabId >= 0 ? `tab:${item.tabId}` : `url:${item.url}`}
                            item={item}
                            selected={index === results.selected}
                            rowRef={index === results.selected ? selectedRef : undefined}
                        />
                    ))}

                    {results.items.length === 0 && (
                        <li className="empty">
                            {query
                                ? `Enter ↳ search for “${query}”`
                                : 'Nothing here yet'}
                        </li>
                    )}
                </ul>
            </div>
        </div>
    );
}

function Row({
    item,
    selected,
    rowRef,
}: {
    item: GoItem;
    selected: boolean;
    rowRef?: React.Ref<HTMLLIElement>;
}) {
    if (item.isSearch) {
        return (
            <li
                ref={rowRef}
                className={`row search ${selected ? 'selected' : ''}`}
                onClick={() => void backend.choose(item)}
            >
                <span className="magnifier">⌕</span>
                <span className="text">
                    <span className="title">{item.title}</span>
                </span>
            </li>
        );
    }

    return (
        <li
            ref={rowRef}
            className={`row ${selected ? 'selected' : ''}`}
            onClick={() => void backend.choose(item)}
        >
            <span className={`dot ${item.tabId >= 0 ? 'open' : ''}`} />

            <span className="text">
                <span className="title">{item.title || item.url}</span>
                <span className="url">{item.url}</span>
            </span>

            <button
                className={`star ${item.bookmarked ? 'active' : ''}`}
                title={item.bookmarked ? 'Remove from places' : 'Save as place'}
                onClick={(e) => {
                    e.stopPropagation();
                    void backend.toggleBookmark(item);
                }}
            >
                {item.bookmarked ? '★' : '☆'}
            </button>

            {item.tabId >= 0 && (
                <button
                    className="close"
                    title="Close tab"
                    onClick={(e) => {
                        e.stopPropagation();
                        void backend.closeItem(item);
                    }}
                >
                    ✕
                </button>
            )}
        </li>
    );
}
