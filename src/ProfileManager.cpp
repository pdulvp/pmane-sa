#include "ProfileManager.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

namespace {

const char *kIndexFile = "profiles.ini";
const char *kLegacyFile = "groups.ini";
const char *kDefaultName = "Default";

// Profile names are free text, so the file they are stored in is a slug of
// the name rather than the name itself.
QString slug(const QString &name) {
    static const QRegularExpression unsafe(QStringLiteral("[^a-z0-9]+"));
    QString s = name.toLower().replace(unsafe, QStringLiteral("-"));
    while (s.startsWith('-')) s.remove(0, 1);
    while (s.endsWith('-')) s.chop(1);
    return s.isEmpty() ? QStringLiteral("profile") : s;
}

} // namespace

ProfileManager::ProfileManager() {
    QDir().mkpath(configDir());
    loadIndex();
    if (m_entries.isEmpty()) seedFirstProfile();
    if (indexOf(m_current) < 0) m_current = m_entries.first().name;
}

QString ProfileManager::configDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
}

QString ProfileManager::indexPath() const {
    return configDir() + QLatin1Char('/') + QLatin1String(kIndexFile);
}

int ProfileManager::indexOf(const QString &name) const {
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    }
    return -1;
}

QStringList ProfileManager::names() const {
    QStringList list;
    list.reserve(m_entries.size());
    for (const Entry &entry : m_entries) list.append(entry.name);
    return list;
}

QString ProfileManager::filePath(const QString &name) const {
    const int i = indexOf(name);
    if (i < 0) return {};
    return configDir() + QLatin1Char('/') + m_entries[i].file;
}

QString ProfileManager::uniqueFileName(const QString &name) const {
    const QString base = slug(name);
    QString candidate = base + QStringLiteral(".ini");
    for (int n = 2; ; ++n) {
        bool taken = false;
        for (const Entry &entry : m_entries) {
            if (entry.file.compare(candidate, Qt::CaseInsensitive) == 0) {
                taken = true;
                break;
            }
        }
        if (!taken && !QFile::exists(configDir() + QLatin1Char('/') + candidate)) return candidate;
        candidate = base + QStringLiteral("-") + QString::number(n) + QStringLiteral(".ini");
    }
}

void ProfileManager::loadIndex() {
    if (!QFile::exists(indexPath())) return;

    QSettings settings(indexPath(), QSettings::IniFormat);
    const int count = settings.beginReadArray(QStringLiteral("Profiles"));
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        Entry entry;
        entry.name = settings.value(QStringLiteral("name")).toString();
        entry.file = settings.value(QStringLiteral("file")).toString();
        if (entry.name.isEmpty() || entry.file.isEmpty()) continue;
        if (indexOf(entry.name) >= 0) continue;
        m_entries.append(entry);
    }
    settings.endArray();
    m_current = settings.value(QStringLiteral("current")).toString();
}

void ProfileManager::saveIndex() const {
    QSettings settings(indexPath(), QSettings::IniFormat);
    settings.clear();
    settings.setValue(QStringLiteral("current"), m_current);
    settings.beginWriteArray(QStringLiteral("Profiles"), m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue(QStringLiteral("name"), m_entries[i].name);
        settings.setValue(QStringLiteral("file"), m_entries[i].file);
    }
    settings.endArray();
}

void ProfileManager::seedFirstProfile() {
    Entry entry;
    entry.name = QLatin1String(kDefaultName);
    // A desktop saved before profiles existed becomes the default profile,
    // read straight out of the file it is already in.
    entry.file = QFile::exists(configDir() + QLatin1Char('/') + QLatin1String(kLegacyFile))
                     ? QLatin1String(kLegacyFile)
                     : uniqueFileName(entry.name);
    m_entries.append(entry);
    m_current = entry.name;
    saveIndex();
}

bool ProfileManager::create(const QString &name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || indexOf(trimmed) >= 0) return false;

    m_entries.append({trimmed, uniqueFileName(trimmed)});
    saveIndex();
    return true;
}

bool ProfileManager::duplicate(const QString &source, const QString &name) {
    const QString trimmed = name.trimmed();
    const int from = indexOf(source);
    if (from < 0 || trimmed.isEmpty() || indexOf(trimmed) >= 0) return false;

    const QString file = uniqueFileName(trimmed);
    // A profile that was never saved has no file yet; the copy then simply
    // starts out empty as well.
    const QString sourcePath = configDir() + QLatin1Char('/') + m_entries[from].file;
    if (QFile::exists(sourcePath) &&
        !QFile::copy(sourcePath, configDir() + QLatin1Char('/') + file)) {
        return false;
    }

    m_entries.append({trimmed, file});
    saveIndex();
    return true;
}

bool ProfileManager::rename(const QString &from, const QString &to) {
    const QString trimmed = to.trimmed();
    const int i = indexOf(from);
    if (i < 0 || trimmed.isEmpty()) return false;
    // Renaming to a different spelling of the same name is allowed; to
    // somebody else's name is not.
    const int clash = indexOf(trimmed);
    if (clash >= 0 && clash != i) return false;

    if (m_current.compare(from, Qt::CaseInsensitive) == 0) m_current = trimmed;
    m_entries[i].name = trimmed;
    // The file keeps its old slug: renaming it would buy nothing and risks
    // losing the desktop if the move fails.
    saveIndex();
    return true;
}

bool ProfileManager::remove(const QString &name) {
    const int i = indexOf(name);
    if (i < 0 || m_entries.size() <= 1) return false;

    QFile::remove(configDir() + QLatin1Char('/') + m_entries[i].file);
    m_entries.removeAt(i);
    if (indexOf(m_current) < 0) m_current = m_entries.first().name;
    saveIndex();
    return true;
}

void ProfileManager::setCurrent(const QString &name) {
    if (indexOf(name) < 0) return;
    m_current = m_entries[indexOf(name)].name; // canonical spelling
    saveIndex();
}
