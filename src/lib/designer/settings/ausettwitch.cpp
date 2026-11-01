#include "ausettwitch.h"
#include "autwitchconnection.h"
#include "autwitchassignments.h"
#include <QPushButton>
#include <QListWidget>
#include <auglobal.h>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

AuSetTwitch::AuSetTwitch(QWidget* parent) : QWidget(parent)
{
    AuSettings& settings = g_Au.Settings();
    QVBoxLayout* layout = new QVBoxLayout(this);
    QLabel* description = new QLabel(tr("Use Twitch chat participants as NPC names while a channel is live."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    QCheckBox* enabled = new QCheckBox(tr("Enable Twitch integration"), this);
    enabled->setChecked(settings.readBoolEntry("Twitch/Enabled", false));
    layout->addWidget(enabled);

    QWidget* options = new QWidget(this);
    QFormLayout* form = new QFormLayout(options);
    QLineEdit* clientId = new QLineEdit(settings.readEntry("Twitch/ClientId"), options);
    clientId->setPlaceholderText(tr("Twitch application client ID"));
    clientId->setMaxLength(128);
    clientId->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9]*"), clientId));
    form->addRow(tr("Client ID"), clientId);

    QLineEdit* channel = new QLineEdit(settings.readEntry("Twitch/Channel"), options);
    channel->setPlaceholderText(tr("Channel login, e.g. twitchdev"));
    channel->setMaxLength(25);
    channel->setValidator(new QRegularExpressionValidator(QRegularExpression("[A-Za-z0-9_]*"), channel));
    channel->setToolTip(tr("Enter a channel login without a URL or @ prefix."));
    form->addRow(tr("Channel"), channel);

    QComboBox* scope = new QComboBox(options);
    scope->addItem(tr("Crew only"), "crew");
    scope->addItem(tr("Crew and officers"), "crew-officers");
    scope->addItem(tr("All eligible NPCs"), "all");
    int index = scope->findData(settings.readEntry("Twitch/NpcScope", "crew"));
    scope->setCurrentIndex(index < 0 ? 0 : index);
    form->addRow(tr("NPC names"), scope);
    QComboBox* assignmentMode = new QComboBox(options);
    assignmentMode->addItem(tr("Manual name assignment"), "manual");
    assignmentMode->addItem(tr("Auto name assignment"), "auto");
    assignmentMode->setCurrentIndex(settings.readEntry("Twitch/AssignmentMode", "manual") == "auto" ? 1 : 0);
    form->addRow(tr("Assignment mode"), assignmentMode);
    QLabel* modeStatus = new QLabel(options);
    modeStatus->setWordWrap(true);
    form->addRow(modeStatus);
    QLabel* authorizationHelp = new QLabel(tr("Register a Public application in the Twitch developer console and enter its client ID. Connect opens Twitch in your browser; sign in as the broadcaster or a channel moderator. No client secret is needed."), options);
    authorizationHelp->setWordWrap(true);
    form->addRow(authorizationHelp);
    QPushButton* connectButton = new QPushButton(tr("Connect Twitch account"), options);
    QPushButton* disconnectButton = new QPushButton(tr("Disconnect / cancel"), options);
    QPushButton* fetchButton = new QPushButton(tr("Fetch chat participants"), options);
    fetchButton->setEnabled(false);
    form->addRow(connectButton);
    form->addRow(disconnectButton);
    form->addRow(fetchButton);
    layout->addWidget(options);
    options->setEnabled(enabled->isChecked());

    QLabel* status = new QLabel(tr("Disconnected. Authorization is kept in memory for this settings window. Fetch participants to preview manual assignments."), this);
    status->setWordWrap(true);
    layout->addWidget(status);
    status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QListWidget* participants = new QListWidget(this);
    layout->addWidget(participants, 1);
    AuTwitchAssignments* assignments = new AuTwitchAssignments(this);
    assignments->SetScope(scope->currentData().toString());
    layout->addWidget(assignments, 1);
    auto updateMode = [assignmentMode, modeStatus, assignments, enabled, scope]() {
        const bool automatic = assignmentMode->currentData().toString() == "auto";
        assignments->SetScope(scope->currentData().toString());
        assignments->setEnabled(enabled->isChecked() && !automatic);
        modeStatus->setText(automatic ? tr("Automatic mode is saved. Background assignment is not available yet; no names will be submitted in this mode.") : tr("Manual mode: preview assignments and select NPCs before submitting."));
    };
    updateMode();
    connect(assignmentMode, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [assignmentMode, updateMode](int) {
        g_Au.Settings().writeEntry("Twitch/AssignmentMode", assignmentMode->currentData().toString());
        updateMode();
    });
    connect(enabled, &QCheckBox::toggled, this, [updateMode](bool) { updateMode(); });
    connect(scope, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [assignments, scope](int) { assignments->SetScope(scope->currentData().toString()); });
    AuTwitchConnection* connection = new AuTwitchConnection(this);
    connect(connection, &AuTwitchConnection::Participants, this, [assignments](const QStringList& names) { assignments->SetParticipants(names); });
    connect(connection, &AuTwitchConnection::Status, status, &QLabel::setText);
    connect(connection, &AuTwitchConnection::Connected, this, [fetchButton](bool connected) {
        fetchButton->setEnabled(connected);
    });
    connect(connection, &AuTwitchConnection::Busy, this, [channel](bool busy) { channel->setReadOnly(busy); });
    connect(connection, &AuTwitchConnection::Participants, this, [participants](const QStringList& names) {
        participants->clear(); participants->addItems(names);
    });
    connect(connectButton, &QPushButton::clicked, this, [connection, clientId]() { connection->Authorize(clientId->text()); });
    connect(disconnectButton, &QPushButton::clicked, connection, &AuTwitchConnection::Disconnect);
    connect(fetchButton, &QPushButton::clicked, this, [connection, channel]() { connection->Fetch(channel->text()); });
    connect(clientId, &QLineEdit::textEdited, connection, &AuTwitchConnection::Disconnect);
    connect(channel, &QLineEdit::textEdited, this, [participants, assignments]() { participants->clear(); assignments->SetParticipants(QStringList()); });
    connect(enabled, &QCheckBox::toggled, this, [connection](bool checked) { if(!checked) connection->Disconnect(); });

    connect(enabled, &QCheckBox::toggled, this, [options](bool checked) {
        options->setEnabled(checked);
        g_Au.Settings().writeEntry("Twitch/Enabled", checked);
    });
    connect(clientId, &QLineEdit::textChanged, this, [](const QString& text) {
        g_Au.Settings().writeEntry("Twitch/ClientId", text);
    });
    connect(channel, &QLineEdit::textChanged, this, [](const QString& text) {
        g_Au.Settings().writeEntry("Twitch/Channel", text.toLower());
    });
    connect(scope, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [scope](int) {
        g_Au.Settings().writeEntry("Twitch/NpcScope", scope->currentData().toString());
    });
}
