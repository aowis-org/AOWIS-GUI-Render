#include "services/rest_client.h"

#include <QDebug>
#include <QVariant>

#ifndef Q_OS_WASM
#include <QSslError>
#include <QSslSocket>
#include <QStringList>
#endif

namespace
{
constexpr int MaximumErrorBodyLength = 512;
#ifndef Q_OS_WASM
constexpr int TileNetworkManagerCount = 8;
#endif
}

RESTClient::RESTClient(const QString &url_base, QObject *parent)
    : RESTClient(url_base, QByteArray(), QByteArray(), parent)
{
}

RESTClient::RESTClient(const QString &url_base, const QByteArray &api_key,
                       const QByteArray &delete_api_key, QObject *parent)
    : QObject(parent),
      url_base(url_base.trimmed()),
      api_key(api_key),
      delete_api_key(delete_api_key)
{
    while (this->url_base.endsWith('/'))
        this->url_base.chop(1);

    const QUrl parsed_url(this->url_base);
    if (!parsed_url.isValid() || parsed_url.scheme().isEmpty() || parsed_url.host().isEmpty())
        qCritical() << "Invalid REST base URL:" << this->url_base;

#ifndef Q_OS_WASM
    if (parsed_url.scheme() == QStringLiteral("https"))
    {
        qInfo() << "REST HTTPS support:"
                << "available=" << QSslSocket::supportsSsl()
                << "build=" << QSslSocket::sslLibraryBuildVersionString()
                << "runtime=" << QSslSocket::sslLibraryVersionString();

        if (!QSslSocket::supportsSsl())
            qCritical() << "Qt cannot use HTTPS because no TLS backend is available";
    }

    this->tile_network_managers.reserve(TileNetworkManagerCount);
    this->tile_network_manager_loads.reserve(TileNetworkManagerCount);
    for (int index = 0; index < TileNetworkManagerCount; ++index)
    {
        this->tile_network_managers.append(new QNetworkAccessManager(this));
        this->tile_network_manager_loads.append(0);
    }
#endif
}

QNetworkRequest RESTClient::createRequest(const QString &endpoint, const QByteArray &key) const
{
    QString normalized_endpoint = endpoint;
    if (!normalized_endpoint.startsWith('/'))
        normalized_endpoint.prepend('/');

    const QUrl request_url(this->url_base + normalized_endpoint);
    QNetworkRequest request(request_url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(65000);
    request.setRawHeader("User-Agent", "AOWIS-SERVER-GUI");
    if (!key.isEmpty())
        request.setRawHeader("X-API-Key", key);
    return request;
}

void RESTClient::monitorReply(QNetworkReply *reply) const
{
#ifndef Q_OS_WASM
    connect(reply, &QNetworkReply::sslErrors, reply, [reply](const QList<QSslError> &errors)
    {
        QStringList error_messages;
        for (const QSslError &error : errors)
            error_messages.append(error.errorString());

        qWarning() << "TLS errors for" << reply->request().url()
                   << error_messages.join(QStringLiteral("; "));
    });
#else
    Q_UNUSED(reply);
#endif
}

QString RESTClient::replyErrorDescription(QNetworkReply *reply) const
{
    QString description = QStringLiteral("%1: %2")
        .arg(reply->request().url().toString(), reply->errorString());

    const QVariant status_value = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (status_value.isValid())
        description += QStringLiteral(" [HTTP %1]").arg(status_value.toInt());

    const QByteArray response_body = reply->readAll().left(MaximumErrorBodyLength).simplified();
    if (!response_body.isEmpty())
        description += QStringLiteral(" Response: %1").arg(QString::fromUtf8(response_body));

    return description;
}

void RESTClient::get(const QString &endpoint)
{
    QNetworkRequest request = createRequest(endpoint, this->api_key);
    request.setRawHeader("Accept", "application/json, text/plain");
    QNetworkReply *reply = this->network_manager.get(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        handleReply(reply);
    });
}

void RESTClient::getTile(const QString &endpoint, const QString &key)
{
    QNetworkRequest request = createRequest(endpoint, this->api_key);
    request.setRawHeader("Accept", "image/*");

    QNetworkAccessManager *manager = &this->network_manager;
    int manager_index = -1;
#ifndef Q_OS_WASM
    if (!this->tile_network_managers.isEmpty())
    {
        manager_index = 0;
        for (int index = 1; index < this->tile_network_managers.size(); ++index)
        {
            if (this->tile_network_manager_loads.at(index) <
                this->tile_network_manager_loads.at(manager_index))
            {
                manager_index = index;
            }
        }

        manager = this->tile_network_managers.at(manager_index);
        this->tile_network_manager_loads[manager_index]++;
    }
#endif

    QNetworkReply *reply = manager->get(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key, manager_index]
    {
#ifndef Q_OS_WASM
        if (manager_index >= 0 && manager_index < this->tile_network_manager_loads.size())
        {
            this->tile_network_manager_loads[manager_index] = qMax(
                0, this->tile_network_manager_loads.at(manager_index) - 1);
        }
#else
        Q_UNUSED(manager_index)
#endif
        handleReplyTile(reply, key);
    });
}

void RESTClient::getTerrainTile(const QString &endpoint, const QString &key)
{
    QNetworkRequest request = createRequest(endpoint, this->api_key);
    request.setRawHeader("Accept", "application/vnd.aowis.terrain");
    QNetworkReply *reply = this->network_manager.get(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, key]
    {
        handleReplyTerrainTile(reply, key);
    });
}

void RESTClient::post(const QString &endpoint, const QJsonObject &payload)
{
    QNetworkRequest request = createRequest(endpoint, this->api_key);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    const QJsonDocument document(payload);
    QNetworkReply *reply = this->network_manager.post(request, document.toJson());
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply]
    {
        handleReply(reply);
    });
}


void RESTClient::getControl(const QString &endpoint, const QString &request_key)
{
    QNetworkRequest request = createRequest(endpoint, this->api_key);
    request.setRawHeader("Accept", "application/json, text/plain");
    QNetworkReply *reply = this->network_manager.get(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request_key]
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit requestControlError(request_key, replyErrorDescription(reply));
            reply->deleteLater();
            return;
        }

        emit requestFinishedControl(request_key, reply->readAll());
        reply->deleteLater();
    });
}

void RESTClient::deleteControl(const QString &endpoint, const QString &request_key)
{
    QNetworkRequest request = createRequest(endpoint, this->delete_api_key);
    request.setRawHeader("Accept", "application/json, text/plain, */*");
    QNetworkReply *reply = this->network_manager.deleteResource(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request_key]
    {
        if (reply->error() != QNetworkReply::NoError)
        {
            emit requestControlError(request_key, replyErrorDescription(reply));
            reply->deleteLater();
            return;
        }

        emit requestFinishedControl(request_key, reply->readAll());
        reply->deleteLater();
    });
}

void RESTClient::deleteResource(const QString &endpoint, quint64 request_id)
{
    QNetworkRequest request = createRequest(endpoint, this->delete_api_key);
    request.setRawHeader("Accept", "*/*");
    QNetworkReply *reply = this->network_manager.deleteResource(request);
    monitorReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, request_id]
    {
        handleReplyDelete(reply, request_id);
    });
}

void RESTClient::handleReply(QNetworkReply *reply)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestError(replyErrorDescription(reply));
        reply->deleteLater();
        return;
    }

    emit requestFinished(reply->readAll());
    reply->deleteLater();
}

void RESTClient::handleReplyTile(QNetworkReply *reply, const QString &key)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestTileError(key, replyErrorDescription(reply));
        reply->deleteLater();
        return;
    }

    const QByteArray content_type = reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
    if (!content_type.isEmpty() && !content_type.startsWith("image/"))
    {
        const QByteArray response_body = reply->readAll().left(MaximumErrorBodyLength).simplified();
        emit requestTileError(key, QStringLiteral("%1 returned non-image content type %2. Response: %3")
                              .arg(reply->request().url().toString(),
                                   QString::fromLatin1(content_type),
                                   QString::fromUtf8(response_body)));
        reply->deleteLater();
        return;
    }

    emit requestFinishedTile(reply->readAll(), key);
    reply->deleteLater();
}

void RESTClient::handleReplyTerrainTile(QNetworkReply *reply, const QString &key)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestTerrainTileError(key, replyErrorDescription(reply));
        reply->deleteLater();
        return;
    }

    const QByteArray content_type =
        reply->header(QNetworkRequest::ContentTypeHeader).toByteArray().toLower();
    if (!content_type.isEmpty() &&
        !content_type.startsWith("application/vnd.aowis.terrain"))
    {
        const QByteArray response_body =
            reply->readAll().left(MaximumErrorBodyLength).simplified();
        emit requestTerrainTileError(
            key,
            QStringLiteral("%1 returned non-terrain content type %2. Response: %3")
                .arg(reply->request().url().toString(),
                     QString::fromLatin1(content_type),
                     QString::fromUtf8(response_body)));
        reply->deleteLater();
        return;
    }

    emit requestFinishedTerrainTile(reply->readAll(), key);
    reply->deleteLater();
}

void RESTClient::handleReplyDelete(QNetworkReply *reply, quint64 request_id)
{
    if (reply->error() != QNetworkReply::NoError)
    {
        emit requestDeleteError(request_id, replyErrorDescription(reply));
        reply->deleteLater();
        return;
    }

    emit requestFinishedDeleteData(request_id, reply->readAll());
    emit requestFinishedDelete(request_id);
    reply->deleteLater();
}
