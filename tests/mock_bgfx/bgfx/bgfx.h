#ifndef AOWIS_TEST_MOCK_BGFX_H
#define AOWIS_TEST_MOCK_BGFX_H

// Standalone GPU-submission test double. This header is never on a production
// target's include path; only the mock test compiles Render's actual .cpp
// against it. No graphics device, Qt or bgfx checkout is required.
#include <cstdint>
#include <vector>

#define BGFX_INVALID_HANDLE {static_cast<std::uint16_t>(0xffff)}
#define BGFX_STATE_WRITE_RGB (std::uint64_t(1) << 0)
#define BGFX_STATE_WRITE_A (std::uint64_t(1) << 1)
#define BGFX_STATE_MSAA (std::uint64_t(1) << 2)
#define BGFX_STATE_WRITE_Z (std::uint64_t(1) << 3)
#define BGFX_STATE_DEPTH_TEST_LEQUAL (std::uint64_t(1) << 4)
#define BGFX_STATE_CULL_CW (std::uint64_t(1) << 5)
#define BGFX_STATE_BLEND_ONE (1)
#define BGFX_STATE_BLEND_INV_SRC_ALPHA (2)
#define BGFX_STATE_BLEND_FUNC(src,dst) ((std::uint64_t(src) << 8) | (std::uint64_t(dst) << 16))

namespace bgfx
{
using ViewId = std::uint16_t;
struct DynamicVertexBufferHandle { std::uint16_t idx; };
struct DynamicIndexBufferHandle { std::uint16_t idx; };
struct ProgramHandle { std::uint16_t idx; };
struct UniformHandle { std::uint16_t idx; };
struct TextureHandle { std::uint16_t idx; };

template <typename Handle>
bool isValid(Handle handle) { return handle.idx != 0xffff; }

namespace mock
{
struct Submission
{
    std::uint16_t vertex;
    std::uint16_t program;
    std::uint64_t state;
    bool indexed;
};
inline std::vector<Submission> submissions;
inline std::uint16_t current_vertex = 0xffff;
inline std::uint64_t current_state = 0;
inline bool current_indexed = false;
inline unsigned int uniform_calls = 0;
inline unsigned int texture_calls = 0;
inline unsigned int index_calls = 0;
inline void reset()
{
    submissions.clear();
    current_vertex = 0xffff;
    current_state = 0;
    current_indexed = false;
    uniform_calls = 0;
    texture_calls = 0;
    index_calls = 0;
}
}

inline void setVertexBuffer(std::uint8_t, DynamicVertexBufferHandle handle, std::uint32_t, std::uint32_t)
{
    mock::current_vertex = handle.idx;
}
inline void setIndexBuffer(DynamicIndexBufferHandle, std::uint32_t, std::uint32_t)
{
    mock::current_indexed = true;
    ++mock::index_calls;
}
inline void setUniform(UniformHandle, const void *) { ++mock::uniform_calls; }
inline void setTexture(std::uint8_t, UniformHandle, TextureHandle) { ++mock::texture_calls; }
inline void setState(std::uint64_t state) { mock::current_state = state; }
inline void submit(ViewId, ProgramHandle program)
{
    mock::submissions.push_back({mock::current_vertex, program.idx, mock::current_state,
                                 mock::current_indexed});
    mock::current_indexed = false;
}
}
#endif // AOWIS_TEST_MOCK_BGFX_H
