#pragma once

#include <QString>
#include <QIcon>

// Extracts the "MAINICON"-style group icon embedded as a PE32/PE32+
// resource (RT_GROUP_ICON / RT_ICON) inside a Windows .exe/.dll, without
// any external dependency (no icoutils, no libbfd).
//
// Only modern PE executables are supported (the vast majority of real
// world .exe files). Old 16-bit NE executables are not parsed; callers
// get a null QIcon back and should fall back to a generic icon.
class PeIconExtractor {
public:
    // Where an icon came from. Legacy 16-bit (NE) and .ico artwork is flat
    // and low-colour, which is what the XP-style rendering targets.
    enum class Source { None, Pe, Ne, Ico };

    static QIcon extractIcon(const QString &exePath, Source *source = nullptr);

    // Reads a standalone .ico file. Same image payloads as the icons
    // embedded in an executable, just a different container around them.
    static QIcon extractIcoFile(const QString &icoPath);
};
