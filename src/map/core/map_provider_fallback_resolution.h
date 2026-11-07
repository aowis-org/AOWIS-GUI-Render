#ifndef MAP_PROVIDER_FALLBACK_RESOLUTION_H
#define MAP_PROVIDER_FALLBACK_RESOLUTION_H

#include "map/core/map_provider_catalog.h"

// A valid fallback must extend the continuous zoom range. Requiring a strictly
// increasing maximum zoom also prevents cycles, including malformed saved preferences.
// All work is in memory; these functions are used by the imagery render paths.
namespace MapProviderFallbackResolution
{
inline const MapProviderEntry *find(const QVector<MapProviderEntry> &entries, const QString &id)
{
    for (const MapProviderEntry &entry : entries)
        if (entry.id == id) return &entry;
    return nullptr;
}

inline const MapProviderEntry *next(const QVector<MapProviderEntry> &entries,
                                   const QHash<QString, QString> &fallbacks,
                                   const MapProviderEntry *current)
{
    if (current == nullptr) return nullptr;
    const MapProviderEntry *candidate = find(entries, fallbacks.value(current->id));
    if (candidate == nullptr || !candidate->enabled ||
        candidate->maximum_zoom <= current->maximum_zoom ||
        candidate->minimum_zoom > current->maximum_zoom + 1)
        return nullptr;
    return candidate;
}

inline QString sourceForZoom(const QVector<MapProviderEntry> &entries,
                             const QHash<QString, QString> &fallbacks,
                             const QString &selected_id, int zoom)
{
    const MapProviderEntry *current = find(entries, selected_id);
    if (current == nullptr || !current->enabled || zoom < current->minimum_zoom)
        return {};
    // Each valid hop has a strictly higher maximum; bound traversal defensively.
    for (qsizetype i = 0; i < entries.size(); ++i)
    {
        if (zoom <= current->maximum_zoom) return current->id;
        const MapProviderEntry *candidate = next(entries, fallbacks, current);
        if (candidate == nullptr) break;
        current = candidate;
    }
    // No higher-resolution source: render the last usable source overzoomed.
    return current->id;
}

inline int maximumSourceZoom(const QVector<MapProviderEntry> &entries,
                             const QHash<QString, QString> &fallbacks,
                             const QString &selected_id)
{
    const MapProviderEntry *current = find(entries, selected_id);
    if (current == nullptr || !current->enabled) return 0;
    for (qsizetype i = 0; i < entries.size(); ++i)
    {
        const MapProviderEntry *candidate = next(entries, fallbacks, current);
        if (candidate == nullptr) break;
        current = candidate;
    }
    return current->maximum_zoom;
}
}

#endif // MAP_PROVIDER_FALLBACK_RESOLUTION_H
