#include "map/render/map_globe_tile_topology.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Globe terrain topology test failed at line " << __LINE__ << ": " << #condition << '\n'; \
    return 1; \
} } while (false)

struct TileVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};

int main()
{
    CHECK(mapGlobeTileIndices(0).isEmpty());
    CHECK(mapGlobeTileIndices(-1).isEmpty());
    CHECK(mapGlobeTileIndices(65).isEmpty());
    CHECK(mapGlobeTileVertexCount(64) == 4485);
    CHECK(mapGlobeTileIndexCount(64) == mapGlobeTileGridIndexCount(64) + 4 * 64 * 6);

    for (int cells : {1, 4, 64})
    {
        const QVector<quint16> &indices = mapGlobeTileIndices(cells);
        CHECK(int(indices.size()) == mapGlobeTileIndexCount(cells));
        CHECK(&indices == &mapGlobeTileIndices(cells));
        CHECK(indices.constData() == mapGlobeTileIndices(cells).constData());
        CHECK(indices[0] == 0 && indices[1] == cells + 1 && indices[2] == 1);
        CHECK(indices[3] == 1 && indices[4] == cells + 1 && indices[5] == cells + 2);
        for (quint16 index : indices)
            CHECK(index < mapGlobeTileVertexCount(cells));
        // After all grid triangles the first skirt quad joins top grid edge
        // g0,g1 to its lowered copies s0,s1 in the unchanged GPU order.
        const int first = mapGlobeTileGridIndexCount(cells);
        const int skirt = mapGlobeTileGridVertexCount(cells);
        CHECK(indices[first] == 0 && indices[first + 1] == skirt && indices[first + 2] == 1);
        CHECK(indices[first + 3] == 1 && indices[first + 4] == skirt
              && indices[first + 5] == skirt + 1);
        // All four walls contain exactly cells quad segments apiece.
        CHECK(int(indices.size()) - first == 4 * cells * 6);
    }

    QVector<TileVertex> vertices;
    vertices.resize(4);
    vertices[0] = {100.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    vertices[1] = {100.0f, 1.0f, 0.0f, 1.0f, 0.0f};
    vertices[2] = {100.0f, 0.0f, 1.0f, 0.0f, 1.0f};
    vertices[3] = {100.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    mapGlobeAppendTileSkirt(&vertices, 0, 1, 5.0, 0.0, 0.0, 0.0);
    CHECK(vertices.size() == 12);
    CHECK(vertices[4].x < vertices[0].x && std::fabs(vertices[4].x - 95.0f) < 1e-4f);
    CHECK(vertices[4].u == vertices[0].u && vertices[4].v == vertices[0].v);
    CHECK(vertices[6].v == vertices[2].v && vertices[7].v == vertices[3].v);
    CHECK(vertices[8].u == vertices[0].u && vertices[9].u == vertices[2].u);
    CHECK(vertices[10].u == vertices[1].u && vertices[11].u == vertices[3].u);
    // A rebase far from the origin still computes the skirt normal in the
    // full ECEF frame, not from the locally rebased relative coordinates.
    QVector<TileVertex> rebased;
    rebased.resize(4);
    for (int i = 0; i < 4; ++i)
        rebased[i] = {5.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    mapGlobeAppendTileSkirt(&rebased, 0, 1, 10.0, 6378137.0, 0.0, 0.0);
    CHECK(std::fabs(rebased[4].x + 5.0f) < 1e-3f);

    const double equator_depth = mapGlobeTileSkirtDepthM(10, 512, 4, 1.0, 0.0);
    CHECK(equator_depth > 9000.0 && equator_depth < 10000.0);
    CHECK(std::fabs(mapGlobeTileSkirtDepthM(10, 512, 4, 2.0, 1500.0)
          - 2.0 * (equator_depth + 1500.0)) < 1e-6);
    CHECK(mapGlobeTileSkirtDepthM(24, 0, 64, 1.0, 0.0) >= 20.0);

    // Concurrent callers must observe stable immutable cached topology.
    std::vector<const quint16 *> concurrent(8, nullptr);
    std::vector<std::thread> threads;
    for (std::size_t index = 0; index < concurrent.size(); ++index)
    {
        threads.emplace_back([&concurrent, index]() {
            for (int iteration = 0; iteration < 100; ++iteration)
                concurrent[index] = mapGlobeTileIndices(32).constData();
        });
    }
    for (std::thread &thread : threads)
        thread.join();
    for (const quint16 *data : concurrent)
        CHECK(data != nullptr && data == concurrent[0]);
    return 0;
}
#undef CHECK
