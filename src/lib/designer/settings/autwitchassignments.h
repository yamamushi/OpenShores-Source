#ifndef AUTWITCHASSIGNMENTS_H
#define AUTWITCHASSIGNMENTS_H
#include <QWidget>
#include <QStringList>
class QTableWidget;
class QLabel;
class AuTwitchAssignments : public QWidget
{
public:
    explicit AuTwitchAssignments(QWidget* parent = 0);
    void SetParticipants(const QStringList& names);
    void SetScope(const QString& scope);
private:
    void Preview();
    void Submit();
    void Protect();
    int m_Revision;
    QStringList m_Names;
    QString m_Scope;
    QTableWidget* m_Table;
    QLabel* m_Status;
};
#endif
