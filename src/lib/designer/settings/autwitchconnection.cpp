#include "autwitchconnection.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrlQuery>

namespace {
QByteArray Form(const QUrlQuery& query) { return query.query(QUrl::FullyEncoded).toUtf8(); }
}
AuTwitchConnection::AuTwitchConnection(QObject* parent, QNetworkAccessManager* network) : QObject(parent),
    m_Network(network ? network : new QNetworkAccessManager(this)), m_Poll(new QTimer(this)),
    m_Validation(new QTimer(this)), m_Deadline(0), m_Generation(0), m_Pages(0), m_Busy(false)
{
    m_Poll->setSingleShot(true);
    connect(m_Poll, &QTimer::timeout, this, &AuTwitchConnection::Poll);
    m_Validation->setInterval(60 * 60 * 1000);
    connect(m_Validation, &QTimer::timeout, this, &AuTwitchConnection::Validate);
}
void AuTwitchConnection::Request(const QString& url, const QByteArray& body, bool authenticated,
                                 std::function<void(int, QJsonObject)> done)
{
    QNetworkRequest request((QUrl(url)));
    if(authenticated) {
        request.setRawHeader("Client-Id", m_ClientId.toUtf8());
        request.setRawHeader("Authorization", "Bearer " + m_Token.toUtf8());
    }
    QNetworkReply* reply;
    if(body.isEmpty()) reply = m_Network->get(request);
    else {
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
        reply = m_Network->post(request, body);
    }
    QTimer::singleShot(30000, reply, [reply]() { if(!reply->isFinished()) reply->abort(); });
    const int generation = m_Generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done]() {
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonParseError error;
        QJsonObject json = QJsonDocument::fromJson(reply->readAll(), &error).object();
        reply->deleteLater();
        if(generation != m_Generation) return;
        if(error.error != QJsonParseError::NoError && status == 200) status = 0;
        done(status, json);
    });
}
void AuTwitchConnection::Disconnect()
{
    ++m_Generation;
    m_Poll->stop(); m_Validation->stop();
    m_Token.clear(); m_DeviceCode.clear(); m_UserId.clear();
    m_Names.clear(); m_Ids.clear(); m_Cursors.clear(); m_Busy = false; emit Busy(false);
    emit Participants(QStringList());
    emit Connected(false);
    emit Status(tr("Disconnected. Connect again to authorize this session."));
}
void AuTwitchConnection::Authorize(const QString& clientId)
{
    Disconnect();
    m_ClientId = clientId.trimmed();
    if(m_ClientId.isEmpty()) { emit Status(tr("Enter a public Twitch application's client ID first.")); return; }
    emit Status(tr("Requesting Twitch authorization…"));
    QUrlQuery query;
    query.addQueryItem("client_id", m_ClientId);
    query.addQueryItem("scopes", "moderator:read:chatters");
    Request("https://id.twitch.tv/oauth2/device", Form(query), false, [this](int code, QJsonObject json) {
        if(code != 200) { emit Status(tr("Could not start authorization (HTTP %1). Check the client ID and that the Twitch app is Public.").arg(code)); return; }
        m_DeviceCode = json.value("device_code").toString();
        QUrl uri(json.value("verification_uri").toString());
        if(m_DeviceCode.isEmpty() || uri.scheme() != "https" || uri.host() != "www.twitch.tv") {
            emit Status(tr("Twitch returned an invalid authorization response.")); return;
        }
        m_Deadline = QDateTime::currentSecsSinceEpoch() + json.value("expires_in").toInt(1800);
        m_Poll->setInterval(qMax(5, json.value("interval").toInt(5)) * 1000);
        emit Status(tr("Authorize in your browser. Code: %1 — %2").arg(json.value("user_code").toString(), uri.toString()));
        QDesktopServices::openUrl(uri);
        m_Poll->start();
    });
}
void AuTwitchConnection::Poll()
{
    if(QDateTime::currentSecsSinceEpoch() >= m_Deadline) { emit Status(tr("Authorization expired. Click Connect to try again.")); return; }
    QUrlQuery query;
    query.addQueryItem("client_id", m_ClientId);
    query.addQueryItem("device_code", m_DeviceCode);
    query.addQueryItem("scopes", "moderator:read:chatters");
    query.addQueryItem("grant_type", "urn:ietf:params:oauth:grant-type:device_code");
    Request("https://id.twitch.tv/oauth2/token", Form(query), false, [this](int code, QJsonObject json) {
        if(code == 400 && json.value("message").toString() == "authorization_pending") { m_Poll->start(); return; }
        if(json.value("message").toString() == "slow_down") { m_Poll->setInterval(m_Poll->interval() + 5000); m_Poll->start(); return; }
        if(code != 200 || json.value("access_token").toString().isEmpty()) {
            emit Status(tr("Authorization failed or was declined (HTTP %1). Click Connect to retry.").arg(code)); return;
        }
        // Session-only credentials: never write tokens into settings or logs.
        m_Token = json.value("access_token").toString();
        m_DeviceCode.clear();
        Validate();
    });
}
void AuTwitchConnection::Validate()
{
    // /validate accepts OAuth, while Helix uses Bearer.
    QNetworkRequest request(QUrl("https://id.twitch.tv/oauth2/validate"));
    request.setRawHeader("Authorization", "OAuth " + m_Token.toUtf8());
    QNetworkReply* reply = m_Network->get(request);
    const int generation = m_Generation;
    QTimer::singleShot(30000, reply, [reply]() { if(!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
        int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QJsonObject json = QJsonDocument::fromJson(reply->readAll()).object();
        reply->deleteLater();
        if(generation != m_Generation) return;
        if(code != 200 || json.value("client_id").toString() != m_ClientId ||
           !json.value("scopes").toArray().contains(QJsonValue("moderator:read:chatters")) || json.value("user_id").toString().isEmpty()) {
            Disconnect(); emit Status(tr("Session validation failed. Connect again to authorize Twitch.")); return;
        }
        m_UserId = json.value("user_id").toString();
        m_Validation->start();
        emit Connected(true);
        emit Status(tr("Connected as %1. Fetch a channel where this account is the broadcaster or a moderator. Session lasts until disconnect, window close, or token expiry.").arg(json.value("login").toString()));
    });
}
void AuTwitchConnection::Fetch(const QString& channel)
{
    if(m_Token.isEmpty() || m_UserId.isEmpty() || m_Busy) return;
    if(channel.trimmed().isEmpty()) { emit Status(tr("Enter a channel login first.")); return; }
    m_Busy = true; emit Busy(true);
    m_Names.clear(); m_Ids.clear(); m_Cursors.clear(); m_Pages = 0;
    emit Participants(QStringList());
    emit Status(tr("Fetching chat participants…"));
    QUrlQuery query; query.addQueryItem("login", channel.trimmed().toLower());
    Request("https://api.twitch.tv/helix/users?" + query.query(QUrl::FullyEncoded), QByteArray(), true, [this](int code, QJsonObject json) {
        if(code != 200) { m_Busy = false; emit Busy(false); if(code == 401) Disconnect(); emit Status(tr("Channel lookup failed (HTTP %1). Reconnect if authorization expired.").arg(code)); return; }
        QJsonArray users = json.value("data").toArray();
        if(users.isEmpty()) { m_Busy = false; emit Busy(false); emit Status(tr("Channel was not found.")); return; }
        Page(users.first().toObject().value("id").toString());
    });
}
void AuTwitchConnection::Page(const QString& broadcaster, const QString& cursor)
{
    QUrlQuery query;
    query.addQueryItem("broadcaster_id", broadcaster); query.addQueryItem("moderator_id", m_UserId);
    query.addQueryItem("first", "1000");
    if(!cursor.isEmpty()) query.addQueryItem("after", cursor);
    Request("https://api.twitch.tv/helix/chat/chatters?" + query.query(QUrl::FullyEncoded), QByteArray(), true,
            [this, broadcaster](int code, QJsonObject json) {
        if(code != 200) {
            m_Busy = false; emit Busy(false);
            if(code == 401) Disconnect();
            emit Status(code == 403 ? tr("This account must be the channel broadcaster or a moderator.") :
                        code == 429 ? tr("Twitch rate limit reached. Wait before fetching again.") : tr("Participant fetch failed (HTTP %1). Try reconnecting.").arg(code));
            return;
        }
        const QJsonArray data = json.value("data").toArray();
        for(const QJsonValue& value : data) {
            QJsonObject user = value.toObject(); QString id = user.value("user_id").toString();
            if(id.isEmpty() || m_Ids.contains(id)) continue;
            m_Ids.insert(id); m_Names.append(user.value("user_name").toString());
        }
        QString next = json.value("pagination").toObject().value("cursor").toString();
        ++m_Pages;
        if(!next.isEmpty() && m_Pages < 100 && !m_Cursors.contains(next)) { m_Cursors.insert(next); Page(broadcaster, next); return; }
        m_Busy = false; emit Busy(false);
        m_Names.sort(Qt::CaseInsensitive);
        emit Participants(m_Names);
        emit Status(next.isEmpty() ? tr("Fetched %1 chat participants.").arg(m_Names.size()) : tr("Fetched %1 participants; pagination limit reached (partial list).").arg(m_Names.size()));
    });
}
