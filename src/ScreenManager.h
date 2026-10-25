#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The named desktops the Program Manager can switch between. A screen holds a
// whole workspace: its program groups, their window positions and sizes, the
// order of the icons inside them, and its colour scheme. Each is a .ini of its
// own in the config directory, listed by a small index that also records which
// was last open.
//
// Screens were called profiles until recently; an index written under the old
// name is read once and written back under the new one.
//
// --configPath moves the index and the screens to a chosen directory, which is
// what makes the desktop kept beside the sources, the one the documentation's
// screenshots are taken from, openable without disturbing the desktop in
// ~/.config. Nothing else changes: screens there are created, renamed and
// deleted as usual.
class ScreenManager {
public:
    ScreenManager();

    // Sets where screens are kept for the rest of this run. Must be called
    // before the first ScreenManager is created. An empty path is ignored; one
    // that does not exist is created.
    static void useConfigPath(const QString &path);

    QStringList names() const;
    QString current() const { return m_current; }
    bool contains(const QString &name) const { return indexOf(name) >= 0; }

    // The file one screen's desktop is stored in. Empty for an unknown name.
    QString filePath(const QString &name) const;

    // Each of these writes the index back out. They return false and change
    // nothing when the name is empty or already in use.
    bool create(const QString &name);
    bool duplicate(const QString &source, const QString &name);
    bool rename(const QString &from, const QString &to);
    bool remove(const QString &name);

    void setCurrent(const QString &name);

private:
    struct Entry {
        QString name;
        QString file; // relative to the config directory
    };

    int indexOf(const QString &name) const;
    QString configDir() const;
    QString indexPath() const;
    QString legacyIndexPath() const;
    // A file name derived from the screen's name and not already in use by
    // another screen.
    QString uniqueFileName(const QString &name) const;
    void loadIndex();
    void saveIndex() const;
    // On first run, adopts a groups.ini from before screens existed as the
    // default screen, so an existing desktop is not lost.
    void seedFirstScreen();

    QVector<Entry> m_entries;
    QString m_current;

    // The absolute directory given by --configPath. Empty when screens are kept
    // in the usual place under ~/.config.
    static QString s_configPath;
};
