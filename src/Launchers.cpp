#include "Launchers.h"

#include "CompatTools.h"
#include "LibretroCores.h"
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

// The two launchers whose runners are detected on the machine rather than
// configured here: every installed wine, and every Proton build under Steam.
const char *kWineId = "wine";
const char *kProtonId = "proton";

// Prefixed so a detected runner's id can never collide with one generated for
// a runner added by hand.
const char *kWinePrefix = "wine:";
const char *kProtonPrefix = "proton:";

} // namespace

QString resolvedRunnerCommand(const LauncherRunner &runner) {
    // Cached: an alias is resolved by asking an interactive shell, which costs
    // the best part of a second and is asked for again by every launch, every
    // core scan and every properties dialog.
    static QHash<QString, QString> cache;
    const QString key = runner.id + QLatin1Char('|') + runner.command;
    const auto found = cache.constFind(key);
    if (found != cache.constEnd()) return *found;

    QString resolved;
    if (runner.command.startsWith(QLatin1Char('/'))) {
        resolved = runner.command;
    } else {
        resolved = QStandardPaths::findExecutable(runner.command);
    }

    // An alias is known to the shell and nowhere else, and its definition is
    // a whole command line: the Lutris RetroArch is an alias carrying the
    // `--config` of the install it belongs to, which is the only thing that
    // says where its cores are.
    if (resolved.isEmpty() && runner.useShell) {
        QProcess probe;
        probe.start(QStringLiteral("bash"),
                    {QStringLiteral("-ic"), QStringLiteral("alias %1").arg(runner.command)});
        if (probe.waitForFinished(3000)) {
            const QString definition = QString::fromLocal8Bit(probe.readAllStandardOutput());
            // alias name='<command line>'
            static const QRegularExpression body(QStringLiteral("='(.*)'\\s*$"));
            const QRegularExpressionMatch quoted = body.match(definition.trimmed());
            if (quoted.hasMatch()) {
                resolved = quoted.captured(1);
            } else {
                // Defined without quotes, or printed some other way: the first
                // absolute path in it is the program.
                static const QRegularExpression path(QStringLiteral("(/[^\\s'\"]+)"));
                const QRegularExpressionMatch match = path.match(definition);
                if (match.hasMatch()) resolved = match.captured(1);
            }
        }
    }

    cache.insert(key, resolved);
    return resolved;
}

QString resolvedRunnerBinary(const LauncherRunner &runner) {
    const QStringList parts = QProcess::splitCommand(resolvedRunnerCommand(runner));
    return parts.isEmpty() ? QString() : parts.first();
}

QString coresFolderFor(const Launcher &launcher, const LauncherRunner &runner) {
    if (!launcher.coresDir.isEmpty()) return launcher.coresDir;

    // Beside this runner's own program: two RetroArch installs each keep their
    // cores in their own folder.
    const QString binary = resolvedRunnerBinary(runner);
    if (binary.isEmpty()) return {};
    const QString cores = QFileInfo(binary).absolutePath() + QStringLiteral("/cores");
    return QDir(cores).exists() ? cores : QString();
}

namespace {

// The full path of the core to play this file with: the one the item names,
// else the one the launcher's table gives the file type. Empty when neither
// leads to an installed core, in which case the program is started without
// one -- RetroArch then comes up on its own menu, which is better than loading
// a core meant for another system.
QString coreFor(const Launcher &launcher, const QString &path, const LauncherRunner &runner,
                const QString &chosenCore) {
    if (!launcher.usesCores) return {};

    QString core = chosenCore;
    if (core.isEmpty()) {
        // No core of its own: every item of a desktop made before they could
        // be picked per item is in this case.
        const QString suffix = QFileInfo(path).suffix().toLower();
        for (const auto &entry : launcher.cores) {
            if (entry.first.compare(suffix, Qt::CaseInsensitive) == 0) {
                core = entry.second;
                break;
            }
        }
    }
    if (core.isEmpty()) return {};

    // A core already given as a path is used as it is.
    if (core.startsWith(QLatin1Char('/'))) return QFile::exists(core) ? core : QString();

    // Named by its file name, so it is looked for in the folder of the install
    // that is about to load it.
    const QString dir = coresFolderFor(launcher, runner);
    if (dir.isEmpty()) return {};
    const QString full = QDir(dir).filePath(core);
    return QFile::exists(full) ? full : QString();
}

QStringList expand(const QString &arguments, const QString &file, const QString &core,
                   const QString &appId) {
    QStringList out;
    // Split first and substitute afterwards, so a path containing spaces stays
    // one argument whatever the template looks like.
    for (const QString &original : QProcess::splitCommand(arguments)) {
        QString piece = original;
        piece.replace(QLatin1String("{file}"), file);
        piece.replace(QLatin1String("{dir}"), QFileInfo(file).absolutePath());
        piece.replace(QLatin1String("{core}"), core);
        piece.replace(QLatin1String("{appid}"), appId);

        // A placeholder with no value leaves an empty argument, which is still
        // an argument. Drop it, and the flag introducing it with it, so
        // "-L {core} {file}" is just the file when no core was found.
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

// An item saved before wine and Proton were separate launchers names a wine
// profile, and that profile is where its Proton build was recorded. This maps
// one to the other, so such an item keeps running through the build it was set
// up with instead of falling back to plain wine.
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

// The installed wines and Proton builds, as runners. Detected every session
// rather than saved, so one installed since last time appears and one
// uninstalled disappears.
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
        // Set for display, so the list reads as programs; protonInvocation()
        // is what actually starts one of these.
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
            continue; // never edited: keep the launcher's shipped settings
        }

        launcher.extensions =
            settings.value(QStringLiteral("extensions"), launcher.extensions).toStringList();
        if (launcher.usesPrefixes) {
            launcher.prefixesDir = settings.value(QStringLiteral("prefixesDir")).toString();
            // A list emptied deliberately stays empty, as the core list does.
            if (settings.contains(QStringLiteral("prefixes"))) {
                launcher.prefixes =
                    settings.value(QStringLiteral("prefixes")).toStringList();
            }
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
            // A list emptied deliberately stays empty; one never written at
            // all keeps the launcher's shipped value.
            if (settings.contains(QStringLiteral("cores"))) launcher.cores = cores;
        }

        // The runners added by hand. The detected ones are not saved here and
        // are appended again afterwards.
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

        // Profiles no longer exist. The array is still read for the one thing
        // it recorded that an item may still refer to: the Proton build a wine
        // profile stood for.
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

    // Last, so a detected build is listed after the runners added by hand, and
    // so that a launcher never edited, skipped by the loop above, gets them
    // too.
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

    // Wine first, so an unconfigured .exe goes to it rather than to Proton,
    // since it does not need Steam.
    Launcher wine;
    wine.id = QLatin1String(kWineId);
    wine.name = tr("Wine");
    wine.description = tr("Windows programs, run by one of the wines installed on this "
                          "machine. Every wine found is listed below.");
    wine.extensions = {QStringLiteral("exe")};
    wine.usesPrefixes = true;
    wine.usesWindowScale = true;
    wine.usesDpiScale = true;
    wine.usesArguments = true;
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
    proton.usesArguments = true;
    // protonInvocation() passes the executable to Proton, along with the
    // environment it needs; this field is only here for consistency.
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
    // Written out in full: a runner deleted in the dialog has to disappear,
    // and this array is the only record of it.
    settings.remove(QStringLiteral("launchers"));
    for (const Launcher &launcher : updated) {
        settings.beginGroup(QStringLiteral("launchers/") + launcher.id);
        settings.setValue(QStringLiteral("extensions"), launcher.extensions);
        if (launcher.usesPrefixes) {
            settings.setValue(QStringLiteral("prefixesDir"), launcher.prefixesDir);
            settings.setValue(QStringLiteral("prefixes"), launcher.prefixes);
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

        // The wines and Proton builds are detected each session, so they are
        // left out: saving them would freeze a list that has to follow what is
        // installed.
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
            // The launcher name, plus the runner name when there is a choice,
            // without repeating a word: a runner already called "Wine
            // (system)" is not prefixed with "Wine" again.
            QString label = launcher->name;
            if (launcher->runners.size() > 1 && !runner.name.isEmpty()) {
                if (runner.name.contains(launcher->name, Qt::CaseInsensitive)) {
                    label = runner.name;
                } else {
                    label = launcher->name + QStringLiteral(" - ") + runner.name;
                }
            }
            LauncherChoice choice;
            choice.id = launcherId(*launcher, runner);
            choice.label = label;
            choice.usesPrefixes = launcher->usesPrefixes;
            choice.prefixes = launcher->prefixes;
            choice.usesWindowScale = launcher->usesWindowScale;
            choice.usesDpiScale = launcher->usesDpiScale;
            choice.usesArguments = launcher->usesArguments;
            // The cores of this install that play this file, which is what the
            // properties dialog offers. Scanned once per install and then
            // cached, so listing the launchers stays cheap.
            choice.usesCores = launcher->usesCores;
            if (launcher->usesCores) choice.cores = libretroCoresFor(*launcher, runner, path);
            choices.append(choice);
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
            // Two parts means launcher and runner. A third part comes from
            // when a profile sat between them, the runner being last.
            const LauncherRunner *runner = named->runner(parts.last());

            // Older still: the middle part was a wine profile standing for a
            // Proton build, which is where the item should be sent.
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

    // Nothing named, or a launcher that no longer exists: use the first one
    // claiming the type, with its default runner.
    const QVector<const Launcher *> claiming = launchersForPath(path);
    if (claiming.isEmpty()) return;
    const Launcher *launcher = claiming.first();
    const LauncherRunner *runner = launcher->defaultRunner();
    if (!runner) return;
    *launcherOut = launcher;
    *runnerOut = runner;
}

LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherRunner &runner,
                                   const QString &path, const QStringList &extraArgs,
                                   const QString &chosenCore) {
    const QString core = coreFor(launcher, path, runner, chosenCore);
    // Steam takes an app id rather than a file; the manifest the item points
    // at is only where that id is recorded.
    const QString appId = launcher.usesSteamLibraries ? steamAppId(path) : QString();
    // The launcher's own arguments, with the file substituted in, then the
    // item's.
    const QStringList args = expand(launcher.arguments, path, core, appId) + extraArgs;

    // A command that is neither on the PATH nor a path can only be an alias or
    // a shell function, whatever the setting says: running it directly would
    // always fail, so it is run through a shell regardless.
    const bool viaShell = runner.useShell ||
                          (!runner.command.contains(QLatin1Char('/')) &&
                           QStandardPaths::findExecutable(runner.command).isEmpty());

    LauncherCommand call;
    if (!viaShell) {
        call.program = runner.command;
        call.args = args;
        return call;
    }

    // An alias or shell function only exists inside an interactive shell, so
    // one is started to run it. The arguments are passed as positional
    // parameters rather than written into the command line, so nothing in them
    // is interpreted as shell syntax.
    call.program = QStringLiteral("bash");
    call.args = QStringList{QStringLiteral("-ic"),
                            runner.command + QStringLiteral(" \"$@\""), QStringLiteral("_")} +
                args;
    return call;
}
