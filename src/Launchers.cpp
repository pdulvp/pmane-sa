#include "Launchers.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

QString tr(const char *text) {
    return QCoreApplication::translate("Launchers", text);
}

// Where an emulator keeps its cores when no folder has been given: a
// libretro install puts them in a `cores` directory beside the binary. An
// alias is asked what it stands for, which is the one thing only the shell
// knows.
QString coresDirFor(const Launcher &launcher) {
    if (!launcher.coresDir.isEmpty()) return launcher.coresDir;

    QString binary = QStandardPaths::findExecutable(launcher.command);
    if (binary.isEmpty() && launcher.useShell) {
        QProcess probe;
        probe.start(QStringLiteral("bash"),
                    {QStringLiteral("-ic"), QStringLiteral("alias %1").arg(launcher.command)});
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

// The core to play this file with, as a full path: the one the profile pins,
// or whatever the launcher's table says the file type takes. Empty when
// neither knows, which leaves the program to start without one.
QString coreFor(const Launcher &launcher, const LauncherProfile &profile, const QString &path) {
    if (!launcher.usesCores) return {};

    QString core = profile.core;
    if (core.isEmpty()) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        for (const auto &entry : launcher.cores) {
            if (entry.first.compare(suffix, Qt::CaseInsensitive) == 0) {
                core = entry.second;
                break;
            }
        }
    }
    if (core.isEmpty()) return {};

    // A core given as a path of its own is taken as it stands.
    if (core.startsWith(QLatin1Char('/'))) return QFile::exists(core) ? core : QString();

    const QString dir = coresDirFor(launcher);
    if (dir.isEmpty()) return {};
    const QString full = QDir(dir).filePath(core);
    return QFile::exists(full) ? full : QString();
}

QStringList expand(const QString &arguments, const QString &file, const QString &core,
                   const QString &config) {
    QStringList out;
    // Split first and substitute after, so that a path with spaces in it
    // stays one argument however the template was written.
    for (const QString &original : QProcess::splitCommand(arguments)) {
        QString piece = original;
        piece.replace(QLatin1String("{file}"), file);
        piece.replace(QLatin1String("{dir}"), QFileInfo(file).absolutePath());
        piece.replace(QLatin1String("{core}"), core);
        piece.replace(QLatin1String("{config}"), config);

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

LauncherProfile makeProfile(const QString &id, const QString &name) {
    LauncherProfile profile;
    profile.id = id;
    profile.name = name;
    return profile;
}

void load() {
    QVector<Launcher> result = defaultLaunchers();
    QSettings settings;
    for (Launcher &launcher : result) {
        settings.beginGroup(QStringLiteral("launchers/") + launcher.id);
        if (!settings.contains(QStringLiteral("extensions"))) {
            settings.endGroup();
            continue; // never edited: keep the launcher as it shipped
        }

        // Read over what the launcher shipped with rather than in place of it:
        // a setting written before a field existed leaves nothing behind, and
        // a launcher with no command could start nothing at all.
        const auto keep = [&settings](const QString &key, const QString &shipped) {
            const QString saved = settings.value(key, shipped).toString();
            return saved.isEmpty() ? shipped : saved;
        };

        launcher.extensions = settings.value(QStringLiteral("extensions"), launcher.extensions)
                                .toStringList();
        launcher.command = keep(QStringLiteral("command"), launcher.command);
        launcher.arguments = keep(QStringLiteral("arguments"), launcher.arguments);
        launcher.useShell = settings.value(QStringLiteral("useShell"), launcher.useShell).toBool();
        if (launcher.usesCores) {
            launcher.coresDir = settings.value(QStringLiteral("coresDir")).toString();
            QVector<QPair<QString, QString>> cores;
            for (const QString &pair :
                 settings.value(QStringLiteral("cores")).toStringList()) {
                const int split = pair.indexOf(QLatin1Char('='));
                if (split <= 0) continue;
                cores.append({pair.left(split), pair.mid(split + 1)});
            }
            // A table emptied on purpose stays empty; one that was never
            // written at all stays the one the launcher shipped with.
            if (settings.contains(QStringLiteral("cores"))) launcher.cores = cores;
        }

        QVector<LauncherProfile> profiles;
        const int count = settings.beginReadArray(QStringLiteral("profiles"));
        for (int i = 0; i < count; ++i) {
            settings.setArrayIndex(i);
            LauncherProfile profile;
            profile.id = settings.value(QStringLiteral("id")).toString();
            profile.name = settings.value(QStringLiteral("name")).toString();
            if (profile.id.isEmpty() || profile.name.isEmpty()) continue;
            profile.extraArguments =
                settings.value(QStringLiteral("extraArguments")).toString();
            profile.compatToolId = settings.value(QStringLiteral("compatToolId")).toString();
            profile.prefixesDir = settings.value(QStringLiteral("prefixesDir")).toString();
            profile.baseConfig = settings.value(QStringLiteral("baseConfig")).toString();
            profile.core = settings.value(QStringLiteral("core")).toString();
            profiles.append(profile);
        }
        settings.endArray();
        settings.endGroup();

        // A launcher with every profile deleted from under it would have no way
        // of starting anything, so the one it shipped with stays.
        if (!profiles.isEmpty()) launcher.profiles = profiles;
    }
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

const LauncherProfile *Launcher::defaultProfile() const {
    return profiles.isEmpty() ? nullptr : &profiles.first();
}

const LauncherProfile *Launcher::profile(const QString &profileId) const {
    for (const LauncherProfile &candidate : profiles) {
        if (candidate.id == profileId) return &candidate;
    }
    return nullptr;
}

QVector<Launcher> defaultLaunchers() {
    QVector<Launcher> result;

    Launcher wine;
    wine.id = QStringLiteral("wine");
    wine.name = tr("Wine");
    wine.description = tr("Windows programs, run by the system wine or by one of the Proton "
                          "builds installed beside it.");
    wine.extensions = {QStringLiteral("exe")};
    wine.usesCompatTools = true;
    wine.command = QStringLiteral("wine");
    wine.arguments = QStringLiteral("{file}");
    wine.profiles = {makeProfile(QStringLiteral("default"), tr("Wine (system)"))};
    result << wine;

    Launcher dosbox;
    dosbox.id = QStringLiteral("dosbox");
    dosbox.name = tr("DOSBox");
    dosbox.description = tr("DOS games, started from the .conf file that sets one up.");
    dosbox.extensions = {QStringLiteral("conf")};
    dosbox.usesBaseConfig = true;
    dosbox.command = QStringLiteral("db");
    // The profile's own configuration first, where it has one, and the
    // item's over the top of it.
    dosbox.arguments = QStringLiteral("-conf {config} -conf {file}");
    dosbox.useShell = true;
    dosbox.profiles = {makeProfile(QStringLiteral("default"), tr("DOSBox"))};
    result << dosbox;

    Launcher scummvm;
    scummvm.id = QStringLiteral("scummvm");
    scummvm.name = tr("ScummVM");
    scummvm.description = tr("Point-and-click adventures. ScummVM is handed the folder the "
                             "game data sits in rather than the file itself.");
    scummvm.extensions = {QStringLiteral("trs")};
    scummvm.command = QStringLiteral("scm");
    scummvm.arguments = QStringLiteral("-p {dir}");
    scummvm.useShell = true;
    scummvm.profiles = {makeProfile(QStringLiteral("default"), tr("ScummVM"))};
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
    retroarch.command = QStringLiteral("retroarch");
    retroarch.arguments = QStringLiteral("-L {core} {file}");
    retroarch.useShell = true;
    retroarch.profiles = {makeProfile(QStringLiteral("default"), tr("RetroArch"))};
    result << retroarch;

    return result;
}

const QVector<Launcher> &launchers() {
    if (loaded()->isEmpty()) load();
    return *loaded();
}

void setLaunchers(const QVector<Launcher> &updated) {
    QSettings settings;
    // Written out whole: a profile deleted in the dialog has to leave, and
    // the array it was in is the only record of it.
    settings.remove(QStringLiteral("launchers"));
    for (const Launcher &launcher : updated) {
        settings.beginGroup(QStringLiteral("launchers/") + launcher.id);
        settings.setValue(QStringLiteral("extensions"), launcher.extensions);
        settings.setValue(QStringLiteral("command"), launcher.command);
        settings.setValue(QStringLiteral("arguments"), launcher.arguments);
        settings.setValue(QStringLiteral("useShell"), launcher.useShell);
        if (launcher.usesCores) {
            settings.setValue(QStringLiteral("coresDir"), launcher.coresDir);
            QStringList pairs;
            for (const auto &entry : launcher.cores) {
                pairs << entry.first + QLatin1Char('=') + entry.second;
            }
            settings.setValue(QStringLiteral("cores"), pairs);
        }

        settings.beginWriteArray(QStringLiteral("profiles"), launcher.profiles.size());
        for (int i = 0; i < launcher.profiles.size(); ++i) {
            const LauncherProfile &profile = launcher.profiles[i];
            settings.setArrayIndex(i);
            settings.setValue(QStringLiteral("id"), profile.id);
            settings.setValue(QStringLiteral("name"), profile.name);
            settings.setValue(QStringLiteral("extraArguments"), profile.extraArguments);
            settings.setValue(QStringLiteral("compatToolId"), profile.compatToolId);
            settings.setValue(QStringLiteral("prefixesDir"), profile.prefixesDir);
            settings.setValue(QStringLiteral("baseConfig"), profile.baseConfig);
            settings.setValue(QStringLiteral("core"), profile.core);
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

const Launcher *launcherForPath(const QString &path) {
    for (const Launcher &launcher : launchers()) {
        if (launcher.handles(path)) return &launcher;
    }
    return nullptr;
}

QString launcherId(const Launcher &launcher, const LauncherProfile &profile) {
    return launcher.id + QLatin1Char('/') + profile.id;
}

QVector<LauncherChoice> launchersFor(const QString &path) {
    QVector<LauncherChoice> choices;
    for (const Launcher &launcher : launchers()) {
        if (!launcher.handles(path)) continue;
        for (const LauncherProfile &profile : launcher.profiles) {
            // "RetroArch - mGBA", but simply "RetroArch" for the profile a
            // launcher leads with, whose name usually is the launcher's.
            const QString label = profile.name.contains(launcher.name)
                                      ? profile.name
                                      : QStringLiteral("%1 - %2").arg(launcher.name, profile.name);
            choices.append({launcherId(launcher, profile), label});
        }
    }
    return choices;
}

void launcherForItem(const QString &path, const QString &id, const Launcher **launcherOut,
                     const LauncherProfile **profileOut) {
    *launcherOut = nullptr;
    *profileOut = nullptr;

    const int split = id.indexOf(QLatin1Char('/'));
    if (split > 0) {
        if (const Launcher *named = launcherById(id.left(split))) {
            if (const LauncherProfile *profile = named->profile(id.mid(split + 1))) {
                *launcherOut = named;
                *profileOut = profile;
                return;
            }
        }
    }

    // Nothing asked for, or a profile since deleted: whatever the file type
    // leads to, started the way that launcher starts things by default.
    const Launcher *launcher = launcherForPath(path);
    if (!launcher) return;
    *launcherOut = launcher;
    *profileOut = launcher->defaultProfile();
}

LauncherCommand launcherCommandFor(const Launcher &launcher, const LauncherProfile &profile,
                               const QString &path, const QStringList &extraArgs) {
    const QString core = coreFor(launcher, profile, path);
    // The launcher's arguments, then the profile's own, then the item's.
    const QStringList args = expand(launcher.arguments, path, core, profile.baseConfig) +
                             expand(profile.extraArguments, path, core, profile.baseConfig) +
                             extraArgs;

    // A command that is neither on the PATH nor a path of its own can only
    // be an alias or a shell function, whatever the setting says: starting it
    // directly would fail every time, so it is looked for where such things
    // live.
    const bool viaShell = launcher.useShell ||
                          (!launcher.command.contains(QLatin1Char('/')) &&
                           QStandardPaths::findExecutable(launcher.command).isEmpty());

    LauncherCommand call;
    if (!viaShell) {
        call.program = launcher.command;
        call.args = args;
        return call;
    }

    // An alias or a shell function exists only inside an interactive shell,
    // so one is started to run it. The arguments go through as positional
    // parameters rather than being written into the command line, so that
    // nothing in them is read as shell syntax.
    call.program = QStringLiteral("bash");
    call.args = QStringList{QStringLiteral("-ic"),
                            launcher.command + QStringLiteral(" \"$@\""), QStringLiteral("_")} +
                args;
    return call;
}
