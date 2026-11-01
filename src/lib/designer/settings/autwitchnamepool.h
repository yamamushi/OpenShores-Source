#ifndef AUTWITCHNAMEPOOL_H
#define AUTWITCHNAMEPOOL_H
#include <QStringList>
#include <QSet>
// Caller validates game-specific name rules. Never propose an occupied name twice.
inline QStringList TwitchAvailableNames(const QStringList& names, QSet<QString> occupied)
{
    QStringList available;
    for(const QString& name : names)
        if(!occupied.contains(name.toCaseFolded()))
        {
            available.append(name);
            occupied.insert(name.toCaseFolded());
        }
    return available;
}
#endif
