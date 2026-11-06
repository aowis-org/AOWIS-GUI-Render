#ifndef MAP_FRAME_PACER_H
#define MAP_FRAME_PACER_H

#include <algorithm>
#include <cmath>

// Converts measured wall-clock frame intervals into presentation intervals.
//
// With BGFX_RESET_VSYNC the GUI thread blocks in bgfx::frame() until the
// next vertical blank, so frames are *presented* exactly one refresh period
// apart, but the measured time between two render callbacks still jitters
// by a few milliseconds (event dispatch, Qt timers, compositor). Advancing
// the camera by that raw measurement makes the per-frame step jitter by the
// same ratio (+-20 % at 60 Hz), which is visible during smooth zoom.
//
// The pacer quantizes the elapsed time to whole refresh periods and carries
// the remainder to the next frame. Camera time therefore never drifts from
// wall time by more than half a period, while a vsync-locked stream gets
// exactly one period per frame. When the period is unknown it is estimated
// from the incoming intervals (WASM requestAnimationFrame timestamps).
class MapFramePacer
{
public:
    void reset()
    {
        this->carry_seconds = 0.0;
    }

    // refresh_period_seconds <= 0 selects the internal estimate.
    double advance(double measured_seconds, double refresh_period_seconds)
    {
        if (!std::isfinite(measured_seconds) || measured_seconds <= 0.0)
            return 0.0;
        observe(measured_seconds);

        // A stall is not something to replay in fixed steps.
        if (measured_seconds > LongGapSeconds)
        {
            this->carry_seconds = 0.0;
            return measured_seconds;
        }

        const double period = refresh_period_seconds > 0.0
                && std::isfinite(refresh_period_seconds)
            ? refresh_period_seconds
            : this->estimated_period_seconds;
        if (!(period >= MinimumPeriodSeconds && period <= MaximumPeriodSeconds))
            return measured_seconds;

        const double total = measured_seconds + this->carry_seconds;
        const double frames = std::floor(total / period + 0.5);
        const double presented = std::max(0.0, frames) * period;
        this->carry_seconds = std::clamp(total - presented, -period, period);
        return presented;
    }

    double estimatedPeriodSeconds() const { return this->estimated_period_seconds; }

private:
    static constexpr double MinimumPeriodSeconds = 1.0 / 500.0;
    static constexpr double MaximumPeriodSeconds = 1.0 / 20.0;
    static constexpr double LongGapSeconds = 0.25;

    void observe(double measured_seconds)
    {
        if (measured_seconds < MinimumPeriodSeconds || measured_seconds > MaximumPeriodSeconds)
            return;
        if (this->estimated_period_seconds <= 0.0)
        {
            this->estimated_period_seconds = measured_seconds;
            return;
        }
        // Ignore skipped frames (2x, 3x period) and outliers.
        const double ratio = measured_seconds / this->estimated_period_seconds;
        if (ratio > 0.75 && ratio < 1.25)
            this->estimated_period_seconds += 0.05 * (measured_seconds - this->estimated_period_seconds);
    }

    double carry_seconds = 0.0;
    double estimated_period_seconds = 0.0;
};

#endif // MAP_FRAME_PACER_H
