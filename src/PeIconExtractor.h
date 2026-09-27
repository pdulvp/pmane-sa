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
    static QIcon extractIcon(const QString &exePath);
};
