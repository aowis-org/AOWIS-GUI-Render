#include "map/bgfx/map_bgfx_2d_imagery_atlas.h"

#include <QPair>

#include <algorithm>

void MapBgfx2dImageryAtlas::reset(int maximum_page_size, int tile_budget)
{
    this->maximum_page_size = std::clamp(maximum_page_size, 2 * (1 + 2 * Gutter), MaximumPageSize);
    this->tile_budget = std::max(1, tile_budget);
    clear();
}

void MapBgfx2dImageryAtlas::clear()
{
    this->tile_slots.clear();
    this->pages.clear();
}

MapBgfx2dImageryAtlas::Slot *MapBgfx2dImageryAtlas::slot(quint64 key)
{
    const QHash<quint64, Slot>::iterator found = this->tile_slots.find(key);
    return found == this->tile_slots.end() ? nullptr : &found.value();
}

int MapBgfx2dImageryAtlas::pageCount() const
{
    int count = 0;
    for (const Page &page : this->pages)
        count += page.valid ? 1 : 0;
    return count;
}

int MapBgfx2dImageryAtlas::createPage(int tile_width, int tile_height)
{
    const int cell_width = tile_width + 2 * Gutter;
    const int cell_height = tile_height + 2 * Gutter;
    const int columns = this->maximum_page_size / cell_width;
    const int maximum_rows = this->maximum_page_size / cell_height;
    if (columns < 1 || maximum_rows < 1)
        return -1;
    // Only as many rows as the budget still needs beyond the pages of this
    // tile size (at least one: the atlas may exceed the budget for a frame
    // that draws more tiles than it).
    int capacity = 0;
    for (const Page &page : this->pages)
    {
        if (page.valid && page.tile_width == tile_width && page.tile_height == tile_height)
            capacity += page.columns * page.rows;
    }
    const int needed_cells = std::max(columns, this->tile_budget - capacity);
    const int rows = std::min(maximum_rows, (needed_cells + columns - 1) / columns);

    Page page;
    page.valid = true;
    page.tile_width = tile_width;
    page.tile_height = tile_height;
    page.columns = columns;
    page.rows = rows;
    page.width = columns * cell_width;
    page.height = rows * cell_height;
    page.free_cells.reserve(columns * rows);
    // Stack: cell 0 is handed out first.
    for (int cell = columns * rows - 1; cell >= 0; --cell)
        page.free_cells.append(cell);

    for (int index = 0; index < this->pages.size(); ++index)
    {
        if (!this->pages.at(index).valid)
        {
            this->pages[index] = page;
            return index;
        }
    }
    this->pages.append(page);
    return int(this->pages.size()) - 1;
}

void MapBgfx2dImageryAtlas::freeSlot(const Slot &slot)
{
    Page &page = this->pages[slot.page];
    page.free_cells.append(slot.cell);
    --page.used;
}

MapBgfx2dImageryAtlas::Allocation MapBgfx2dImageryAtlas::allocate(
    quint64 key, int tile_width, int tile_height, quint64 frame)
{
    Allocation allocation;
    const QHash<quint64, Slot>::iterator existing = this->tile_slots.find(key);
    if (existing != this->tile_slots.end())
    {
        const Page &page = this->pages.at(existing->page);
        if (page.tile_width == tile_width && page.tile_height == tile_height)
        {
            // Same tile again (deleted and reloaded): overwrite its cell.
            existing->last_used_frame = frame;
            allocation.page = existing->page;
            allocation.cell = existing->cell;
            return allocation;
        }
        freeSlot(existing.value());
        this->tile_slots.erase(existing);
    }

    // A free cell in a page of this tile size.
    int page_index = -1;
    for (int index = 0; index < this->pages.size() && page_index < 0; ++index)
    {
        const Page &page = this->pages.at(index);
        if (page.valid && page.tile_width == tile_width && page.tile_height == tile_height
            && !page.free_cells.isEmpty())
            page_index = index;
    }
    // Full: at the budget, the least recently used tile of this size that
    // the current frame does not draw gives up its cell.
    if (page_index < 0 && this->tile_slots.size() >= this->tile_budget)
    {
        QHash<quint64, Slot>::iterator oldest = this->tile_slots.end();
        for (QHash<quint64, Slot>::iterator it = this->tile_slots.begin(); it != this->tile_slots.end(); ++it)
        {
            const Page &page = this->pages.at(it->page);
            if (it->last_used_frame != frame && page.tile_width == tile_width
                && page.tile_height == tile_height
                && (oldest == this->tile_slots.end() || it->last_used_frame < oldest->last_used_frame))
                oldest = it;
        }
        if (oldest != this->tile_slots.end())
        {
            page_index = oldest->page;
            freeSlot(oldest.value());
            this->tile_slots.erase(oldest);
        }
    }
    if (page_index < 0)
    {
        page_index = createPage(tile_width, tile_height);
        if (page_index < 0)
            return allocation;
        allocation.page_created = true;
    }

    Page &page = this->pages[page_index];
    Slot slot;
    slot.page = page_index;
    slot.cell = page.free_cells.takeLast();
    slot.last_used_frame = frame;
    ++page.used;
    this->tile_slots.insert(key, slot);
    allocation.page = slot.page;
    allocation.cell = slot.cell;
    return allocation;
}

void MapBgfx2dImageryAtlas::remove(quint64 key)
{
    const QHash<quint64, Slot>::iterator existing = this->tile_slots.find(key);
    if (existing == this->tile_slots.end())
        return;
    freeSlot(existing.value());
    this->tile_slots.erase(existing);
}

int MapBgfx2dImageryAtlas::tilesUsedIn(quint64 frame) const
{
    int used = 0;
    for (const Slot &slot : this->tile_slots)
    {
        if (slot.last_used_frame == frame)
            ++used;
    }
    return used;
}

QVector<int> MapBgfx2dImageryAtlas::trim(int budget, quint64 frame)
{
    QVector<int> dropped;
    if (this->tile_slots.size() > budget)
    {
        // Oldest first, never anything drawn in the current frame.
        QVector<QPair<quint64, quint64>> by_age;
        by_age.reserve(this->tile_slots.size());
        for (QHash<quint64, Slot>::const_iterator it = this->tile_slots.cbegin(); it != this->tile_slots.cend(); ++it)
        {
            if (it->last_used_frame != frame)
                by_age.append(qMakePair(it->last_used_frame, it.key()));
        }
        std::sort(by_age.begin(), by_age.end());
        qsizetype excess = this->tile_slots.size() - budget;
        for (const QPair<quint64, quint64> &entry : by_age)
        {
            if (excess <= 0)
                break;
            remove(entry.second);
            --excess;
        }
    }
    for (int index = 0; index < this->pages.size(); ++index)
    {
        Page &page = this->pages[index];
        if (page.valid && page.used == 0)
        {
            page = Page();
            dropped.append(index);
        }
    }
    return dropped;
}

QPoint MapBgfx2dImageryAtlas::tileOrigin(const Slot &slot) const
{
    const Page &page = this->pages.at(slot.page);
    const int column = slot.cell % page.columns;
    const int row = slot.cell / page.columns;
    return QPoint(column * (page.tile_width + 2 * Gutter) + Gutter,
                  row * (page.tile_height + 2 * Gutter) + Gutter);
}

double MapBgfx2dImageryAtlas::atlasU(const Slot &slot, double u) const
{
    const Page &page = this->pages.at(slot.page);
    const int column = slot.cell % page.columns;
    return (double(column * (page.tile_width + 2 * Gutter) + Gutter) + u * page.tile_width)
        / double(page.width);
}

double MapBgfx2dImageryAtlas::atlasV(const Slot &slot, double v) const
{
    const Page &page = this->pages.at(slot.page);
    const int row = slot.cell / page.columns;
    return (double(row * (page.tile_height + 2 * Gutter) + Gutter) + v * page.tile_height)
        / double(page.height);
}
