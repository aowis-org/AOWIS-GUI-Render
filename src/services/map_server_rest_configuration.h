#ifndef MAP_SERVER_REST_CONFIGURATION_H
#define MAP_SERVER_REST_CONFIGURATION_H

#include <QByteArray>
#include <QString>

// Where an aowis-server-map instance is reached over HTTP(S): base URL
// (scheme, host, optional port and path prefix, no trailing slash needed),
// the API key sent as X-API-Key and the key for cache deletions. The host
// owns where these come from (GUI configuration file, mobile settings).
struct MapServerRestConfiguration
{
    QString base_url;
    QByteArray api_key;
    QByteArray delete_api_key;

    bool configured() const { return !this->base_url.trimmed().isEmpty(); }
};

#endif // MAP_SERVER_REST_CONFIGURATION_H
