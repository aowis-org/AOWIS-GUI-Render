#ifndef AOWIS_BGFX_FRAME_COORDINATOR_H
#define AOWIS_BGFX_FRAME_COORDINATOR_H

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

// Owns process-wide bgfx view-ID reservations and the one frame submission
// entry point. No Qt/window dependencies: render clients may ultimately be
// maps, revision DAGs or offscreen previews.
//
// View IDs are global bgfx state, not local to a renderer. A map reserves a
// contiguous block (main view + globe heatmap bake views). Frame targets are
// deliberately not created here: they require native-window and backend
// ownership, which remains single-surface until a separate step-2 change.
class BgfxFrameCoordinator final
{
public:
    struct ViewRange
    {
        std::uint16_t first = 0;
        std::uint16_t count = 0;
        bool valid() const { return this->count != 0; }
        std::uint16_t at(std::uint16_t index) const { return std::uint16_t(this->first + index); }
    };

    using Submit = std::function<void()>;
    struct RenderWork
    {
        const void *client = nullptr;
        Submit draw;
    };
    explicit BgfxFrameCoordinator(Submit submit, std::uint16_t capacity = 256);

    BgfxFrameCoordinator(const BgfxFrameCoordinator &) = delete;
    BgfxFrameCoordinator &operator=(const BgfxFrameCoordinator &) = delete;

    // All callers must share the same GUI/render thread. A reservation belongs
    // to a live bgfx device generation; reset() invalidates every reservation.
    ViewRange reserve(const void *client, std::uint16_t count);
    bool release(const void *client);
    ViewRange views(const void *client) const;
    bool onlyClient(const void *client) const;
    // A single current map uses present(). If multiple clients hold view IDs,
    // renderAndPresent() can draw all currently active clients in one batch,
    // or advance only an already-drawn client while other views are idle.
    // Reserving a view does not require submitting it in every frame.
    bool present(const void *client);
    bool renderAndPresent(const std::vector<RenderWork> &work);
    // present() for a client that renders into a host-owned context: the
    // same checks, but the frame is executed without a swap (the host
    // presents). Needs setFlushSubmit().
    bool flush(const void *client);
    void setFlushSubmit(Submit flush_submit) { this->flush_api = std::move(flush_submit); }
    void reset();
    std::uint16_t capacity() const { return this->view_capacity; }

private:
    bool submitWork(const std::vector<RenderWork> &work, const Submit &submit);
    struct Reservation
    {
        const void *client = nullptr;
        ViewRange views;
    };
    const std::uint16_t view_capacity;
    Submit submit_api;
    Submit flush_api;
    std::vector<Reservation> reservations;
    bool presenting = false;
};

#endif // AOWIS_BGFX_FRAME_COORDINATOR_H
