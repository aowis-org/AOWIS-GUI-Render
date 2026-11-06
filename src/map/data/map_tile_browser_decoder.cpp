#include "map/data/map_tile_browser_decoder.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>

#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/proxying.h>

#include <pthread.h>

#include <atomic>

namespace
{
// One tile on its way through the browser. Created and deleted on the GUI
// thread; the decode worker only reads data and fills image.
struct Job
{
    QByteArray data;
    QImage image;
    QPointer<QObject> context;
    MapTileBrowserDecoder::Completion completion;
};

enum WorkerState : int
{
    WorkerNotStarted,
    WorkerStarting,
    WorkerReady,
    WorkerUnavailable
};
std::atomic<int> worker_state{WorkerNotStarted};
pthread_t worker_thread;

// The decode worker: returns at once but stays alive (keepalive), idling in
// its JS event loop, where the proxied jobs and the browser's promise
// callbacks run.
void *workerMain(void *)
{
    emscripten_runtime_keepalive_push();
    worker_state.store(WorkerReady);
    return nullptr;
}

// Runs on the decode worker. Ends with _aowisBrowserTileDecoded(job, result):
// 1 decoded (pixels written via _aowisBrowserTileBuffer), 0 not decodable,
// -1 this browser lacks createImageBitmap / OffscreenCanvas 2D in workers.
// The Blob needs a copy of the bytes outside the shared wasm memory (slice);
// the canvas is reused per worker, "copy" compositing replaces its content.
EM_JS(void, aowis_browser_decode_tile, (void *job, const char *data, int size), {
    if (typeof createImageBitmap !== 'function' || typeof OffscreenCanvas !== 'function') {
        _aowisBrowserTileDecoded(job, -1);
        return;
    }
    const blob = new Blob([HEAPU8.slice(data, data + size)]);
    createImageBitmap(blob, { colorSpaceConversion: 'none' }).then((bitmap) => {
        let result = 0;
        try {
            const width = bitmap.width;
            const height = bitmap.height;
            if (width > 0 && height > 0 && width <= 4096 && height <= 4096) {
                if (!globalThis.aowisTileDecodeCanvas) {
                    globalThis.aowisTileDecodeCanvas = new OffscreenCanvas(width, height);
                    globalThis.aowisTileDecodeContext = globalThis.aowisTileDecodeCanvas.getContext(
                        '2d', { willReadFrequently: true });
                }
                const canvas = globalThis.aowisTileDecodeCanvas;
                const context = globalThis.aowisTileDecodeContext;
                if (!context) {
                    result = -1;
                } else {
                    if (canvas.width !== width || canvas.height !== height) {
                        canvas.width = width;
                        canvas.height = height;
                    }
                    context.globalCompositeOperation = 'copy';
                    context.drawImage(bitmap, 0, 0);
                    const pixels = context.getImageData(0, 0, width, height).data;
                    const target = _aowisBrowserTileBuffer(job, width, height);
                    if (target) {
                        HEAPU8.set(pixels, target);
                        result = 1;
                    }
                }
            }
        } catch (error) {
            result = 0;
        }
        bitmap.close();
        _aowisBrowserTileDecoded(job, result);
    }, () => {
        _aowisBrowserTileDecoded(job, 0);
    });
});

// Proxied from the GUI thread onto the decode worker.
void startJob(void *argument)
{
    Job *job = static_cast<Job *>(argument);
    aowis_browser_decode_tile(job, job->data.constData(), int(job->data.size()));
}
}

// Called from the JS above on the decode worker.
extern "C" EMSCRIPTEN_KEEPALIVE void *aowisBrowserTileBuffer(void *argument, int width, int height)
{
    Job *job = static_cast<Job *>(argument);
    job->image = QImage(width, height, QImage::Format_RGBA8888);
    // getImageData() rows are tightly packed, like RGBA8888 rows (4-byte
    // pixels, no padding).
    return job->image.isNull() ? nullptr : job->image.bits();
}

extern "C" EMSCRIPTEN_KEEPALIVE void aowisBrowserTileDecoded(void *argument, int result)
{
    Job *job = static_cast<Job *>(argument);
    if (result < 0)
        worker_state.store(WorkerUnavailable);
    if (result <= 0)
        job->image = QImage();
    // Back to the GUI thread (posting an event is thread-safe); the context
    // pointer is only looked at there.
    QMetaObject::invokeMethod(QCoreApplication::instance(), [job]()
    {
        if (job->context)
            job->completion(job->image);
        delete job;
    }, Qt::QueuedConnection);
}

bool MapTileBrowserDecoder::decode(const QByteArray &data, QObject *context, Completion completion)
{
    const int state = worker_state.load();
    if (state == WorkerNotStarted)
    {
        // Started on first use; tiles until it runs are decoded by Qt.
        worker_state.store(WorkerStarting);
        if (pthread_create(&worker_thread, nullptr, workerMain, nullptr) != 0)
            worker_state.store(WorkerUnavailable);
        return false;
    }
    if (state != WorkerReady)
        return false;
    Job *job = new Job{data, QImage(), QPointer<QObject>(context), std::move(completion)};
    if (!emscripten_proxy_async(emscripten_proxy_get_system_queue(), worker_thread, startJob, job))
    {
        delete job;
        return false;
    }
    return true;
}
