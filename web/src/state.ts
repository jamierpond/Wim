import { backend } from './generated/backend';
import { makeBridgeStore } from './generated/react';
import type { GoResults } from './generated/schema';

const initial: GoResults = {
    items: [],
    selected: 0,
    generation: 0,
    currentUrl: '',
};

export const useGoResults = makeBridgeStore({
    backend,
    event: 'results',
    fetch: backend.getResults,
    initial,
});
