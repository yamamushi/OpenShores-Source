#include <QtTest>
#include <QTemporaryDir>
#include "../../src/lib/client/player/aunpcnamejournal.h"
#include "../../src/lib/designer/settings/autwitchnamepool.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QDesktopServices>
#include "../../src/lib/designer/settings/autwitchconnection.h"
#ifndef Q_MOC_RUN
struct Response { int code; QByteArray body; };
class Reply : public QNetworkReply {
    QByteArray data;
public:
    Reply(const QNetworkRequest& request, Response response, QObject* parent) : QNetworkReply(parent), data(response.body) {
        setRequest(request); setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.code);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(0, this, [this]() { setFinished(true); emit readyRead(); emit finished(); });
    }
    void abort() override {}
    qint64 bytesAvailable() const override { return data.size() + QNetworkReply::bytesAvailable(); }
    qint64 readData(char* dest, qint64 max) override {
        qint64 size = qMin(max, qint64(data.size())); if(!size) return -1;
        memcpy(dest, data.constData(), size); data.remove(0, size); return size;
    }
};
class Network : public QNetworkAccessManager {
public:
    QList<Response> responses;
    QList<QNetworkRequest> requests;
    QNetworkReply* createRequest(Operation, const QNetworkRequest& request, QIODevice*) override {
        requests.append(request);
        return new Reply(request, responses.isEmpty() ? Response{500, "{}"} : responses.takeFirst(), this);
    }
};
#endif
#include "connection_test.h"
void ConnectionTest::browser(const QUrl&) {}

void ConnectionTest::initTestCase() { QDesktopServices::setUrlHandler("https", this, "browser"); }

void ConnectionTest::paginationAndDisconnect() {
        Network net;
        net.responses = {{200, R"({"device_code":"device","verification_uri":"https://www.twitch.tv/activate","expires_in":300,"interval":5})"},
            {200, R"({"access_token":"test-token"})"},
            {200, R"({"client_id":"client","user_id":"1","login":"owner","scopes":["moderator:read:chatters"]})"},
            {200, R"({"data":[{"id":"1"}]})"},
            {200, R"({"data":[{"user_id":"2","user_name":"Bob"}],"pagination":{"cursor":"next"}})"},
            {200, R"({"data":[{"user_id":"2","user_name":"Bob"},{"user_id":"3","user_name":"Alice"}],"pagination":{}})"}};
        AuTwitchConnection connection(0, &net);
        QSignalSpy connected(&connection, &AuTwitchConnection::Connected);
        QSignalSpy participants(&connection, &AuTwitchConnection::Participants);
        connection.Authorize("client");
        QTRY_VERIFY_WITH_TIMEOUT(connected.size() >= 2, 8000);
        QVERIFY(connected.last().first().toBool());
        connection.Fetch("owner");
        QTRY_COMPARE(participants.last().first().toStringList().size(), 2);
        QCOMPARE(participants.last().first().toStringList(), (QStringList() << "Alice" << "Bob"));
        QCOMPARE(net.requests.last().rawHeader("Authorization"), QByteArray("Bearer test-token"));
        QVERIFY(net.requests.last().url().query().contains("after=next"));
        connection.Disconnect();
        QVERIFY(!connected.last().first().toBool());
        QVERIFY(participants.last().first().toStringList().isEmpty());
    }

void ConnectionTest::cancelledAuthorizationIgnoresReply() {
        Network net; net.responses = {{200, R"({"device_code":"device","verification_uri":"https://www.twitch.tv/activate"})"}};
        AuTwitchConnection connection(0, &net);
        QSignalSpy status(&connection, &AuTwitchConnection::Status);
        connection.Authorize("client"); connection.Disconnect();
        QTest::qWait(30);
        QCOMPARE(net.requests.size(), 1);
        QVERIFY(status.last().first().toString().startsWith("Disconnected"));
    }

void ConnectionTest::permissionFailure() {
        Network net;
        net.responses = {{200, R"({"device_code":"device","verification_uri":"https://www.twitch.tv/activate","expires_in":300})"},
            {200, R"({"access_token":"test-token"})"},
            {200, R"({"client_id":"client","user_id":"1","scopes":["moderator:read:chatters"]})"},
            {200, R"({"data":[{"id":"99"}]})"}, {403, "{}"}};
        AuTwitchConnection connection(0, &net);
        QSignalSpy connected(&connection, &AuTwitchConnection::Connected);
        QSignalSpy status(&connection, &AuTwitchConnection::Status);
        connection.Authorize("client");
        QTRY_VERIFY_WITH_TIMEOUT(connected.size() >= 2, 8000);
        connection.Fetch("other");
        QTRY_VERIFY(status.last().first().toString().contains("must be"));
    }

void ConnectionTest::missingScopeRejected() {
        Network net;
        net.responses = {{200, R"({"device_code":"device","verification_uri":"https://www.twitch.tv/activate","expires_in":300})"},
            {200, R"({"access_token":"test-token"})"},
            {200, R"({"client_id":"client","user_id":"1","scopes":[]})"}};
        AuTwitchConnection connection(0, &net);
        QSignalSpy status(&connection, &AuTwitchConnection::Status);
        connection.Authorize("client");
        QTRY_VERIFY_WITH_TIMEOUT(status.last().first().toString().contains("validation failed"), 8000);
    }

void ConnectionTest::authorizationFailure() {
        Network net; net.responses = {{400, "{}"}};
        AuTwitchConnection connection(0, &net);
        QSignalSpy status(&connection, &AuTwitchConnection::Status);
        connection.Authorize("bad");
        QTRY_VERIFY(status.last().first().toString().contains("HTTP 400"));
    }
void ConnectionTest::assignmentNamePool()
{
    QSet<QString> occupied; occupied.insert("alice");
    QCOMPARE(TwitchAvailableNames(QStringList() << "Alice" << "Bob" << "BOB" << "Carol", occupied),
             (QStringList() << "Bob" << "Carol"));
    QVERIFY(TwitchAvailableNames(QStringList(), occupied).isEmpty());
    QVERIFY(TwitchAvailableNames(QStringList() << "ALICE", occupied).isEmpty());
}
void ConnectionTest::renameHistoryPersistsAndConfirms()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("history.ini");
    const QString key = "NpcNaming/Submitted/test/online/42";
    {
        QSettings settings(file, QSettings::IniFormat);
        QVERIFY(!ObserveNpcNameHistory(settings, key, "Generated Name"));
        RecordNpcNameHistory(settings, key, "Alice", "twitch");
        QVERIFY(ObserveNpcNameHistory(settings, key, "Generated Name"));
        QCOMPARE(settings.value(key + "/State").toString(), QString("submitted"));
    }
    QSettings restored(file, QSettings::IniFormat);
    QVERIFY(ObserveNpcNameHistory(restored, key, "Alice"));
    QCOMPARE(restored.value(key + "/State").toString(), QString("confirmed"));
    QCOMPARE(restored.value(key + "/Source").toString(), QString("twitch"));
    QVERIFY(!restored.value(key + "/ConfirmedAt").toString().isEmpty());
    QVERIFY(ObserveNpcNameHistory(restored, key, "Changed elsewhere"));
    RecordNpcNameHistory(restored, key, "Bob", "manual");
    QCOMPARE(restored.value(key + "/State").toString(), QString("submitted"));
    QVERIFY(!restored.contains(key + "/ConfirmedAt"));
    restored.setValue(key + "/State", "protected");
    QVERIFY(ObserveNpcNameHistory(restored, key, "Bob"));
    QCOMPARE(restored.value(key + "/State").toString(), QString("protected"));
}
QTEST_GUILESS_MAIN(ConnectionTest)
