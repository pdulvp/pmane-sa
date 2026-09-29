#include "DesktopEntry.h"

#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QStringList>
#include <QTextStream>

namespace {

// Name[fr_CA] beats Name[fr] beats Name. The suffix is compared against the
// locale's own names rather than parsed, which is all the matching the
// specification's fallback rules need here.
int localeRank(const QString &suffix) {
    if (suffix.isEmpty()) return 0;
    const QLocale locale;
    if (suffix == locale.name()) return 3;                        // fr_CA
    if (suffix == locale.name().section(QLatin1Char('_'), 0, 0)) return 2; // fr
    for (const QString &ui : QLocale::system().uiLanguages()) {
        if (suffix == ui || suffix == ui.section(QLatin1Char('-'), 0, 0)) return 1;
    }
    return -1; // a translation into some other language
}

} // namespace

DesktopEntry readDesktopEntry(const QString &path) {
    DesktopEntry entry;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);

    bool inEntryGroup = false;
    int nameRank = -1;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;

        if (line.startsWith(QLatin1Char('['))) {
            // Actions and other groups follow the first one and are not ours.
            inEntryGroup = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntryGroup) continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0) continue;
        QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();

        QString localeSuffix;
        if (key.endsWith(QLatin1Char(']'))) {
            const int bracket = key.indexOf(QLatin1Char('['));
            if (bracket > 0) {
                localeSuffix = key.mid(bracket + 1, key.size() - bracket - 2);
                key = key.left(bracket);
            }
        }

        if (key == QLatin1String("Name")) {
            const int rank = localeRank(localeSuffix);
            if (rank > nameRank) {
                nameRank = rank;
                entry.name = value;
            }
        } else if (localeSuffix.isEmpty()) {
            if (key == QLatin1String("Icon")) {
                entry.icon = value;
            } else if (key == QLatin1String("Exec")) {
                entry.exec = value;
            } else if (key == QLatin1String("Path")) {
                entry.path = value;
            } else if (key == QLatin1String("Terminal")) {
                entry.terminal = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
            }
        }
    }
    return entry;
}

QStringList desktopEntryCommand(const DesktopEntry &entry) {
    QStringList parts = QProcess::splitCommand(entry.exec);

    for (int i = parts.size() - 1; i >= 0; --i) {
        const QString &part = parts[i];
        // %% is an escaped percent sign and stays; a lone field code is the
        // application's way of asking for a file, url or icon it is not
        // being given.
        if (part.size() == 2 && part.startsWith(QLatin1Char('%')) &&
            part != QLatin1String("%%")) {
            parts.removeAt(i);
        }
    }
    return parts;
}
