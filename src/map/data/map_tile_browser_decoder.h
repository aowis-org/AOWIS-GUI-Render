#ifndef MAP_TILE_BROWSER_DECODER_H
#define MAP_TILE_BROWSER_DECODER_H

#include <QByteArray>
#include <QImage>
#include <QObject>

#include <functional>

// Browser-native decoding of map tiles (WebAssembly only: built for
// Emscripten, called from MapTileRepository under Q_OS_WASM).
//
// Qt's image plugins decode tiles inside the WebAssembly module: libjpeg and
// libpng compiled to wasm, on MapTileRepository's two decode threads. The
// browser's own decoders are native code (SIMD, platform codecs) and run on
// the browser's decoding threads. So in the browser a tile goes
//
//   encoded bytes -> Blob -> createImageBitmap()   (browser, native, async)
//                 -> OffscreenCanvas 2D drawImage() + getImageData()
//                 -> written straight into a QImage(Format_RGBA8888)
//
// all of it on one dedicated decode worker: a pthread that returns to its JS
// event loop right away (kept alive), receives jobs from the GUI thread by
// emscripten_proxy_async() and runs the browser's promises there. Neither the
// GUI thread nor the wasm decode threads touch a pixel; the result is the
// same RGBA8888 image Qt's path delivers (MapTileRepository::tile()), so the
// bgfx zero-copy upload is unchanged.
//
// The canvas round trip yields straight (unpremultiplied) RGBA: exact for
// opaque tiles (basemap imagery; verified in Chromium against libpng and
// libjpeg-turbo: identical bytes); semi-transparent
// pixels are rounded through the canvas' premultiplied storage. Embedded
// colour profiles are ignored, as Qt does (colorSpaceConversion "none").
//
// Not available (decode() returns false, Qt decodes): until the decode
// worker has started (the first few tiles), and for good once the worker
// finds no createImageBitmap / OffscreenCanvas 2D (older Safari).
namespace MapTileBrowserDecoder
{
// Called on the GUI thread: an RGBA8888 image, or a null image when the
// browser could not decode the data (then decode it with Qt).
using Completion = std::function<void(const QImage &image)>;

// Starts decoding data in the browser; completion runs later on the GUI
// thread unless context was destroyed meanwhile. Call on the GUI thread.
bool decode(const QByteArray &data, QObject *context, Completion completion);
}

#endif // MAP_TILE_BROWSER_DECODER_H
