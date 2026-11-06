#ifndef MAP_RENDER_TYPES_H
#define MAP_RENDER_TYPES_H

// Small value types of the map renderer (picking results, the underground
// display mode). Globe surface hits are Render's (map_globe_surface_hit.h).

#include "map/core/map_network_types.h"
#include "geo/geo_wgs84_ellipsoid.h"
#include "map/render/map_globe_surface_hit.h"

#include <aowis/model/gis.h>

#include <QUuid>

enum class MapUndergroundMode
{
    XRay,
    Hide,
    Solid
};

struct MapRenderHit
{
    quint32 render_id = 0;
    InfrastructureEntity entity_type = InfrastructureEntity::Unknown;
    QUuid uuid;

    bool isValid() const
    {
        return this->render_id != 0
            && this->entity_type != InfrastructureEntity::Unknown;
    }
};

#endif // MAP_RENDER_TYPES_H
