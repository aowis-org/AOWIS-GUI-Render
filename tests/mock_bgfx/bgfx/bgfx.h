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
#define BGFX_SAMPLER_U_CLAMP (std::uint64_t(1) << 22)
#define BGFX_SAMPLER_V_CLAMP (std::uint64_t(1) << 23)
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
struct Attrib
{
    enum Enum { Position, Normal, Color0, TexCoord0, TexCoord1, TexCoord2,
                TexCoord3, TexCoord4, TexCoord5 };
};
struct AttribType { enum Enum { Float }; };
struct VertexLayout
{
    struct Attribute { Attrib::Enum id; std::uint8_t components; };
    std::vector<Attribute> attributes;
    std::uint16_t stride = 0;
    VertexLayout &begin() { attributes.clear(); stride = 0; return *this; }
    VertexLayout &add(Attrib::Enum id, std::uint8_t components, AttribType::Enum)
    {
        attributes.push_back({id, components});
        stride += static_cast<std::uint16_t>(components * sizeof(float));
        return *this;
    }
    VertexLayout &skip(std::uint8_t bytes) { stride += bytes; return *this; }
    VertexLayout &end() { return *this; }
    std::uint16_t getStride() const { return stride; }
};
struct TextureFormat { enum Enum { RGBA8 }; };
struct UniformType { enum Enum { Mat4, Vec4, Sampler }; };
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
inline unsigned int created_uniforms = 0;
inline unsigned int created_textures = 0;
inline unsigned int destroyed_uniforms = 0;
inline unsigned int destroyed_textures = 0;
inline unsigned int destroyed_programs = 0;
inline unsigned int created_texture_bytes = 0;
inline unsigned int texture_mips = 0;
inline std::uint64_t texture_flags = 0;
inline int fail_uniform_after = -1;
inline int fail_texture_after = -1;
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
    created_uniforms = 0;
    created_textures = 0;
    destroyed_uniforms = 0;
    destroyed_textures = 0;
    destroyed_programs = 0;
    created_texture_bytes = 0;
    texture_mips = 0;
    texture_flags = 0;
    fail_uniform_after = -1;
    fail_texture_after = -1;
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
inline UniformHandle createUniform(const char *, UniformType::Enum)
{
    if (mock::fail_uniform_after >= 0
        && mock::created_uniforms >= static_cast<unsigned int>(mock::fail_uniform_after))
        return {0xffff};
    ++mock::created_uniforms;
    return {mock::next_handle++};
}
inline TextureHandle createTexture2D(std::uint16_t, std::uint16_t, bool has_mips,
                                     std::uint16_t, TextureFormat::Enum,
                                     std::uint64_t flags, const Memory *memory)
{
    mock::created_texture_bytes += static_cast<unsigned int>(memory->bytes.size());
    mock::texture_mips += has_mips ? 1u : 0u;
    mock::texture_flags = flags;
    delete memory;
    if (mock::fail_texture_after >= 0
        && mock::created_textures >= static_cast<unsigned int>(mock::fail_texture_after))
        return {0xffff};
    ++mock::created_textures;
    return {mock::next_handle++};
}
inline void destroy(UniformHandle) { ++mock::destroyed_uniforms; }
inline void destroy(TextureHandle) { ++mock::destroyed_textures; }
inline void destroy(ProgramHandle) { ++mock::destroyed_programs; }
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
