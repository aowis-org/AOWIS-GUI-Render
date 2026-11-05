#include "map/bgfx/map_bgfx_2d_style_binding.h"

#include <type_traits>

// The GPU calls ready() through this interface; if the host is not ready,
// it must not submit a draw. Keep this contract independent of Qt and bgfx.
class TestStyleBinding final : public MapBgfx2dStyleBinding
{
public:
    bool ready() const override { return this->initialized; }
    void bind(Mode, Table, bool) const override { }

    void setInitialized(bool initialized) { this->initialized = initialized; }

private:
    bool initialized = false;
};

int main()
{
    static_assert(std::is_abstract_v<MapBgfx2dStyleBinding>);
    static_assert(!std::is_abstract_v<TestStyleBinding>);

    TestStyleBinding implementation;
    const MapBgfx2dStyleBinding &binding = implementation;
    if (binding.ready())
        return 1;
    implementation.setInitialized(true);
    if (!binding.ready())
        return 2;
    binding.bind(MapBgfx2dStyleBinding::Mode::Symbology);
    return 0;
}
