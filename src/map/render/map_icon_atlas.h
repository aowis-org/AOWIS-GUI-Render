#ifndef MAP_ICON_ATLAS_H
#define MAP_ICON_ATLAS_H

#include "map/core/map_network_types.h"

#include <QImage>
#include <QRectF>

// Backend-neutral icon-atlas metadata. Scene builders consume only this
// layout/aspect information; GPU backends remain responsible for creating
// and uploading their own texture resources from mapIconAtlasImage().
struct MapIconAtlasEntry
{
    bool valid = false;
    QRectF uv_rect;
    qreal width_ratio = 1.0;
    qreal height_ratio = 1.0;
};

QImage mapIconAtlasImage();
MapIconAtlasEntry mapIconAtlasEntry(InfrastructureEntity entity_type);
bool mapHasIcon(InfrastructureEntity entity_type);

#endif // MAP_ICON_ATLAS_H
