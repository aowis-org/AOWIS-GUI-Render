#ifndef MAP_VIEW_TYPES_H
#define MAP_VIEW_TYPES_H

// View enums shared by every map host (moved from the GUI's
// common/_enums_structs.h, which includes this header).

enum MapProvider
{
    ArcGISSat       = 1,
    OpenStreetMap   = 2,
    OpenTopoMap     = 3,
    OSMCyclo        = 4,
    Custom          = 5
};

enum class MapViewMode
{
    TwoD,
    Globe
};

#endif // MAP_VIEW_TYPES_H
