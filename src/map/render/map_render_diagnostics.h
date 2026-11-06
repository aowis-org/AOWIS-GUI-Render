#ifndef MAP_RENDER_DIAGNOSTICS_H
#define MAP_RENDER_DIAGNOSTICS_H

// Opt-in map rendering counters. Enable AOWIS_MAP_RENDER_DIAGNOSTICS=1
// before starting the application: the bgfx map then logs a cumulative JSON
// snapshot at most once every five seconds while it presents frames
// (MapBgfxSurface::renderFrameAt()). Tests query single counters without a
// GPU or a Qt window. None of them changes rendering behaviour.

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>

namespace MapRenderDiagnostics
{
enum class Counter : size_t
{
    // MapModel camera changes.
    ZoomChange,
    CenterChange,
    // Presented bgfx frames: wall time for frame preparation plus
    // bgfx::frame(), including any VSync/compositor wait; not GPU time.
    Frame,
    // MapGlobeNetworkScene work.
    DemandAttachmentsPrepared,
    GlobeGeometryRebuilt,
    GlobeFlowRebuilt,
    // One persistent shared map surface serves all four workspace modes.
    BgfxDeviceStarted,     // successful GPU resource initialization
    BgfxDeviceStopped,     // owning device was shut down
    Bgfx2dScenePrepared,   // CPU display geometry actually re-prepared
    WorkspaceModeSwitch,   // synchronous mode-switch CPU duration
    GlobeEditorIconIndexBuild,  // geometry-revision-only ECEF index creation
    GlobeEditorIconAnchors,     // total anchors across 3D Editor icon frames
    GlobeEditorIconCandidates,  // anchors surviving the cheap frustum query
    GlobeEditorIconVisible,     // projected sprites inside the viewport
    GlobeEditorIconFrame,       // CPU time of one complete 3D Editor icon overlay
    GlobeEditorIconIndexQuery,  // CPU time selecting frustum candidates
    GlobeEditorIconProjection,  // CPU time including terrain-aware projection
    GlobeEditorIconDraw,        // CPU time sorting and submitting HUD sprites
    GlobeTerrainHeightCacheHit, // drawn terrain reused without another ray query
    GlobeTerrainHeightSample,   // actual terrain sampler invocation
    GlobeTerrainHeightUnavailableReuse, // missing terrain retry deferred
    GlobeTerrainHeightEviction, // bounded height cache evicted oldest entry
    // Optional 2D frame breakdown. The bgfx present step includes VSync
    // waiting, so these separate CPU work from compositor throttling.
    Bgfx2dImagery,
    Bgfx2dMeshSync,
    Bgfx2dStyleSync,
    Bgfx2dNetworkDraw,
    Bgfx2dLabels,
    BgfxHud,
    BgfxPresent,
    TileDecodeCompletion,  // GUI thread cache insertion and tile-ready signal
    Tile2dUpload,          // bgfx tile upload calls accepted by the 2D atlas
    TileGlobeUpload,       // bgfx tile upload calls accepted by the globe atlas
    Count
};

inline constexpr std::array<const char *, static_cast<std::size_t>(Counter::Count)> CounterNames{{
    "zoom_change", "center_change", "frames",
    "demand_attachments_prepared", "globe_geometry_rebuilt", "globe_flow_rebuilt",
    "bgfx_device_started", "bgfx_device_stopped", "bgfx_2d_scene_prepared",
    "workspace_mode_switch", "globe_editor_icon_index_build",
    "globe_editor_icon_anchors", "globe_editor_icon_candidates",
    "globe_editor_icon_visible", "globe_editor_icon_frame",
    "globe_editor_icon_index_query", "globe_editor_icon_projection",
    "globe_editor_icon_draw", "globe_terrain_height_cache_hit",
    "globe_terrain_height_sample", "globe_terrain_height_unavailable_reuse",
    "globe_terrain_height_eviction",
    "bgfx_2d_imagery", "bgfx_2d_mesh_sync", "bgfx_2d_style_sync",
    "bgfx_2d_network_draw", "bgfx_2d_labels", "bgfx_hud",
    "bgfx_present", "tile_decode_completion", "tile_2d_upload", "tile_globe_upload"
}};

using Clock = std::chrono::steady_clock;

struct State
{
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> values{};
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> nanoseconds{};
    std::array<std::atomic<quint64>, static_cast<std::size_t>(Counter::Count)> max_nanoseconds{};
    Clock::time_point started = Clock::now();
    std::atomic<qint64> next_report_ms{5000};
};

inline State &state()
{
    static State instance;
    return instance;
}

inline bool enabled()
{
    static const bool is_enabled = []
    {
        const char *value = std::getenv("AOWIS_MAP_RENDER_DIAGNOSTICS");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return is_enabled;
}

inline void add(Counter counter, quint64 amount = 1)
{
    if (enabled())
        state().values[static_cast<size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

inline quint64 value(Counter counter)
{
    return state().values[static_cast<size_t>(counter)].load(std::memory_order_relaxed);
}

class Scope final
{
public:
    explicit Scope(Counter counter) : counter_(counter), active_(enabled())
    {
        if (this->active_)
            this->started_ = Clock::now();
    }

    ~Scope()
    {
        if (!this->active_)
            return;
        const std::chrono::nanoseconds::rep duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - this->started_).count();
        State &counters = state();
        const std::size_t index = static_cast<size_t>(this->counter_);
        counters.values[index].fetch_add(1, std::memory_order_relaxed);
        const quint64 elapsed_ns = static_cast<quint64>(duration);
        counters.nanoseconds[index].fetch_add(elapsed_ns, std::memory_order_relaxed);
        quint64 previous_max = counters.max_nanoseconds[index].load(std::memory_order_relaxed);
        while (previous_max < elapsed_ns
            && !counters.max_nanoseconds[index].compare_exchange_weak(
                previous_max, elapsed_ns, std::memory_order_relaxed))
        {
        }
    }

    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Counter counter_;
    bool active_ = false;
    Clock::time_point started_{};
};

inline QJsonObject snapshot()
{
    const State &counters = state();
    QJsonObject counts;
    QJsonObject durations_ms;
    QJsonObject maximum_ms;
    for (std::size_t index = 0; index < CounterNames.size(); ++index)
    {
        counts.insert(QString::fromLatin1(CounterNames[index]),
            static_cast<double>(counters.values[index].load(std::memory_order_relaxed)));
        const quint64 nanos = counters.nanoseconds[index].load(std::memory_order_relaxed);
        if (nanos != 0)
            durations_ms.insert(QString::fromLatin1(CounterNames[index]), double(nanos) / 1.0e6);
        const quint64 maximum = counters.max_nanoseconds[index].load(std::memory_order_relaxed);
        if (maximum != 0)
            maximum_ms.insert(QString::fromLatin1(CounterNames[index]), double(maximum) / 1.0e6);
    }
    return QJsonObject{
        {QStringLiteral("kind"), QStringLiteral("aowis_map_render_diagnostics")},
        {QStringLiteral("elapsed_ms"), double(std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - counters.started).count())},
        {QStringLiteral("counts"), counts},
        {QStringLiteral("total_duration_ms"), durations_ms},
        {QStringLiteral("max_duration_ms"), maximum_ms}
    };
}

// Capture an immediate snapshot after a mode switch. Ordinary frames retain
// their existing at-most-once-per-five-seconds reporting cadence.
inline void reportNow()
{
    if (enabled())
        qInfo().noquote() << QJsonDocument(snapshot()).toJson(QJsonDocument::Compact);
}

inline void reportIfDue()
{
    if (!enabled())
        return;
    State &counters = state();
    const qint64 elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now() - counters.started).count();
    qint64 deadline = counters.next_report_ms.load(std::memory_order_relaxed);
    if (elapsed_ms < deadline ||
        !counters.next_report_ms.compare_exchange_strong(
            deadline, elapsed_ms + 5000, std::memory_order_relaxed))
    {
        return;
    }
    qInfo().noquote() << QJsonDocument(snapshot()).toJson(QJsonDocument::Compact);
}
}

#endif // MAP_RENDER_DIAGNOSTICS_H
