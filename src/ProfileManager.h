#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// The named desktops the Program Manager can switch between. A profile
// owns a whole workspace: its program groups, where their windows sit and
// how big they are, the order of the icons inside them, and its colour
// scheme. Each one is a .ini of its own in the config directory, listed by
// a small index that also remembers which was last open.
class ProfileManager {
public:
    ProfileManager();

    QStringList names() const;
    QString current() const { return m_current; }
    bool contains(const QString &name) const { return indexOf(name) >= 0; }

    // Where one profile's desktop is stored. Empty for an unknown name.
    QString filePath(const QString &name) const;

    // Each of these writes the index back out. They return false and change
    // nothing when the name is blank or already taken.
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
    // A file name derived from the profile name that no other profile is
    // already using.
    QString uniqueFileName(const QString &name) const;
    void loadIndex();
    void saveIndex() const;
    // First run: adopt a pre-profiles groups.ini as the default profile so
    // an existing desktop is not lost.
    void seedFirstProfile();

    QVector<Entry> m_entries;
    QString m_current;
};
