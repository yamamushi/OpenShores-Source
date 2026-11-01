#ifndef AUTWITCHCONNECTION_H
#define AUTWITCHCONNECTION_H
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <functional>
class QNetworkAccessManager;
class QTimer;
class AuTwitchConnection : public QObject
{
    Q_OBJECT
public:
    explicit AuTwitchConnection(QObject* parent = 0, QNetworkAccessManager* network = 0);
    void Authorize(const QString& clientId);
    void Disconnect();
    void Fetch(const QString& channel);
signals:
    void Status(const QString& text);
    void Connected(bool connected);
    void Busy(bool busy);
    void Participants(const QStringList& names);
private:
    void Request(const QString& url, const QByteArray& body, bool authenticated,
                 std::function<void(int, QJsonObject)> done);
    void Poll();
    void Validate();
    void Page(const QString& broadcaster, const QString& cursor = QString());
    QNetworkAccessManager* m_Network;
    QTimer* m_Poll;
    QTimer* m_Validation;
    QString m_ClientId, m_DeviceCode, m_Token, m_UserId;
    qint64 m_Deadline;
    int m_Generation, m_Pages;
    bool m_Busy;
    QSet<QString> m_Ids, m_Cursors;
    QStringList m_Names;
};
#endif
