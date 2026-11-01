#ifndef AUNPCNAMEHISTORY_H
#define AUNPCNAMEHISTORY_H
#include <auglobal.h>
#include <auapp.h>
#include <auid.h>
#include <QUrl>
#include "aunpcnamejournal.h"
// Conservative local protection: submitted renames remain reserved even on rejection.
inline QString NpcNameHistoryKey(const AuId& id)
{
    return QString("NpcNaming/Submitted/%1/%2/%3")
        .arg(auApp->IsSingleUserMode() ? "local" : QString::fromLatin1(QUrl::toPercentEncoding(g_Au.SceneMasterName())))
        .arg(auApp->IsSingleUserMode() ? "local" : "online").arg(id.GetID());
}
inline void RecordNpcNameSubmission(const AuId& id, const QString& name, const QString& source = "manual")
{
    RecordNpcNameHistory(g_Au.Settings(), NpcNameHistoryKey(id), name, source);
}
inline bool NpcHasNameHistory(const AuId& id, const QString& currentName)
{
    return ObserveNpcNameHistory(g_Au.Settings(), NpcNameHistoryKey(id), currentName);
}
#endif
