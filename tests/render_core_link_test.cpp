#include "map/bgfx/bgfx_device_manager.h"
#include "map/bgfx/map_bgfx_backend.h"
#include "map/bgfx/map_bgfx_globe_buffer_pool.h"
#include "map/bgfx/map_bgfx_globe_terrain_gpu.h"

#include <QCoreApplication>
#include <QFile>

#include <cstdlib>
#include <iostream>

#ifndef AOWIS_RENDER_TEST_SHADER_PATH
#error AOWIS_RENDER_TEST_SHADER_PATH must name an embedded shader resource
#endif

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    MapBgfxBackend::initializeEmbeddedShaders();
    const QString shaderPath = QString::fromLatin1(AOWIS_RENDER_TEST_SHADER_PATH);
    QFile shader(shaderPath);
    if (!shader.open(QIODevice::ReadOnly) || shader.size() <= 0)
    {
        std::cerr << "Missing embedded shader: " << shaderPath.toStdString() << '\n';
        return EXIT_FAILURE;
    }

    // The resource-pool entry point must link into the real RenderCore
    // without an initialized GPU. Only passive handle inspection is used.
    MapBgfxGlobeBufferPool gpu_buffers;
    MapBgfxGlobeGpu::Input input;
    gpu_buffers.populateInput(&input);
    for (const MapBgfxGlobeGpu::StreamBuffer &stream : input.streams)
    {
        if (stream.count != 0 || bgfx::isValid(stream.handle))
        {
            std::cerr << "New globe buffer pool contains an allocated GPU stream\n";
            return EXIT_FAILURE;
        }
    }

    // Force the base-terrain production GPU submission/resource object to
    // link from the real RenderCore archive without starting bgfx.
    MapBgfxGlobeTerrainGpu terrain_gpu;
    terrain_gpu.release();
    if (terrain_gpu.ready())
    {
        std::cerr << "Unexpected fresh globe terrain GPU resources\n";
        return EXIT_FAILURE;
    }

    BgfxDeviceManager device;
    if (device.hasOwner() || device.generation() != 0 || device.secondaryClientCount() != 0)
    {
        std::cerr << "New renderer device has unexpected state\n";
        return EXIT_FAILURE;
    }
    std::cout << "Renderer link and embedded shader: PASS\n";
    return EXIT_SUCCESS;
}
