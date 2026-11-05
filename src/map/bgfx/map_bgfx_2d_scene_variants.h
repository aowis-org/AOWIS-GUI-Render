#ifndef MAP_BGFX_2D_SCENE_VARIANTS_H
#define MAP_BGFX_2D_SCENE_VARIANTS_H

#include "map/bgfx/map_bgfx_2d_scene.h"

// Two camera-independent presentations of one retained geometry revision.
// Monitor may declutter coincident nodes, while the editor must retain their
// precise positions. Preparing each variant once avoids rebuilding the CPU
// primitives at every mode switch. The active variant's preparationId stays
// stable when revisited, so dependent GPU/highlight staging sees its identity.
// GPU buffers still belong to MapBgfx2dGpu and can be re-uploaded on a switch.
class MapBgfx2dSceneVariants
{
public:
    MapBgfx2dSceneVariants()
    {
        this->scenes[0].setDeclutteringEnabled(false);
    }

    const MapBgfx2dScene &active() const { return this->scenes[this->active_index]; }
    MapBgfx2dScene &active() { return this->scenes[this->active_index]; }

    // Called for a new retained snapshot. Discard the old inactive variant
    // promptly so large previous networks are not kept in memory.
    bool prepare(const MapBgfx2dNetworkInput &retained)
    {
        if (!this->active().prepare(retained))
            return false;
        const int other_index = 1 - this->active_index;
        this->scenes[other_index] = MapBgfx2dScene();
        this->scenes[other_index].setDeclutteringEnabled(other_index != 0);
        return true;
    }

    // Returns true only when preparing previously uncached geometry.
    bool setDeclutteringEnabled(bool enabled, const MapBgfx2dNetworkInput &retained)
    {
        const int requested_index = enabled ? 1 : 0;
        if (this->active_index == requested_index)
            return false;
        this->active_index = requested_index;
        return this->active().prepare(retained);
    }

private:
    MapBgfx2dScene scenes[2];
    int active_index = 1;
};

#endif // MAP_BGFX_2D_SCENE_VARIANTS_H
