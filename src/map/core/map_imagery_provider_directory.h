#ifndef MAP_IMAGERY_PROVIDER_DIRECTORY_H
#define MAP_IMAGERY_PROVIDER_DIRECTORY_H

#include "map/core/map_view_types.h"

#include <QString>

// The host's imagery provider administration as MapModel sees it: which
// providers exist and are enabled, and which upstream source serves a
// provider at a zoom level (fallback chains already resolved). The GUI
// implements it with its server-backed MapProviderCatalog; a host notifies
// the model of changes through MapModel::providerCatalogChanged() and
// MapModel::providerFallbackChanged().
class MapImageryProviderDirectory
{
public:
    virtual ~MapImageryProviderDirectory() = default;

    virtual bool isProviderEnabled(const QString &provider_id) const = 0;
    // Empty when no provider is enabled.
    virtual QString firstEnabledProviderId() const = 0;
    // Empty when nothing serves provider_id at zoom.
    virtual QString sourceForZoom(const QString &provider_id, int zoom) const = 0;
    // Finest zoom any source in provider_id's fallback chain serves.
    virtual int maximumSourceZoom(const QString &provider_id) const = 0;
    // Zoom range of one source (a provider id); false when it is unknown.
    virtual bool sourceZoomRange(const QString &source, int *minimum_zoom,
                                 int *maximum_zoom) const = 0;
    // Provider id of a built-in MapProvider (ArcGISSat .. OSMCyclo); empty
    // for Custom.
    virtual QString builtinProviderId(MapProvider provider) const = 0;
};

#endif // MAP_IMAGERY_PROVIDER_DIRECTORY_H
