#pragma once

#include <eacp/Graphics/Graphics.h>

#include "FuzzyMatch.h"

#include <algorithm>

namespace wim
{
using namespace eacp;
using namespace eacp::Graphics;

struct SwitcherInput final : TextInput
{
    using TextInput::TextInput;

    void keyDown(const KeyEvent& event) override
    {
        auto code = event.keyCode;
        auto ctrl = event.modifiers.control;

        if (code == KeyCode::Escape)
        {
            onCancel();
            return;
        }

        if (code == KeyCode::DownArrow || (ctrl && code == KeyCode::J)
            || (ctrl && code == KeyCode::N))
        {
            onMove(1);
            return;
        }

        if (code == KeyCode::UpArrow || (ctrl && code == KeyCode::K)
            || (ctrl && code == KeyCode::P))
        {
            onMove(-1);
            return;
        }

        TextInput::keyDown(event);
    }

    std::function<void()> onCancel = [] {};
    std::function<void(int)> onMove = [](int) {};
};

// fzf-style omnibar: fuzzy-filters the open tabs as you type. Enter picks the
// selected tab, or -- when nothing matches -- submits the query to the owner
// (which turns it into a Google search / URL in a new tab).
struct TabSwitcher final : View
{
    struct Item
    {
        std::string title;
        std::string url;
        int tabIndex = 0;
    };

    TabSwitcher()
    {
        input.setPlaceholder("Switch tab, or search…");
        input.setTextColor(Color::white(0.95f));
        input.setBackgroundColor(Color {0.09f, 0.09f, 0.11f});
        input.setBorderColor(Color {0.3f, 0.3f, 0.35f});

        input.onChange([this](const std::string&) { refilter(); });
        input.onSubmit([this](const std::string& text) { submit(text); });
        input.onCancel = [this] { onDismiss(); };
        input.onMove = [this](int delta) { move(delta); };

        setHandlesMouseEvents();
        addChildren({input});
    }

    void open(Vector<Item> allItems)
    {
        items = std::move(allItems);
        input.setText("");
        refilter();
        input.focus();
    }

    void resized() override
    {
        auto panel = panelBounds();
        input.setBounds(
            Rect {panel.x, panel.y, panel.w, inputHeight}.inset(10.f, 9.f));
    }

    void paint(Context& g) override
    {
        g.setColor(Color::black(0.35f));
        g.fillRect(getLocalBounds());

        auto panel = panelBounds();
        g.setColor(Color {0.12f, 0.12f, 0.14f});
        g.fillRoundedRect(panel, 12.f);

        auto rows = panel;
        rows.removeFromTop(inputHeight);

        if (filtered.empty())
        {
            g.setColor(Color::gray(0.6f));
            g.drawText("Search Google for \"" + input.getText() + "\"",
                       {rows.x + 18.f, rows.y + rowHeight / 2.f + 5.f},
                       titleFont);
            return;
        }

        auto first = firstVisibleRow();
        auto last = std::min(first + maxVisibleRows, (int) filtered.size());

        for (auto i = first; i < last; ++i)
        {
            auto row = rows.removeFromTop(rowHeight);
            auto& item = *filtered[(size_t) i].item;

            if (i == selected)
            {
                g.setColor(Color {0.25f, 0.45f, 0.9f, 0.35f});
                g.fillRoundedRect(row.inset(6.f, 2.f), 6.f);
            }

            g.setColor(Color::white(0.95f));
            g.drawText(
                truncated(item.title.empty() ? item.url : item.title, row.w - 36.f),
                {row.x + 18.f, row.y + 19.f},
                titleFont);

            g.setColor(Color::gray(0.6f));
            g.drawText(truncated(item.url, row.w - 36.f),
                       {row.x + 18.f, row.y + 36.f},
                       urlFont);
        }
    }

    void mouseDown(const MouseEvent& event) override
    {
        auto panel = panelBounds();

        if (!panel.contains(event.pos))
        {
            onDismiss();
            return;
        }

        auto rowArea = panel;
        rowArea.removeFromTop(inputHeight);

        if (!rowArea.contains(event.pos) || filtered.empty())
            return;

        auto index =
            firstVisibleRow() + (int) ((event.pos.y - rowArea.y) / rowHeight);

        if (index >= 0 && index < (int) filtered.size())
            onTabChosen(filtered[(size_t) index].item->tabIndex);
    }

    std::function<void()> onDismiss = [] {};
    std::function<void(int)> onTabChosen = [](int) {};
    std::function<void(const std::string&)> onQuerySubmitted = [](auto&) {};

private:
    struct Match
    {
        int score = 0;
        const Item* item = nullptr;
    };

    void refilter()
    {
        auto query = input.getText();
        filtered.clear();

        for (auto& item: items)
        {
            if (auto score = fuzzyScore(query, item.title + " " + item.url))
                filtered.push_back({*score, &item});
        }

        std::stable_sort(filtered.begin(),
                         filtered.end(),
                         [](const Match& a, const Match& b)
                         { return a.score > b.score; });

        selected = 0;
        repaint();
    }

    void move(int delta)
    {
        if (filtered.empty())
            return;

        selected = std::clamp(selected + delta, 0, (int) filtered.size() - 1);
        repaint();
    }

    void submit(const std::string& text)
    {
        if (!filtered.empty())
        {
            onTabChosen(filtered[(size_t) selected].item->tabIndex);
            return;
        }

        if (!text.empty())
        {
            onQuerySubmitted(text);
            return;
        }

        onDismiss();
    }

    int firstVisibleRow() const
    {
        return std::max(0, selected - maxVisibleRows + 1);
    }

    int visibleRowCount() const
    {
        return std::clamp((int) filtered.size(), 1, maxVisibleRows);
    }

    Rect panelBounds() const
    {
        auto bounds = getLocalBounds();
        auto width = std::min(680.f, bounds.w - 60.f);
        auto height = inputHeight + rowHeight * (float) visibleRowCount() + 8.f;

        return {(bounds.w - width) / 2.f, 60.f, width, height};
    }

    static std::string truncated(const std::string& text, float width)
    {
        auto maxChars = (size_t) std::max(width / 7.f, 8.f);

        if (text.size() <= maxChars)
            return text;

        return text.substr(0, maxChars - 1) + "…";
    }

    static constexpr auto inputHeight = 50.f;
    static constexpr auto rowHeight = 48.f;
    static constexpr auto maxVisibleRows = 8;

    SwitcherInput input {std::string()};
    Vector<Item> items;
    Vector<Match> filtered;
    int selected = 0;

    Font titleFont {FontOptions().withSize(14.f)};
    Font urlFont {FontOptions().withSize(11.f)};
};
} // namespace wim
