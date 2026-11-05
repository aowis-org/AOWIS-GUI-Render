#ifndef MAP_GLOBE_VERTICAL_TRANSFORM_H
#define MAP_GLOBE_VERTICAL_TRANSFORM_H

#include <algorithm>
#include <cmath>

// Canonical vertical-placement policy for the Globe renderer.
//
// Keep this independent of the GPU backend: terrain,
// network geometry, picking/culling helpers and camera-follow code all need
// to agree on how a physical elevation becomes a rendered height above the
// WGS84 ellipsoid. Centralizing that rule here prevents the individual
// rendering paths from drifting apart when vertical exaggeration or network
// clearance changes.
class MapGlobeVerticalTransform
{
public:
    // The network is kept slightly above coincident terrain to avoid depth
    // fighting. A larger user-configured ground offset replaces this floor;
    // the two values are never stacked.
    static constexpr double MinimumNetworkAntiZFightingLiftM = 2.0;

    explicit MapGlobeVerticalTransform(
        double vertical_exaggeration = 1.0,
        double network_ground_offset_m = 0.0)
    {
        setVerticalExaggeration(vertical_exaggeration);
        setNetworkGroundOffsetM(network_ground_offset_m);
    }

    void setVerticalExaggeration(double vertical_exaggeration)
    {
        this->vertical_exaggeration =
            std::isfinite(vertical_exaggeration) && vertical_exaggeration > 0.0
            ? vertical_exaggeration
            : 1.0;
    }

    void setNetworkGroundOffsetM(double network_ground_offset_m)
    {
        this->network_ground_offset_m =
            std::isfinite(network_ground_offset_m)
            ? std::max(0.0, network_ground_offset_m)
            : 0.0;
    }

    double verticalExaggeration() const
    {
        return this->vertical_exaggeration;
    }

    double networkGroundOffsetM() const
    {
        return this->network_ground_offset_m;
    }

    double networkLiftM() const
    {
        return std::max(
            this->network_ground_offset_m,
            MinimumNetworkAntiZFightingLiftM);
    }

    double terrainHeightM(double elevation_m) const
    {
        const double finite_elevation_m = std::isfinite(elevation_m)
            ? elevation_m
            : 0.0;
        return finite_elevation_m * this->vertical_exaggeration;
    }

    double networkHeightM(double elevation_m) const
    {
        return terrainHeightM(elevation_m) + networkLiftM();
    }

    double renderedVerticalDistanceM(double distance_m) const
    {
        return std::isfinite(distance_m)
            ? distance_m * this->vertical_exaggeration
            : 0.0;
    }

    // Positive when the rendered network centerline lies below the rendered
    // terrain surface. This intentionally includes the network lift, so
    // underground/X-ray classification describes what is actually rendered,
    // not a different parallel elevation formula.
    double networkDepthBelowTerrainM(
        double network_elevation_m,
        double terrain_elevation_m) const
    {
        return terrainHeightM(terrain_elevation_m)
            - networkHeightM(network_elevation_m);
    }

private:
    double vertical_exaggeration = 1.0;
    double network_ground_offset_m = 0.0;
};

#endif // MAP_GLOBE_VERTICAL_TRANSFORM_H
