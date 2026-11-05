#include "map/bgfx/bgfx_device_manager.h"
#include "map/bgfx/map_bgfx_backend.h"

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

    BgfxDeviceManager device;
    if (device.hasOwner() || device.generation() != 0 || device.secondaryClientCount() != 0)
    {
        std::cerr << "New renderer device has unexpected state\n";
        return EXIT_FAILURE;
    }
    std::cout << "Renderer link and embedded shader: PASS\n";
    return EXIT_SUCCESS;
}
