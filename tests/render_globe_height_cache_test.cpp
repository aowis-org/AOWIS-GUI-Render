#include "map/bgfx/map_bgfx_globe_height_cache.h"

#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
int failures = 0;
void require(bool condition, const char *description)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", description);
        ++failures;
    }
}
}

int main()
{
    MapBgfxGlobeHeightCache cache;
    int samples = 0;
    double current = 123.0;
    const std::function<bool(double *)> sample = [&samples, &current](double *height)
    {
        ++samples;
        *height = current;
        return true;
    };
    double height = 0.0;
    cache.synchronize(10, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 123.0,
            "initial drawn-terrain query");
    current = 456.0;
    require(cache.height(9.0, 47.0, &height, sample) && height == 123.0 && samples == 1,
            "stable terrain is reused rather than sampled again");
    cache.synchronize(10, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && samples == 1,
            "camera frames do not invalidate successful heights");
    cache.synchronize(11, 1.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 456.0 && samples == 2,
            "new drawn surface invalidates successful heights");
    current = 789.0;
    cache.synchronize(11, 2.0);
    require(cache.height(9.0, 47.0, &height, sample) && height == 789.0 && samples == 3,
            "vertical exaggeration invalidates successful heights");

    int missing_calls = 0;
    bool terrain_ready = false;
    const std::function<bool(double *)> initially_missing =
        [&missing_calls, &terrain_ready](double *value)
    {
        ++missing_calls;
        *value = 42.0;
        return terrain_ready;
    };
    require(!cache.height(9.1, 47.1, &height, initially_missing)
            && !cache.height(9.1, 47.1, &height, initially_missing)
            && missing_calls == 1 && cache.unavailableReuses() == 1,
            "missing terrain is sampled once per retry interval, never reused as a height");
    terrain_ready = true;
    for (std::uint64_t frame = 1; frame < MapBgfxGlobeHeightCache::UnavailableRetryFrames; ++frame)
    {
        cache.synchronize(11, 2.0);
        require(!cache.height(9.1, 47.1, &height, initially_missing),
                "missing terrain is not treated as known before retry");
    }
    require(missing_calls == 1, "repeated unavailable samples are throttled");
    cache.synchronize(11, 2.0);
    require(cache.height(9.1, 47.1, &height, initially_missing) && height == 42.0
            && missing_calls == 2,
            "previously unavailable terrain is retried within bounded frames");
    require(cache.height(9.1, 47.1, &height, initially_missing)
            && missing_calls == 2,
            "successful retry becomes a stable height hit");

    terrain_ready = false;
    require(!cache.height(9.2, 47.2, &height, initially_missing),
            "a second coordinate records missing terrain");
    terrain_ready = true;
    cache.synchronize(12, 2.0);
    require(cache.height(9.2, 47.2, &height, initially_missing) && height == 42.0,
            "drawn surface revision retries missing terrain immediately");
    cache.clear();
    cache.synchronize(12, 2.0);
    current = 321.0;
    require(cache.height(9.0, 47.0, &height, sample) && height == 321.0,
            "backend teardown invalidates the cache even with identical revision");
    const int calls_before_invalid = samples;
    require(!cache.height(NAN, 47.0, &height, sample)
            && !cache.height(9.0, 47.0, nullptr, sample)
            && samples == calls_before_invalid,
            "invalid coordinates and output pointers never run terrain queries");

    // Repeated selection lattices / camera travel must not grow RAM forever.
    cache.clear();
    cache.synchronize(13, 2.0);
    const std::function<bool(double *)> flat = [](double *value)
    {
        *value = 12.0;
        return true;
    };
    const std::size_t extra = 16;
    for (std::size_t index = 0; index < MapBgfxGlobeHeightCache::MaxEntries + extra; ++index)
    {
        const double longitude = -180.0 + double(index) * 0.001;
        require(cache.height(longitude, 1.0, &height, flat) && height == 12.0,
                "bounded-cache insertion returns original terrain result");
    }
    require(cache.size() == MapBgfxGlobeHeightCache::MaxEntries,
            "terrain cache is bounded under sustained coordinate churn");
    require(cache.evictionCount() == extra,
            "oldest cache entries are evicted on the configured limit");
    const std::uint64_t previous_samples = cache.samples();
    require(cache.height(-180.0, 1.0, &height, flat)
            && cache.samples() == previous_samples + 1,
            "an evicted coordinate is sampled again correctly");
    cache.clear();
    require(cache.size() == 0, "clear releases all stored heights and missing entries");

    if (failures == 0)
        std::puts("All globe terrain-height cache tests passed.");
    return failures == 0 ? 0 : 1;
}
