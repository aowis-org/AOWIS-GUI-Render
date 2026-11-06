#ifndef MAP_GLOBE_RETAINED_ORIGIN_H
#define MAP_GLOBE_RETAINED_ORIGIN_H

#include "geo/geo_wgs84_ellipsoid.h"

#include <cmath>

// Geometry is expressed relative to this persistent, double-precision ECEF
// anchor. Camera/terrain origins may rebase independently without touching
// geometry or GPU buffers. Re-anchoring is permitted only for an actual
// geometry revision (e.g. loading a different network).
//
// One origin for the whole network (MapBgfxGlobeNetwork's buffers). Widely
// distributed networks would need per-spatial-chunk local frames; the
// retained chunk index is already available for that.
class MapGlobeRetainedOrigin
{
public:
    bool setGeometryOrigin(const GeoWgs84Ellipsoid::EcefPositionD &origin)
    {
        if (!isFinite(origin))
            return false;
        const bool changed = !this->initialized
            || this->origin.x != origin.x || this->origin.y != origin.y
            || this->origin.z != origin.z;
        this->origin = origin;
        this->initialized = true;
        return changed;
    }

    // Allows an empty scene to adopt its initial view origin. Further camera
    // rebasings must NEVER alter the retained vertex coordinate system.
    bool initializeFromCamera(const GeoWgs84Ellipsoid::EcefPositionD &origin)
    {
        if (this->initialized)
            return false;
        return setGeometryOrigin(origin);
    }

    bool isInitialized() const { return this->initialized; }
    const GeoWgs84Ellipsoid::EcefPositionD &originEcef() const
    {
        return this->origin;
    }

    static bool isFinite(const GeoWgs84Ellipsoid::EcefPositionD &origin)
    {
        return std::isfinite(origin.x) && std::isfinite(origin.y)
            && std::isfinite(origin.z);
    }

private:
    GeoWgs84Ellipsoid::EcefPositionD origin;
    bool initialized = false;
};

#endif // MAP_GLOBE_RETAINED_ORIGIN_H
