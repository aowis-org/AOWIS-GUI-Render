#ifndef MAP_GLOBE_SURFACE_HIT_H
#define MAP_GLOBE_SURFACE_HIT_H

// A ray hit on the drawn globe surface (terrain mesh or, where no relief is
// drawn, the ellipsoid). Moved from the GUI's map_render_types.h, which
// includes this header.

#include "geo/geo_wgs84_ellipsoid.h"

#include <aowis/model/gis.h>

enum class MapGlobeSurfaceHitSource
{
    Invalid,
    TerrainMesh,
    EllipsoidFallback
};

struct MapGlobeSurfaceHit
{
    CoordinateWGS84 coordinate;
    GeoWgs84Ellipsoid::EcefPositionD ecef_position;
    double surface_height_m = 0.0;
    double distance_m = 0.0;
    MapGlobeSurfaceHitSource source = MapGlobeSurfaceHitSource::Invalid;

    bool isValid() const
    {
        return this->source != MapGlobeSurfaceHitSource::Invalid;
    }

    bool isTerrainMesh() const
    {
        return this->source == MapGlobeSurfaceHitSource::TerrainMesh;
    }
};

#endif // MAP_GLOBE_SURFACE_HIT_H
