#ifndef MAP_SERVER_REST_CONFIGURATION_H
#define MAP_SERVER_REST_CONFIGURATION_H

#include <QByteArray>
#include <QNetworkProxy>
#include <QString>

// Where an aowis-server-map instance is reached over HTTP(S): base URL
// (scheme, host, optional port and path prefix, no trailing slash needed),
// the API key sent as X-API-Key and the key for cache deletions. The host
// owns where these come from (GUI configuration file, mobile settings).
// proxy is set on every QNetworkAccessManager that talks to this server
// (e.g. the host's WireGuard SOCKS5 proxy); the default, DefaultProxy, keeps
// Qt's application proxy as before. It is never applied process-wide.
struct MapServerRestConfiguration
{
    QString base_url;
    QByteArray api_key;
    QByteArray delete_api_key;
    QNetworkProxy proxy;

    bool configured() const { return !this->base_url.trimmed().isEmpty(); }
};

#endif // MAP_SERVER_REST_CONFIGURATION_H
