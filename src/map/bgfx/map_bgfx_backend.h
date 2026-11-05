#ifndef MAP_BGFX_BACKEND_H
#define MAP_BGFX_BACKEND_H

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>
#include <QVector>

#include <bgfx/bgfx.h>

#include <cstdint>
#include <functional>
#include <limits>

// Backend selection is bgfx's job: with RendererType::Count, bgfx::init()
// ranks the renderers compiled in for this platform and falls through to the
// next one when a renderer fails to initialise. AOWIS only has to guarantee
// that a matching compiled shader set exists for whatever bgfx picks.
//
// Which renderers are compiled in is decided by AOWIS_BGFX_RENDERERS
// (cmake/aowis_bgfx.cmake): Linux OpenGL + Vulkan, Windows Direct3D 11 +
// OpenGL, macOS Metal, WASM OpenGL ES 3 (WebGL 2).
//
// Shader binaries are pre-compiled, committed in src/map/bgfx/shaders/bin and
// embedded per shaderc profile family under :/aowis/map/bgfx/<directory>/
// <name>.bin (cmake/aowis_bgfx_shaders.cmake). These are the directory names
// bgfx's own examples use for the same families. A family whose binaries are
// out of date is not embedded, so shadersAvailable() rejects that renderer
// and the surface falls through to the next one.
namespace MapBgfxBackend
{
// Forces Qt resource registration even when the shader archive is linked as
// a static library. Safe before bgfx::init(), and idempotent for all hosts.
void initializeEmbeddedShaders();

inline const char *shaderDirectory(bgfx::RendererType::Enum renderer)
{
    switch (renderer)
    {
        case bgfx::RendererType::OpenGL:
            return "glsl";
        case bgfx::RendererType::OpenGLES:
            return "essl";
        case bgfx::RendererType::Vulkan:
            return "spirv";
        case bgfx::RendererType::Direct3D11:
            return "dx11";
        case bgfx::RendererType::Metal:
            return "metal";
        default:
            return nullptr;
    }
}

inline QString shaderResourcePath(bgfx::RendererType::Enum renderer, const char *name)
{
    initializeEmbeddedShaders();
    const char *directory = shaderDirectory(renderer);
    if (directory == nullptr || name == nullptr)
        return QString();
    return QStringLiteral(":/aowis/map/bgfx/%1/%2.bin")
        .arg(QString::fromLatin1(directory), QString::fromLatin1(name));
}

// Whether a GPU buffer size or vertex count fits the uint32_t arguments of
// bgfx::copy()/createDynamicVertexBuffer() (guards against silent truncation
// of the retained network buffers; not used for shader files).
//
// Never compare against qsizetype(std::numeric_limits<uint32_t>::max()):
// qsizetype is 32-bit on wasm32, where that cast yields -1 and every
// non-negative size compares "too large".
inline bool fitsUint32(qsizetype value)
{
    return value >= 0 && quint64(value) <= quint64(std::numeric_limits<uint32_t>::max());
}

// Shader from the embedded set of the renderer bgfx selected; invalid, with
// a warning naming the shader, when the resource is missing or bgfx rejects
// it. Every bgfx GPU part loads its shaders through here.
//
// No size check of our own: the binaries are a few hundred bytes compiled
// into the executable from committed files (cmake/aowis_bgfx_shaders.cmake
// refuses to embed a family with a missing .bin), and bgfx::createShader()
// validates the binary header itself.
inline bgfx::ShaderHandle loadShader(const char *name)
{
    const QString resource = shaderResourcePath(bgfx::getRendererType(), name);
    QFile file(resource);
    if (resource.isEmpty() || !file.open(QIODevice::ReadOnly))
    {
        qWarning() << "bgfx shader resource missing" << name << "for renderer"
                   << bgfx::getRendererName(bgfx::getRendererType());
        return BGFX_INVALID_HANDLE;
    }
    const QByteArray binary = file.readAll();
    const bgfx::ShaderHandle shader =
        bgfx::createShader(bgfx::copy(binary.constData(), uint32_t(binary.size())));
    if (!bgfx::isValid(shader))
        qWarning() << "bgfx rejected shader" << name << binary.size() << "bytes";
    return shader;
}

// Program from loadShader(); invalid (with a warning) when either shader is
// missing or rejected, or bgfx refuses to link the pair. The shaders are
// owned by the program (destroyShaders = true).
inline bgfx::ProgramHandle loadProgram(const char *vertex_name, const char *fragment_name)
{
    const bgfx::ShaderHandle vertex = loadShader(vertex_name);
    if (!bgfx::isValid(vertex))
        return BGFX_INVALID_HANDLE;
    const bgfx::ShaderHandle fragment = loadShader(fragment_name);
    if (!bgfx::isValid(fragment))
    {
        bgfx::destroy(vertex);
        return BGFX_INVALID_HANDLE;
    }
    const bgfx::ProgramHandle program = bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(program))
        qWarning() << "bgfx rejected program" << vertex_name << "+" << fragment_name;
    return program;
}

// Probe with the first vertex shader; every program is compiled for every
// profile in the same CMake loop, so one file stands for the whole set.
inline bool shadersAvailable(bgfx::RendererType::Enum renderer)
{
    const QString path = shaderResourcePath(renderer, "vs_network_2d");
    return !path.isEmpty() && QFile::exists(path);
}

// Renderers bgfx supports on this machine for which shaders are embedded,
// in bgfx's own order. Valid before bgfx::init().
inline QVector<bgfx::RendererType::Enum> usableRenderers()
{
    bgfx::RendererType::Enum supported[bgfx::RendererType::Count];
    const uint8_t count = bgfx::getSupportedRenderers(
        uint8_t(bgfx::RendererType::Count), supported);
    QVector<bgfx::RendererType::Enum> result;
    for (uint8_t index = 0; index < count; ++index)
    {
        if (supported[index] != bgfx::RendererType::Noop && shadersAvailable(supported[index]))
            result.append(supported[index]);
    }
    return result;
}

// Debug/diagnostic override only, deliberately not an ini option:
// AOWIS_BGFX_RENDERER=opengl|vulkan|d3d11|metal|auto. Anything else
// (including unset) means bgfx's automatic choice. A renderer that is not
// compiled into this build is skipped by the surface's fallback loop.
inline bgfx::RendererType::Enum requestedRenderer()
{
    const QByteArray value = qgetenv("AOWIS_BGFX_RENDERER").trimmed().toLower();
    if (value == "opengl" || value == "gl")
        return bgfx::RendererType::OpenGL;
    if (value == "vulkan" || value == "vk")
        return bgfx::RendererType::Vulkan;
    if (value == "d3d11")
        return bgfx::RendererType::Direct3D11;
    if (value == "metal")
        return bgfx::RendererType::Metal;
    return bgfx::RendererType::Count;
}
}

#endif // MAP_BGFX_BACKEND_H
