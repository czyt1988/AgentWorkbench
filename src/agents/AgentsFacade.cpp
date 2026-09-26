#include "agents/AgentsFacade.h"

#include "agents/AgentHealthMonitor.h"
#include "agents/AgentModel.h"
#include "agents/AgentRepository.h"
#include "agents/AgentRuntime.h"
#include "agents/AgentScripts.h"
#include "agents/AgentStateStore.h"
#include "agents/AgentUrls.h"
#include "core/EnvExpander.h"
#include "core/Settings.h"
#include "theme/Theme.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QUrl>

namespace awb::agents {

namespace {

// "[app] …" operational log lines, byte-compatible with 0.3.0.
QString logPrefix(const QString &tag, const QString &operation, const QString &id)
{
    return id.isEmpty()
               ? QStringLiteral("[%1] %2: ").arg(tag, operation)
               : QStringLiteral("[%1] %2 \"%3\": ").arg(tag, operation, id);
}

void appLog(const QString &operation, const QString &id, const QString &message)
{
    qInfo().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

void appLogError(const QString &operation, const QString &id, const QString &message)
{
    qWarning().noquote() << logPrefix(QStringLiteral("app"), operation, id) + message;
}

// Build a definition from the field map the QML edit form submits. The id
// comes from the caller (generated or immutable), the icon is resolved so
// the model always holds a displayable URL.
AgentDefinition definitionFromFields(const QVariantMap &f, const QString &id)
{
    AgentDefinition a;
    a.id = id;
    a.name = f.value(QStringLiteral("name")).toString().trimmed();
    a.command = f.value(QStringLiteral("command")).toString().trimmed();
    a.webUrl = f.value(QStringLiteral("webUrl")).toString().trimmed();
    a.configDir = f.value(QStringLiteral("configDir")).toString().trimmed();
    a.icon = AgentRepository::resolveIcon(
        f.value(QStringLiteral("icon")).toString().trimmed());
    a.color = f.value(QStringLiteral("color")).toString().trimmed();
    a.cardColor = f.value(QStringLiteral("cardColor")).toString().trimmed();
    a.installCommand =
        f.value(QStringLiteral("installCommand")).toString().trimmed();
    a.updateCommand = f.value(QStringLiteral("updateCommand")).toString().trimmed();
    a.versionCommand =
        f.value(QStringLiteral("versionCommand")).toString().trimmed();
    a.setupCommand = f.value(QStringLiteral("setupCommand")).toString().trimmed();
    a.tokenFile = f.value(QStringLiteral("tokenFile")).toString().trimmed();
    return a;
}

} // namespace

AgentsFacade::AgentsFacade(core::Settings *settings, const QString &dataRoot,
                           theme::Theme *theme, QObject *parent)
    : QObject(parent)
    , m_repo(new AgentRepository(dataRoot))
    , m_stateStore(new AgentStateStore(dataRoot))
    , m_model(new AgentModel(this))
    , m_runtime(new AgentRuntime(m_model, this))
    , m_scripts(new AgentScripts(m_model, m_stateStore, this))
    , m_health(new AgentHealthMonitor(
          m_model, settings->launcherOptions().healthCheckIntervalMs, this))
{
    // Configuration: built-ins from the shipped default, user agents on top.
    // Auto-assignment colors come from the current theme (specs/03 S3-T1).
    if (theme)
        m_repo->setAgentPalette(theme->agentPalette());
    m_repo->load();
    m_model->setDefinitions(m_repo->definitions());

    // Runtime failures bubble up to QML under the 0.3.0 signal names.
    connect(m_runtime, &AgentRuntime::launchFailed, this, &AgentsFacade::launchFailed);
    connect(m_scripts, &AgentScripts::launchFailed, this, &AgentsFacade::launchFailed);
    connect(m_scripts, &AgentScripts::installFinished, this,
            &AgentsFacade::installFinished);
    connect(m_runtime, &AgentRuntime::recheckRequested, m_health,
            &AgentHealthMonitor::recheckNow);

    // Health transitions: clear the launching spinner once the server is up,
    // report only real transitions in the log, and update the model.
    connect(m_health, &AgentHealthMonitor::runningChanged, this,
            [this](const QString &id, bool up) {
                if (up)
                    m_model->setLaunching(id, false);
                const int row = m_model->indexOf(id);
                const bool wasRunning =
                    row >= 0 && m_model->state(id).running;
                if (up != wasRunning) {
                    appLog(QStringLiteral("state"), id,
                           up ? QStringLiteral("stopped → running")
                              : QStringLiteral("running → stopped"));
                }
                m_model->setRunning(id, up);
            });

    // A successful one-time setup proceeds with the actual launch.
    connect(m_scripts, &AgentScripts::setupFinished, this,
            [this](const QString &id, bool ok) {
                if (ok)
                    launch(id);
            });

}

QAbstractItemModel *AgentsFacade::model() const
{
    return m_model;
}

void AgentsFacade::start()
{
    appLog(QStringLiteral("start"), QString(),
           QStringLiteral("%1 launcher(s) configured, config: %2")
               .arg(m_model->definitions().size())
               .arg(configFilePath()));

    // Apply the persisted setup state to the cards.
    m_stateStore->load();
    for (const AgentDefinition &d : m_model->definitions())
        m_model->setSetupDone(d.id, m_stateStore->isSetupDone(d.id));

    // Mark every agent with a versionCommand as "checking" before any QML
    // paint so the spinner is visible from the first frame, even if the
    // version process finishes before the first render.
    for (const AgentDefinition &d : m_model->definitions()) {
        if (!d.versionCommand.isEmpty())
            m_model->setCheckingVersion(d.id, true);
    }

    m_health->start();
    m_scripts->checkVersions();
}

// --- Launch / stop -----------------------------------------------------------

void AgentsFacade::launch(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const AgentDefinition def = m_model->definitions().at(row);

    // If the agent has a one-time setup command that hasn't been run yet,
    // run it first; setupFinished(ok) calls back into launch().
    if (!def.setupCommand.isEmpty() && !m_stateStore->isSetupDone(id)) {
        m_scripts->runSetup(id);
        return;
    }

    m_runtime->launch(def, AgentUrls::tokenValue(def.tokenFile));
}

bool AgentsFacade::stop(const QString &id)
{
    return m_runtime->stop(id);
}

void AgentsFacade::forceStop(const QString &id)
{
    m_runtime->forceStop(id);
}

void AgentsFacade::openWeb(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    const QString url = AgentUrls::finalUrl(m_model->definitions().at(row));
    if (url.isEmpty())
        return;

    // The #token=… fragment must never reach the log, so strip it first.
    QString loggedUrl = url;
    const int fragment = loggedUrl.indexOf(QLatin1Char('#'));
    if (fragment >= 0)
        loggedUrl.truncate(fragment);

    if (QDesktopServices::openUrl(QUrl(url))) {
        appLog(QStringLiteral("openWeb"), id,
               QStringLiteral("opened %1").arg(loggedUrl));
    } else {
        appLogError(QStringLiteral("openWeb"), id,
                    QStringLiteral("failed to open %1 — no handler accepted "
                                   "the URL")
                        .arg(loggedUrl));
    }
}

void AgentsFacade::openConfigDir(const QString &id)
{
    const int row = m_model->indexOf(id);
    if (row < 0)
        return;
    QString dir = core::EnvExpander::expand(
        m_model->definitions().at(row).configDir);
    if (dir.isEmpty()) {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("no config directory configured"));
        return;
    }
    dir = QDir::fromNativeSeparators(dir);
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) {
        appLog(QStringLiteral("openConfigDir"), id,
               QStringLiteral("opened %1").arg(dir));
    } else {
        appLogError(QStringLiteral("openConfigDir"), id,
                    QStringLiteral("failed to open %1").arg(dir));
    }
}

bool AgentsFacade::hasLaunchedAgents() const
{
    return m_runtime->hasLaunchedAgents();
}

int AgentsFacade::stopAll()
{
    return m_runtime->stopAll();
}

// --- Scripts passthrough -----------------------------------------------------

void AgentsFacade::install(const QString &id)
{
    m_scripts->install(id);
}

void AgentsFacade::updateTool(const QString &id)
{
    m_scripts->update(id);
}

void AgentsFacade::resetSetup(const QString &id)
{
    m_model->setSetupDone(id, false);

    // Remove from agent_state.json; a missing file means nothing to clear
    // (silent, like 0.3.0).
    if (!QFile::exists(m_stateStore->stateFilePath()))
        return;

    if (m_stateStore->reset(id)) {
        appLog(QStringLiteral("setup"), id,
               QStringLiteral("re-initialized, the setup command runs again "
                              "before the next start"));
    } else {
        appLogError(QStringLiteral("setup"), id,
                    QStringLiteral("cannot write %1 while clearing the setup "
                                   "state")
                        .arg(m_stateStore->stateFilePath()));
    }
}

// --- CRUD (Settings page) ----------------------------------------------------

bool AgentsFacade::addAgent(const QVariantMap &fields)
{
    QString id = fields.value(QStringLiteral("id")).toString().trimmed();
    if (id.isEmpty()) {
        // Generate from the display name, uniquified with -2, -3, ... suffixes.
        const QString base = AgentRepository::slugFromName(
            fields.value(QStringLiteral("name")).toString());
        id = base;
        int n = 2;
        while (m_model->indexOf(id) >= 0)
            id = base + QLatin1Char('-') + QString::number(n++);
    } else if (m_model->indexOf(id) >= 0) {
        return false; // duplicate id (the form prevents this; defensive)
    }

    AgentDefinition a = definitionFromFields(fields, id);
    // Empty color would render a broken card until the next restart
    // (load() assigns palette colors); assign one now.
    if (a.color.isEmpty())
        a.color = AgentRepository::paletteColorAt(m_model->definitions().size());

    m_model->insertAgent(m_model->definitions().size(), a);
    return saveConfig();
}

bool AgentsFacade::updateAgentFull(const QString &id, const QVariantMap &fields)
{
    if (m_model->indexOf(id) < 0)
        return false;

    AgentDefinition a = definitionFromFields(fields, id); // id is immutable
    if (a.color.isEmpty())
        a.color = AgentRepository::paletteColorAt(m_model->indexOf(id));

    // Runtime state is keyed separately and untouched by a definition swap.
    m_model->replaceDefinition(a);
    return saveConfig();
}

bool AgentsFacade::removeAgent(const QString &id)
{
    if (!m_model->removeAgentById(id))
        return false;

    // Record deleted built-ins: built-in agents are re-applied from the
    // shipped default on every start, so the id has to be remembered here to
    // keep this one deleted.
    if (m_repo->isDefaultAgent(id) && !m_repo->removedIds().contains(id))
        m_repo->setRemovedIds(m_repo->removedIds() + QStringList{id});

    // The process itself keeps running on purpose (documented in the UI).
    m_runtime->forget(id);
    return saveConfig();
}

bool AgentsFacade::restoreDefaults()
{
    // Forget the deletions and re-apply the shipped built-in list; agents
    // the user added themselves are kept. The model swap preserves runtime
    // state per id, so a running card stays running.
    if (!m_repo->restoreDefaults(m_model->definitions())) {
        appLogError(QStringLiteral("config"), QString(),
                    QStringLiteral("failed to write %1").arg(configFilePath()));
        return false;
    }
    m_model->setDefinitions(m_repo->definitions());
    appLog(QStringLiteral("config"), QString(),
           QStringLiteral("saved %1").arg(configFilePath()));
    return true;
}

bool AgentsFacade::isDefaultAgent(const QString &id) const
{
    return m_repo->isDefaultAgent(id);
}

QString AgentsFacade::configFilePath() const
{
    return m_repo->configFilePath();
}

bool AgentsFacade::saveConfig()
{
    m_repo->setDefinitions(m_model->definitions());
    const bool ok = m_repo->save();
    if (ok) {
        appLog(QStringLiteral("config"), QString(),
               QStringLiteral("saved %1").arg(configFilePath()));
    } else {
        appLogError(QStringLiteral("config"), QString(),
                    QStringLiteral("failed to write %1").arg(configFilePath()));
    }
    return ok;
}

} // namespace awb::agents
