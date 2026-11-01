#include <QObject>
#include <QUrl>
class ConnectionTest : public QObject {
    Q_OBJECT
public slots:
    void browser(const QUrl&);
private slots:
    void initTestCase();
    void paginationAndDisconnect();
    void cancelledAuthorizationIgnoresReply();
    void permissionFailure();
    void missingScopeRejected();
    void authorizationFailure();
    void assignmentNamePool();
    void renameHistoryPersistsAndConfirms();
};
