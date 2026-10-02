#include "Launchers.h"

#include "CompatTools.h"
#include "SteamLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Launchers", text);
}

// The two launchers whose runners are read off the machine rather than set
// up here: every wine installed, and every Proton build under Steam.
const char *kWineId = "wine";
const char *kProtonId = "proton";

// Prefixed, so that a discovered runner's id can never collide with one
// minted for a runner someone added by hand.
const char *kWinePrefix = "wine:";
const char *kProtonPrefix = "proton:";

// Where an emulator keeps its cores when no folder has been given: a
// libretro install puts them in a `cores` directory beside the binary. An
// alias is asked what it stands for, which is the one thing only the shell
// knows.
QString coresDirFor(const Launcher &launcher, const LauncherRunner &runner) {
    if (!launcher.coresDir.isEmpty()) return launcher.coresDir;

    // Beside the runner's own binary, not some other install's: two
    // RetroArch installs keep their cores in their own folders.
    if (runner.command.startsWith(QLatin1Char('/'))) {
        const QString cores = QFileInfo(runner.command).absolutePath() + QStringLiteral("/cores");
        if (QDir(cores).exists()) return cores;
    }

    QString binary = QStandardPaths::findExecutable(runner.command);
    if (binary.isEmpty() && runner.useShell) {
        QProcess probe;
        probe.start(QStringLiteral("bash"),
                    {QStringLiteral("-ic"), QStringLiteral("alias %1").arg(runner.command)});
        if (probe.waitForFinished(3000)) {
            const QString definition = QString::fromLocal8Bit(probe.readAllStandardOutput());
            static const QRegularExpression path(QStringLiteral("(/[^\\s'\"]+)"));
            const QRegularExpressionMatch match = path.match(definition);
            if (match.hasMatch()) binary = match.captured(1);
        }
    }
    if (binary.isEmpty()) return {};

    const QString cores = QFileInfo(binary).absolutePath() + QStringLiteral("/cores");
    return QDir(cores).exists() ? cores : QString();
}

// The core to play this file with, as a full path: whatever the launcher's
// table says the file type takes. Empty when it does not say, which leaves
// the program to start without one.
QString coreFor(const Launcher &launcher, const QString &path, const LauncherRunner &runner) {
    if (!launcher.usesCores) return {};

    QString core;
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const auto &entry : launcher.cores) {
        if (entry.first.compare(suffix, Qt::CaseInsensitive) == 0) {
            core = entry.second;
            break;
        }
    }
    if (core.isEmpty()) return {};

    // A core given as a path of its own is taken as it stands.
    if (core.startsWith(QLatin1Char('/'))) return QFile::exists(core) ? core : QString();

    const QString dir = coresDirFor(launcher, runner);
    if (dir.isEmpty()) return {};
    const QString full = QDir(dir).filePath(core);
    return QFile::exists(full) ? full : QString();
}

QStringList expand(const QString &arguments, const QString &file, const QString &core,
                   const QString &appId) {
    QStringList out;
    // Split first and substitute after, so that a path with spaces in it
    // stays one argument however the template was written.
    for (const QString &original : QProcess::splitCommand(arguments)) {
        QString piece = original;
        piece.replace(QLatin1String("{file}"), file);
        piece.replace(QLatin1String("{dir}"), QFileInfo(file).absolutePath());
        piece.replace(QLatin1String("{core}"), core);
        piece.replace(QLatin1String("{appid}"), appId);

        // A placeholder that stood for nothing leaves an empty argument, and
        // an empty argument is not nothing: it is an argument. Out it goes,
        // and the flag introducing it goes with it, so "-L {core} {file}" is
        // simply the file when no core was found.
        if (piece.isEmpty() && original.contains(QLatin1Char('{'))) {
            if (!out.isEmpty() && out.last().startsWith(QLatin1Char('-'))) out.removeLast();
            continue;
        }
        out << piece;
    }
    return out;
}

QVector<Launcher> *loaded() {
    static QVector<Launcher> launchers;
    return &launchers;
}

// An item saved before wine and Proton were told apart named a wine profile,
// and that profile is where its Proton build was recorded. This maps the one
// to the other so such an item keeps running through the build it was set up
// with rather than quietly dropping back to plain wine.
QHash<QString, QString> *legacyProtonProfiles() {
    static QHash<QString, QString> map;
    return &map;
}

LauncherRunner makeRunner(const QString &id, const QString &name, const QString &command,
                          bool useShell = false) {
    LauncherRunner runner;
    runner.id = id;
    runner.name = name;
    runner.command = command;
    runner.useShell = useShell;
    return runner;
}

// The wines and the Proton builds on the machine, as runners. Read off it
// every session rather than saved, so one installed since last time is simply
// there and one uninstalled is simply gone.
void appendDiscoveredRunners(Launcher &launcher) {
    if (launcher.id == QLatin1String(kWineId)) {
        for (const WineBuild &build : installedWineBuilds()) {
            LauncherRunner runner =
                makeRunner(QLatin1String(kWinePrefix) + build.id, build.displayName,
                           build.command);
            runner.discovered = true;
            launcher.runners.append(runner);
        }
        return;
    }
    if (launcher.id != QLatin1String(kProtonId)) return;
    for (const CompatTool &tool : installedCompatTools()) {
        LauncherRunner runner;
        runner.id = QLatin1String(kProtonPrefix) + tool.id;
        runner.name = tool.displayName;
        runner.compatToolId = tool.id;
        runner.discovered = true;
        // Shown so the list reads as programs; it is protonInvocation that
        // actually starts one of these.
        runner.command = tool.protonPath;
        launcher.runners.append(runner);
    }
}

void load() {
    QVector<Launcher> result = defaultLaunchers();
    legacyProtonProfiles()->clear();

    QSettings settings;
    for (Launcher &launcher : result) {
        settings.beginGroup(QStringLiteral("launchers/") + launcher.id);
        if (!settings.contains(QStringLiteral("extensions"))) {
            settings.endGroup();
            continue; // never edited: keep the launcher as it shipped
        }

        launcher.extensions =
            settings.value(QStringLiteral("extensions"), launcher.extensions).toStringList();
        if (launcher.usesPrefixes) {
            launcher.prefixesDir = settings.value(QStringLiteral("prefixesDir")).toString();
        }
        if (launcher.usesSteamLibraries) {
            launcher.libraryFolders =
                settings.value(QStringLiteral("libraryFolders")).toStringList();
        }
        if (launcher.usesCores) {
            launcher.coresDir = settings.value(QStringLiteral("coresDir")).toString();
            QVector<QPair<QString, QString>> cores;
            for (const QString &pair : settings.value(QStringLiteral("cores")).toStringList()) {
                const int split = pair.indexOf(QLatin1Char('='));
                if (split <= 0) continue;
                cores.append({pair.left(split), pair.mid(split + 1)});
            }
            // A table emptied on purpose stays empty; one that was never
            // written at all stays the one the launcher shipped with.
            if (settings.contains(QStringLiteral("cores"))) launcher.cores = cores;
        }

        // The runners someone added by hand. The discovered ones are not in
        // here and are put back afterwards.
        if (settings.contains(QStringLiteral("runners/size"))) {
            QVector<LauncherRunner> runners;
            const int count = settings.beginReadArray(QStringLiteral("runners"));
            for (int i = 0; i < count; ++i) {
                settings.setArrayIndex(i);
                LauncherRunner runner;
                runner.id = settings.value(QStringLiteral("id")).toString();
                runner.name = settings.value(QStringLiteral("name")).toString();
                runner.command = settings.value(QStringLiteral("command")).toString();
                if (runner.id.isEmpty() || runner.command.isEmpty()) continue;
                if (runner.name.isEmpty()) runner.name = runner.command;
                runner.useShell = settings.value(QStringLiteral("useShell")).toBool();
                runners.append(runner);
            }
            settings.endArray();
            launcher.runners = runners;
        }

        // Profiles are gone: nothing was left to keep on one. The array is
        // still read, for the one thing it recorded that an item may still be
        // pointing at -- the Proton build a wine profile stood for.
        const int count = settings.beginReadArray(QStringLiteral("profiles"));
        for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);
            const QString profileId = settings.value(QStringLiteral("id")).toString();
            const QString tool = settings.value(QStringLiteral("compatToolId")).toString();
            if (launcher.id == QLatin1String(kWineId) && !profileId.isEmpty() &&
                !tool.isEmpty()) {
                legacyProtonProfiles()->insert(profileId, QLatin1String(kProtonPrefix) + tool);
            }
        }
        settings.endArray();
        settings.endGroup();
    }

    // Last, so a build installed on the machine lands behind whatever this
    // desktop added by hand, and so that a launcher never edited at all --
    // skipped by the loop above -- gets them too.
    for (Launcher &launcher : result) appendDiscoveredRunners(launcher);

    *loaded() = result;
}

} // namespace

bool Launcher::handles(const QString &path) const {
    const QString suffix = QFileInfo(path).suffix();
    if (suffix.isEmpty()) return false;
    for (const QString &extension : extensions) {
        if (extension.compare(suffix, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

const LauncherRunner *Launcher::runner(const QString &runnerId) const {
    if (runnerId.isEmpty()) return nullptr;
    for (const LauncherRunner &candidate : runners) {
        if (candidate.id == runnerId) return &candidate;
    }
    return nullptr;
}

const LauncherRunner *Launcher::defaultRunner() const {
    return runners.isEmpty() ? nullptr : &runners.first();
}

QVector<Launcher> defaultLaunchers() {
    QVector<Launcher> result;

    // Wine first, so that a .exe with nothing set up lands on it rather than
    // on Proton: it is the one that needs no Steam.
    Launcher wine;
    wine.id = QLatin1String(kWineId);
    wine.name = tr("Wine");
    wine.description = tr("Windows programs, run by one of the wines installed on this "
                          "machine. Every wine found is listed below.");
    wine.extensions = {QStringLiteral("exe")};
    wine.usesPrefixes = true;
    wine.usesWindowScale = true;
    wine.usesDpiScale = true;
    wine.arguments = QStringLiteral("{file}");
    result << wine;

    Launcher proton;
    proton.id = QLatin1String(kProtonId);
    proton.name = tr("Proton");
    proton.description = tr("Windows programs, run by one of the Proton builds installed "
                            "under Steam's compatibilitytools.d. Each program gets a wine "
                            "prefix of its own.");
    proton.extensions = {QStringLiteral("exe")};
    proton.usesPrefixes = true;
    proton.usesWindowScale = true;
    // Proton is handed the executable by protonInvocation, which knows the
    // environment it wants as well; this is here for the sake of the rest.
    proton.arguments = QStringLiteral("run {file}");
    result << proton;

    Launcher dosbox;
    dosbox.id = QStringLiteral("dosbox");
    dosbox.name = tr("DOSBox");
    dosbox.description = tr("DOS games, started from the .conf file that sets one up.");
    dosbox.extensions = {QStringLiteral("conf")};
    dosbox.arguments = QStringLiteral("-conf {file}");
    dosbox.runners = {makeRunner(QStringLiteral("db"), tr("DOSBox"), QStringLiteral("db"), true)};
    result << dosbox;

    Launcher scummvm;
    scummvm.id = QStringLiteral("scummvm");
    scummvm.name = tr("ScummVM");
    scummvm.description = tr("Point-and-click adventures. ScummVM is handed the folder the "
                             "game data sits in rather than the file itself.");
    scummvm.extensions = {QStringLiteral("trs")};
    scummvm.arguments = QStringLiteral("-p {dir}");
    scummvm.runners = {
        makeRunner(QStringLiteral("scm"), tr("ScummVM"), QStringLiteral("scm"), true)};
    result << scummvm;

    Launcher retroarch;
    retroarch.id = QStringLiteral("retroarch");
    retroarch.name = tr("RetroArch");
    retroarch.description = tr("Console ROMs and disc images. RetroArch will not pick a core "
                               "from the file on its own, so one is named for it.");
    retroarch.extensions = {QStringLiteral("nes"), QStringLiteral("z64"), QStringLiteral("n64"),
                            QStringLiteral("sfc"), QStringLiteral("smc"), QStringLiteral("gb"),
                            QStringLiteral("gba"), QStringLiteral("gcm"),
                            QStringLiteral("iso")};
    retroarch.usesCores = true;
    retroarch.cores = {
        {QStringLiteral("nes"), QStringLiteral("mesen_libretro.so")},
        {QStringLiteral("gba"), QStringLiteral("mgba_libretro.so")},
        {QStringLiteral("gb"), QStringLiteral("gearboy_libretro.so")},
        {QStringLiteral("sfc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("smc"), QStringLiteral("snes9x_libretro.so")},
        {QStringLiteral("n64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("z64"), QStringLiteral("mupen64plus_next_libretro.so")},
        {QStringLiteral("v64"), QStringLiteral("mupen64plus_next_libretro.so")},
    };
    retroarch.arguments = QStringLiteral("-L {core} {file}");
    retroarch.runners = {makeRunner(QStringLiteral("retroarch"), tr("RetroArch"),
                                    QStringLiteral("retroarch"), true)};
    result << retroarch;

    Launcher steam;
    steam.id = QStringLiteral("steam");
    steam.name = tr("Steam");
    steam.description = tr("Steam games, started from the app manifest Steam writes for each "
                           "one. Steam finds the game itself: the manifest only says which.");
    steam.extensions = {QStringLiteral("acf")};
    steam.usesSteamLibraries = true;
    steam.arguments = QStringLiteral("steam://rungameid/{appid}");
    steam.runners = {
        makeRunner(QStringLiteral("steam"), tr("Steam"), QStringLiteral("steam"))};
    result << steam;

    return result;
}

const QVector<Launcher> &launchers() {
    if (loaded()->isEmpty()) load();
    return *loaded();
}

void setLaunchers(const QVector<Launcher> &updated) {
    QSettings settings;
    // Written out whole: a runner deleted in the dialog has to leave, and the
    // array it was in is the only record of it.
    settings.remove(QStringLiteral("launchers"));
    for (const Launcher &launcher : updated) {
        settings.beginGroup(QStringLiteral("launchers/") + launcher.id);
        settings.setValue(QStringLiteral("extensions"), launcher.extensions);
        if (launcher.usesPrefixes) {
            settings.setValue(QStringLiteral("prefixesDir"), launcher.prefixesDir);
        }
        if (launcher.usesSteamLibraries) {
            settings.setValue(QStringLiteral("libraryFolders"), launcher.libraryFolders);
        }
        if (launcher.usesCores) {
            settings.setValue(QStringLiteral("coresDir"), launcher.coresDir);
            QStringList pairs;
            for (const auto &entry : launcher.cores) {
                pairs << entry.first + QLatin1Char('=') + entry.second;
            }
            settings.setValue(QStringLiteral("cores"), pairs);
        }

        // The wines and Proton builds among them are read off the machine
        // each session, so they are left out: saving them would freeze a list
        // that should follow what is installed.
        QVector<LauncherRunner> saved;
        for (const LauncherRunner &runner : launcher.runners) {
            if (!runner.discovered) saved.append(runner);
        }
        settings.beginWriteArray(QStringLiteral("runners"), saved.size());
        for (int i = 0; i < saved.size(); ++i) {
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("id"), saved[i].id);
            settings.setValue(QStringLiteral("name"), saved[i].name);
            settings.setValue(QStringLiteral("command"), saved[i].command);
            settings.setValue(QStringLiteral("useShell"), saved[i].useShell);
        }
        settings.endArray();
        settings.endGroup();
    }
    *loaded() = updated;
}

const Launcher *launcherById(const QString &id) {
    for (const Launcher &launcher : launchers()) {
        if (launcher.id == id) return &launcher;
    }
    return nullptr;
}

QVector<const Launcher *> launchersForPath(const QString &path) {
    QVector<const Launcher *> claiming;
    for (const Launcher &launcher : launchers()) {
        if (launcher.handles(path)) claiming.append(&launcher);
    }
    return claiming;
}

QString launcherId(const Launcher &launcher, const LauncherRunner &runner) {
    return launcher.id + QLatin1Char('/') + runner.id;
}

QVector<LauncherChoice> launchersFor(const QString &path) {
    QVector<LauncherChoice> choices;
    for (const Launcher *launcher : launchersForPath(path)) {
        for (const LauncherRunner &runner : launcher->runners) {
            // The launcher, and the runner where there is a choice of them --
            // but never a word twice, so a runner already called "Wine
            // (system)" is not prefixed with "Wine" again.
            QString label = launcher->name;
            if (launcher->runners.size() > 1 && !runner.name.isEmpty()) {
                if (runner.name.contains(launcher->name, Qt::CaseInsensitive)) {
                    label = runner.name;
                } else {
                    label = launcher->name + QStringLiteral(" - ") + runner.name;
                }
            }
            choices.append({launcherId(*launcher, runner), label, launcher->usesPrefixes,
                            launcher->usesWindowScale, launcher->usesDpiScale});
        }
    }
    return choices;
}

void launcherForItem(const QString &path, const QString &id, const Launcher **launcherOut,
                     const LauncherRunner **runnerOut) {
    *launcherOut = nullptr;
    *runnerOut = nullptr;

    const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.size() >= 2) {
        if (const Launcher *named = launcherById(parts.first())) {
            // Two parts is launcher and runner. A third is from when a
            // profile sat between them, and the runner was the last.
            const LauncherRunner *runner = named->runner(parts.last());

            // Older still: the middle part was a wine profile that stood for
            // a Proton build, and the build is where the item should go.
            if (!runner) {
                const QString legacy = legacyProtonProfiles()->value(parts.at(1));
                if (!legacy.isEmpty()) {
                    if (const Launcher *proton = launcherById(QLatin1String(kProtonId))) {
                        if (const LauncherRunner *build = proton->runner(legacy)) {
                            *launcherOut = proton;
                            *runnerOut = build;
                            return;
                        }
                    }
                }
            }
            if (!runner) runner = named->defaultRunner();
            if (runner) {
                *launcherOut = named;
                *runnerOut = runner;
                return;
            }
        }
    }

    // Nothing asked for, or a launcher since renamed away: the first one
    // claiming the type, started the way it leads with.
    const QVector<const Launcher *> claiming = launchersForPath(path);
    if (claiming.isEmpty()) return;
    const Launcher *launcher = claiming.first();
    const LauncherRunner *runner = launcher->defaultRunner();
    if (!runner) return;
    *launcherOut = launcher;
    *runnerOut = runner;
}

LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherRunner &runner,
                                   const QString &path, const QStringList &extraArgs) {
    const QString core = coreFor(launcher, path, runner);
    // Steam is handed an app id rather than a file: the manifest the item
    // points at is only where that id is written down.
    const QString appId = launcher.usesSteamLibraries ? steamAppId(path) : QString();
    // The launcher's own line, with the file in it, then the item's.
    const QStringList args = expand(launcher.arguments, path, core, appId) + extraArgs;

    // A command that is neither on the PATH nor a path of its own can only
    // be an alias or a shell function, whatever the setting says: starting it
    // directly would fail every time, so it is looked for where such things
    // live.
    const bool viaShell = runner.useShell ||
                          (!runner.command.contains(QLatin1Char('/')) &&
                           QStandardPaths::findExecutable(runner.command).isEmpty());

    LauncherCommand call;
    if (!viaShell) {
        call.program = runner.command;
        call.args = args;
        return call;
    }

    // An alias or a shell function exists only inside an interactive shell,
    // so one is started to run it. The arguments go through as positional
    // parameters rather than being written into the command line, so that
    // nothing in them is read as shell syntax.
    call.program = QStringLiteral("bash");
    call.args = QStringList{QStringLiteral("-ic"),
                            runner.command + QStringLiteral(" \"$@\""), QStringLiteral("_")} +
                args;
    return call;
}
