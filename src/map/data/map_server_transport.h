#ifndef MAP_SERVER_TRANSPORT_H
#define MAP_SERVER_TRANSPORT_H

#include <functional>

class InterfaceServerMap;
class QObject;

// How a map data repository reaches the map server. The host decides the
// transport (the GUI: REST over HTTP, or the in-process map server of the
// Standalone build) and its request limits; the repositories only use the
// InterfaceServerMap contract.
struct MapServerTransport
{
    // Creates the transport with the repository as parent. Empty, or a
    // nullptr result: no map server (nothing is requested).
    std::function<InterfaceServerMap *(QObject *parent)> create;
    // Map tile requests the tile repository keeps in flight (foreground /
    // background prefetch); 0 keeps the repository's defaults (48 / 16).
    int tile_requests_in_flight = 0;
    int background_tile_requests_in_flight = 0;
};

#endif // MAP_SERVER_TRANSPORT_H
