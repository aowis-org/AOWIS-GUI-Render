#ifndef MAP_BGFX_2D_IMAGERY_ATLAS_H
#define MAP_BGFX_2D_IMAGERY_ATLAS_H

#include <QHash>
#include <QPoint>
#include <QVector>
#include <QtGlobal>

// Bookkeeping for the 2D imagery tile atlas (MapBgfx2dImageryGpu).
//
// Instead of one texture per tile - one bgfx draw per tile, and in WebGL one
// JavaScript round trip per state change and draw call - the tiles live as
// cells in a few large atlas pages, so a frame's imagery is one draw per
// page run (normally one draw in total).
//
// A page is a grid of equal cells for one tile size: each cell holds the
// tile plus a one-texel gutter on every side that repeats the tile's edge
// texels. Bilinear sampling at a tile edge then reads the repeated edge, as
// clamp-to-edge sampling of a texture of its own did, so the opposite
// neighbour in the atlas never bleeds in (the imagery has no mipmaps, one
// texel of gutter is all bilinear filtering reaches).
//
// Pages are at most maximum_page_size texels wide and high (GPU limit, at
// most 4096) and sized for the tile budget: a page is only as high as the
// cells the budget still needs (256 px tiles at 4096: 15 columns, the budget
// of 384 tiles fits into a 4096 x 3870 and a 4096 x 2838 page).
//
// Pure bookkeeping (no bgfx): unit-tested without a GPU.
class MapBgfx2dImageryAtlas
{
public:
    static constexpr int Gutter = 1;
    static constexpr int MaximumPageSize = 4096;

    struct Page
    {
        bool valid = false;
        int tile_width = 0;
        int tile_height = 0;
        int columns = 0;
        int rows = 0;
        // Texels.
        int width = 0;
        int height = 0;
        // Unused cells (stack; cells are column + row * columns).
        QVector<int> free_cells;
        int used = 0;
    };

    struct Slot
    {
        int page = -1;
        int cell = -1;
        quint64 last_used_frame = 0;
    };

    struct Allocation
    {
        // -1: the tile does not fit into a page.
        int page = -1;
        int cell = -1;
        // The page is new: create its texture (page(index) has the size).
        bool page_created = false;
    };

    // maximum_page_size: the GPU's maximum texture size (clamped to
    // MaximumPageSize); tile_budget: tiles kept resident (pages are sized for
    // it). Drops everything.
    void reset(int maximum_page_size, int tile_budget);
    // Drops all pages and tiles (provider switch); sizes stay.
    void clear();

    bool contains(quint64 key) const { return this->tile_slots.contains(key); }
    Slot *slot(quint64 key);
    int tileCount() const { return int(this->tile_slots.size()); }
    // Tiles used (drawn or uploaded) in `frame`.
    int tilesUsedIn(quint64 frame) const;
    int pageCount() const;
    const Page &page(int index) const { return this->pages.at(index); }
    int pageSlots() const { return int(this->pages.size()); }

    // A cell for key (its own cell again when it is resident with the same
    // size). When the pages of this tile size are full, the least recently
    // used tile not used in `frame` gives up its cell while the atlas holds
    // the budget or more tiles; otherwise a new page is added.
    Allocation allocate(quint64 key, int tile_width, int tile_height, quint64 frame);
    // Frees key's cell (failed upload).
    void remove(quint64 key);
    // Frees the least recently used tiles above budget, never one used in
    // `frame`. Returns the pages that became empty and were dropped (their
    // textures are to be destroyed).
    QVector<int> trim(int budget, quint64 frame);

    // Texel position of the tile inside its cell (past the gutter).
    QPoint tileOrigin(const Slot &slot) const;
    // Atlas texture coordinates of tile coordinates (0..1 across the tile).
    double atlasU(const Slot &slot, double u) const;
    double atlasV(const Slot &slot, double v) const;

private:
    int createPage(int tile_width, int tile_height);
    void freeSlot(const Slot &slot);

    int maximum_page_size = MaximumPageSize;
    int tile_budget = 0;
    QHash<quint64, Slot> tile_slots;
    QVector<Page> pages;
};

#endif // MAP_BGFX_2D_IMAGERY_ATLAS_H
