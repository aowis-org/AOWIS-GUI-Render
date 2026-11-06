#ifndef AOWIS_TEST_MOCK_BGFX_H
#define AOWIS_TEST_MOCK_BGFX_H

// Standalone GPU-submission test double. This header is never on a production
// target's include path; only the mock test compiles Render's actual .cpp
// against it. No graphics device, Qt or bgfx checkout is required.
#include <cstdint>
#include <cstring>
#include <vector>

#define BGFX_INVALID_HANDLE {static_cast<std::uint16_t>(0xffff)}
#define BGFX_BUFFER_ALLOW_RESIZE 1u
#define BGFX_BUFFER_INDEX32 2u
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
struct VertexLayout {};
struct Memory { std::vector<std::uint8_t> bytes; };

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
inline unsigned int created_vertex_buffers = 0;
inline unsigned int created_index_buffers = 0;
inline unsigned int destroyed_vertex_buffers = 0;
inline unsigned int destroyed_index_buffers = 0;
inline unsigned int vertex_updates = 0;
inline unsigned int index_updates = 0;
inline unsigned int uploaded_bytes = 0;
inline std::uint16_t next_handle = 100;
inline void reset()
{
    submissions.clear();
    current_vertex = 0xffff;
    current_state = 0;
    current_indexed = false;
    uniform_calls = 0;
    texture_calls = 0;
    index_calls = 0;
    created_vertex_buffers = 0;
    created_index_buffers = 0;
    destroyed_vertex_buffers = 0;
    destroyed_index_buffers = 0;
    vertex_updates = 0;
    index_updates = 0;
    uploaded_bytes = 0;
    next_handle = 100;
}
}

inline DynamicVertexBufferHandle createDynamicVertexBuffer(std::uint32_t,
                                                          const VertexLayout &, std::uint16_t)
{
    ++mock::created_vertex_buffers;
    return {mock::next_handle++};
}
inline DynamicIndexBufferHandle createDynamicIndexBuffer(std::uint32_t, std::uint16_t)
{
    ++mock::created_index_buffers;
    return {mock::next_handle++};
}
inline const Memory *copy(const void *data, std::uint32_t bytes)
{
    Memory *memory = new Memory;
    memory->bytes.resize(bytes);
    if (bytes != 0)
        std::memcpy(memory->bytes.data(), data, bytes);
    return memory;
}
inline void update(DynamicVertexBufferHandle, std::uint32_t, const Memory *memory)
{
    ++mock::vertex_updates;
    mock::uploaded_bytes += static_cast<unsigned int>(memory->bytes.size());
    delete memory;
}
inline void update(DynamicIndexBufferHandle, std::uint32_t, const Memory *memory)
{
    ++mock::index_updates;
    mock::uploaded_bytes += static_cast<unsigned int>(memory->bytes.size());
    delete memory;
}
inline void destroy(DynamicVertexBufferHandle) { ++mock::destroyed_vertex_buffers; }
inline void destroy(DynamicIndexBufferHandle) { ++mock::destroyed_index_buffers; }
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
