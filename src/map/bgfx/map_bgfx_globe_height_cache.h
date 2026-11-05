#ifndef MAP_BGFX_GLOBE_HEIGHT_CACHE_H
#define MAP_BGFX_GLOBE_HEIGHT_CACHE_H

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <unordered_map>

// Retain drawn-surface heights across camera frames, not across terrain
// revisions. Many icons have no drawn terrain below them: retrying those
// mesh/DEM queries on every frame was more expensive than the cache hits.
// Retry unavailable samples after a few overlay frames (or immediately after
// a surface revision), and bound the cache when selection lattices or camera
// travel introduce large numbers of distinct coordinates.
class MapBgfxGlobeHeightCache
{
public:
    static constexpr std::size_t MaxEntries = 262144;
    static constexpr std::uint64_t UnavailableRetryFrames = 8;

    void synchronize(std::uint64_t surface_revision, double vertical_exaggeration)
    {
        // Called once per globe overlay frame, independent of camera position.
        ++this->frame_sequence;
        if (!this->source_valid || this->surface_revision != surface_revision
            || this->vertical_exaggeration != vertical_exaggeration)
        {
            clearEntries();
            this->surface_revision = surface_revision;
            this->vertical_exaggeration = vertical_exaggeration;
            this->source_valid = true;
        }
    }

    void clear()
    {
        clearEntries();
        this->source_valid = false;
    }

    // Height samples may become available when a terrain mesh arrives.
    // Unavailable values are *not* treated as heights: the caller still uses
    // its existing ellipsoid fallback until the next successful query.
    // Negative entries only throttle expensive repeated failed queries.
    template <typename Sampler>
    bool height(double longitude_deg, double latitude_deg, double *height_m,
                const Sampler &sample)
    {
        if (height_m == nullptr || !std::isfinite(longitude_deg)
            || !std::isfinite(latitude_deg))
            return false;
        const Key key{bits(longitude_deg), bits(latitude_deg)};
        const std::unordered_map<Key, Entry, Hash>::iterator found =
            this->heights.find(key);
        if (found != this->heights.end())
        {
            if (found->second.known)
            {
                ++this->cache_hits;
                *height_m = found->second.height_m;
                return true;
            }
            if (this->frame_sequence < found->second.retry_after_frame)
            {
                ++this->unavailable_reuses;
                return false;
            }
        }

        double sampled_height_m = 0.0;
        ++this->terrain_samples;
        const bool known = sample(&sampled_height_m) && std::isfinite(sampled_height_m);
        const Entry entry{known ? sampled_height_m : 0.0,
                          this->frame_sequence + UnavailableRetryFrames, known};
        if (found != this->heights.end())
            found->second = entry;
        else
        {
            if (this->heights.size() >= MaxEntries)
            {
                // Insertion-ordered eviction avoids an ever-growing cache
                // without imposing LRU bookkeeping on every icon hit.
                this->heights.erase(this->insertion_order.front());
                this->insertion_order.pop_front();
                ++this->evictions;
            }
            this->heights.emplace(key, entry);
            this->insertion_order.push_back(key);
        }
        if (known)
            *height_m = sampled_height_m;
        return known;
    }

    std::size_t size() const { return this->heights.size(); }
    std::uint64_t hits() const { return this->cache_hits; }
    std::uint64_t samples() const { return this->terrain_samples; }
    std::uint64_t unavailableReuses() const { return this->unavailable_reuses; }
    std::uint64_t evictionCount() const { return this->evictions; }

private:
    struct Key
    {
        std::uint64_t longitude = 0;
        std::uint64_t latitude = 0;
        bool operator==(const Key &other) const
        {
            return this->longitude == other.longitude && this->latitude == other.latitude;
        }
    };

    struct Entry
    {
        double height_m = 0.0;
        std::uint64_t retry_after_frame = 0;
        bool known = false;
    };

    struct Hash
    {
        std::size_t operator()(const Key &key) const
        {
            const std::size_t a = std::hash<std::uint64_t>{}(key.longitude);
            const std::size_t b = std::hash<std::uint64_t>{}(key.latitude);
            return a ^ (b + std::size_t(0x9e3779b9u) + (a << 6) + (a >> 2));
        }
    };

    static std::uint64_t bits(double value)
    {
        if (value == 0.0)
            value = 0.0; // +0 and -0 share one entry.
        std::uint64_t result = 0;
        std::memcpy(&result, &value, sizeof(result));
        return result;
    }

    void clearEntries()
    {
        this->heights.clear();
        this->insertion_order.clear();
    }

    std::unordered_map<Key, Entry, Hash> heights;
    std::deque<Key> insertion_order;
    std::uint64_t surface_revision = 0;
    std::uint64_t frame_sequence = 0;
    double vertical_exaggeration = 0.0;
    bool source_valid = false;
    std::uint64_t cache_hits = 0;
    std::uint64_t terrain_samples = 0;
    std::uint64_t unavailable_reuses = 0;
    std::uint64_t evictions = 0;
};

#endif // MAP_BGFX_GLOBE_HEIGHT_CACHE_H
