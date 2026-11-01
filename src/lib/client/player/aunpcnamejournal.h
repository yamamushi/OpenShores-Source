#ifndef AUNPCNAMEJOURNAL_H
#define AUNPCNAMEJOURNAL_H
#include <QSettings>
#include <QDateTime>
inline void RecordNpcNameHistory(QSettings& settings, const QString& key, const QString& name, const QString& source)
{
    settings.setValue(key, name);
    settings.setValue(key + "/State", "submitted");
    settings.setValue(key + "/Source", source);
    settings.setValue(key + "/SubmittedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    settings.remove(key + "/ConfirmedAt");
    settings.sync();
}
inline bool ObserveNpcNameHistory(QSettings& settings, const QString& key, const QString& currentName)
{
    const QString requested = settings.value(key).toString();
    if(requested.isEmpty()) return false;
    if(requested == currentName && settings.value(key + "/State").toString() == "submitted")
    {
        settings.setValue(key + "/State", "confirmed");
        settings.setValue(key + "/ConfirmedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        settings.sync();
    }
    return true;
}
#endif
