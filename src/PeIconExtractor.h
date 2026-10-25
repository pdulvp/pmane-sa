#pragma once

#include <QString>
#include <QIcon>
#include <QVector>

// Extracts the "MAINICON"-style group icon embedded as a PE32/PE32+ resource
// (RT_GROUP_ICON / RT_ICON) inside a Windows .exe or .dll, with no external
// dependency (no icoutils, no libbfd).
//
// Only modern PE executables are supported, which covers the vast majority of
// real .exe files. Old 16-bit NE executables are not parsed; callers get a null
// QIcon and should fall back to a generic icon.
class PeIconExtractor {
public:
    // Where an icon came from. Legacy 16-bit (NE) and .ico artwork is flat and
    // low-colour, which is what the XP-style rendering is meant for.
    enum class Source { None, Pe, Ne, Ico };

    // One of the icons a file holds, as listIcons() found it.
    struct IconInfo {
        int index = -1;   // the index extractIconAt() accepts
        QIcon icon;
        int width = 0;    // of the decoded image
        int height = 0;
        int bitCount = 0; // colour depth the container declares, 0 if unknown
    };

    static QIcon extractIcon(const QString &exePath, Source *source = nullptr);

    // Reads a standalone .ico file: the same image payloads as the icons
    // embedded in an executable, in a different container.
    static QIcon extractIcoFile(const QString &icoPath);

    // Every distinct icon a file holds, in storage order: one per icon group in
    // a PE/NE executable, a group being the same artwork at several sizes, and
    // one per image in an .ico.
    static QVector<IconInfo> listIcons(const QString &path, Source *source = nullptr);

    // The icon listIcons() gave `index`. A negative index, or one the file no
    // longer holds because its icons changed since the item was set up, returns
    // whatever extractIcon() would.
    static QIcon extractIconAt(const QString &path, int index, Source *source = nullptr);
};
