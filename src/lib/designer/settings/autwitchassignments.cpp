#include "autwitchassignments.h"
#include "autwitchnamepool.h"
#include <auapp.h>
#include <auframe.h>
#include <auview.h>
#include <auglobal.h>
#include <aunpcnamehistory.h>
#include <daatomlist.h>
#include <danpc.h>
#include <daperson.h>
#include <davessel.h>
#include <QCheckBox>
#include <QComboBox>
#include <QSignalBlocker>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>

namespace {
struct Candidate { AuId id; QString name; };
QList<Candidate> Candidates(const QString& scope, QSet<QString>& occupied)
{
    QList<Candidate> result;
    AuFrame* frame = auApp->Frame();
    AuView* view = frame ? frame->View() : 0;
    if(!view) return result;
    DaPerson* person = 0;
    view->LockSceneAtom(&person);
    if(person && person->EmpireId())
    {
        DaAtom* env = person->FindEnvironmentAtom();
        DaAtomList npcs;
        if(env) env->FindNpcsRecursive(npcs);
        for(DaAtom* atom : npcs)
        {
            DaNpc* npc = dynamic_cast<DaNpc*>(atom);
            if(!npc) continue;
            occupied.insert(npc->AssignedName().toCaseFolded());
            DaVessel* ship = npc->FindParentVesselInEnv();
            if(!ship || npc->EmpireId() != person->EmpireId() || !ship->InChainOfCommand(person) || npc->CurrentHitPoints() <= 0) continue;
            if(scope == "crew" && npc->NpcRole() != ROLE_Crew) continue;
            if(scope == "crew-officers" && npc->NpcRole() != ROLE_Crew && npc->NpcRole() != ROLE_Officer) continue;
            if(NpcHasNameHistory(npc->Id(), npc->AssignedName())) continue;
            result.append(Candidate{npc->Id(), npc->AssignedName()});
        }
    }
    view->UnlockSceneAtom();
    std::sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) { return a.id.GetID() < b.id.GetID(); });
    return result;
}
}
AuTwitchAssignments::AuTwitchAssignments(QWidget* parent) : QWidget(parent), m_Revision(0), m_Scope("crew")
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    QPushButton* preview = new QPushButton(tr("Preview manual name assignments"), this);
    layout->addWidget(preview);
    m_Table = new QTableWidget(0, 3, this);
    m_Table->setHorizontalHeaderLabels(QStringList() << tr("Assign / NPC ID") << tr("Current name") << tr("Twitch name"));
    m_Table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_Table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_Table);
    QPushButton* submit = new QPushButton(tr("Submit selected assignments"), this);
    layout->addWidget(submit);
    QPushButton* protect = new QPushButton(tr("Protect selected NPCs as already renamed"), this);
    layout->addWidget(protect);
    connect(protect, &QPushButton::clicked, this, [this]() { Protect(); });
    m_Status = new QLabel(tr("Preview assignments, then check the NPCs you want to rename. Previously submitted names from this client are protected. Earlier renames and renames from other clients cannot yet be identified; review current names before selecting."), this);
    m_Status->setWordWrap(true);
    layout->addWidget(m_Status);
    connect(preview, &QPushButton::clicked, this, [this]() { Preview(); });
    connect(submit, &QPushButton::clicked, this, [this]() { Submit(); });
}
void AuTwitchAssignments::SetParticipants(const QStringList& names)
{
    ++m_Revision;
    m_Names = names;
    m_Table->setRowCount(0);
}
void AuTwitchAssignments::SetScope(const QString& scope)
{
    ++m_Revision;
    m_Scope = scope;
    m_Table->setRowCount(0);
}
void AuTwitchAssignments::Preview()
{
    ++m_Revision;
    m_Table->setRowCount(0);
    QSet<QString> occupied;
    QList<Candidate> npcs = Candidates(m_Scope, occupied);
    const QString zeroKey = NpcNameHistoryKey(AuId());
    const QString prefix = zeroKey.left(zeroKey.lastIndexOf('/') + 1);
    for(const QString& key : g_Au.Settings().allKeys())
        if(key.startsWith(prefix) && !key.mid(prefix.size()).contains('/')) occupied.insert(g_Au.Settings().readEntry(key).toCaseFolded());
    QStringList valid;
    for(const QString& name : m_Names) if(IsNameValid(name)) valid.append(name);
    QStringList names = TwitchAvailableNames(valid, occupied);
    m_Table->setProperty("server", NpcNameHistoryKey(AuId()));
    int count = qMin(50, npcs.size());
    for(int row = 0; row < count; ++row)
    {
        m_Table->insertRow(row);
        QTableWidgetItem* item = new QTableWidgetItem(QString::number(npcs[row].id.GetID()));
        item->setCheckState(Qt::Unchecked);
        m_Table->setItem(row, 0, item);
        m_Table->setItem(row, 1, new QTableWidgetItem(npcs[row].name));
        QComboBox* choice = new QComboBox(m_Table);
        choice->addItem(tr("Do not assign"), QString());
        for(const QString& name : names) choice->addItem(name, name);
        choice->setCurrentIndex(row < names.size() ? row + 1 : 0);
        choice->setProperty("previousIndex", choice->currentIndex());
        choice->setToolTip(tr("Choose a Twitch participant for this NPC. Choosing a name used by another row swaps the two assignments."));
        m_Table->setCellWidget(row, 2, choice);
        connect(choice, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, choice](int index) {
            const int previous = choice->property("previousIndex").toInt();
            if(index > 0)
                for(int otherRow = 0; otherRow < m_Table->rowCount(); ++otherRow)
                {
                    QComboBox* other = qobject_cast<QComboBox*>(m_Table->cellWidget(otherRow, 2));
                    if(other && other != choice && other->currentIndex() == index)
                    {
                        QSignalBlocker blocker(other);
                        other->setCurrentIndex(previous);
                        other->setProperty("previousIndex", previous);
                        break;
                    }
                }
            choice->setProperty("previousIndex", index);
        });
    }
    m_Status->setText(tr("%1 proposed assignments (up to 50 per preview). Choose Twitch names in the dropdowns (names swap between rows), then select NPCs. Existing renames from other clients cannot yet be detected.").arg(count));
}
void AuTwitchAssignments::Submit()
{
    if(m_Table->property("server").toString() != NpcNameHistoryKey(AuId()))
    {
        m_Table->setRowCount(0);
        m_Status->setText(tr("Game connection changed. Preview assignments again."));
        return;
    }
    QSet<QString> selectedNames;
    for(int row = 0; row < m_Table->rowCount(); ++row)
    {
        if(m_Table->item(row, 0)->checkState() != Qt::Checked) continue;
        QComboBox* choice = qobject_cast<QComboBox*>(m_Table->cellWidget(row, 2));
        const QString name = choice ? choice->currentData().toString() : QString();
        if(name.isEmpty()) continue;
        if(!IsNameValid(name) || selectedNames.contains(name.toCaseFolded()))
        {
            m_Status->setText(tr("Choose a different valid Twitch name for each selected NPC before submitting."));
            return;
        }
        selectedNames.insert(name.toCaseFolded());
    }
    if(selectedNames.isEmpty())
    {
        m_Status->setText(tr("Select at least one NPC and choose its Twitch name before submitting."));
        return;
    }
    const int revision = ++m_Revision;
    const QString server = NpcNameHistoryKey(AuId());
    int queued = 0;
    for(int row = 0; row < m_Table->rowCount(); ++row)
    {
        if(m_Table->item(row, 0)->checkState() != Qt::Checked) continue;
        const AuId id(m_Table->item(row, 0)->text());
        const QString oldName = m_Table->item(row, 1)->text();
        QComboBox* choice = qobject_cast<QComboBox*>(m_Table->cellWidget(row, 2));
        const QString name = choice ? choice->currentData().toString() : QString();
        if(name.isEmpty()) continue;
        QTimer::singleShot(queued * 500, this, [this, revision, server, id, oldName, name]() {
            if(revision != m_Revision || !isEnabled() || server != NpcNameHistoryKey(AuId())) return;
            QSet<QString> occupied;
            QList<Candidate> current = Candidates(m_Scope, occupied);
            bool found = false;
            for(const Candidate& npc : current) if(npc.id == id && npc.name == oldName) found = true;
            const QString prefix = server.left(server.lastIndexOf('/') + 1);
            for(const QString& key : g_Au.Settings().allKeys())
                if(key.startsWith(prefix) && !key.mid(prefix.size()).contains('/')) occupied.insert(g_Au.Settings().readEntry(key).toCaseFolded());
            if(!found || occupied.contains(name.toCaseFolded()))
            {
                m_Status->setText(tr("Skipped a changed, renamed or unavailable NPC. Check the game log for other request results."));
                return;
            }
            auApp->NpcRename(id, name);
            g_Au.Settings().writeEntry(NpcNameHistoryKey(id) + "/Source", QString("twitch"));
            m_Status->setText(tr("Submitted name %1. Check the game log for the server result. Submitted NPCs stay protected locally even if rejected.").arg(name));
        });
        ++queued;
    }
    m_Table->setRowCount(0);
    m_Status->setText(tr("Queued %1 assignments, one request per half second. Changing mode, scope or participants cancels remaining requests. Check the game log for server results.").arg(queued));
}

void AuTwitchAssignments::Protect()
{
    ++m_Revision;
    if(m_Table->property("server").toString() != NpcNameHistoryKey(AuId()))
    {
        m_Table->setRowCount(0);
        m_Status->setText(tr("Game connection changed. Preview again."));
        return;
    }
    QSet<QString> occupied;
    const QList<Candidate> current = Candidates(m_Scope, occupied);
    int protectedCount = 0;
    for(int row = 0; row < m_Table->rowCount(); ++row)
    {
        if(m_Table->item(row, 0)->checkState() != Qt::Checked) continue;
        const AuId id(m_Table->item(row, 0)->text());
        for(const Candidate& npc : current)
            if(npc.id == id && npc.name == m_Table->item(row, 1)->text())
            {
                RecordNpcNameSubmission(id, npc.name, "protected");
                g_Au.Settings().writeEntry(NpcNameHistoryKey(id) + "/State", QString("protected"));
                ++protectedCount;
            }
    }
    m_Table->setRowCount(0);
    m_Status->setText(tr("Marked %1 NPCs as already renamed in local history. No rename requests were sent.").arg(protectedCount));
}
