#pragma once

#include <QString>

// What this is and when it was made.
//
// The version is the one in the VERSION file at the root of the source --
// three numbers, moved on by hand when something is worth calling a new
// version. The build is the moment the binary was compiled, as
// yymmddhhmmss: no two builds carry the same one, which is what tells
// yesterday's 1.1.0 from the one in front of you, and being one number it
// only ever counts upwards.
//
// Both are defined in a file written at each build; see
// cmake/GenerateVersion.cmake.
extern const char *const kProgmanVersion;
extern const char *const kProgmanBuildVersion;

inline QString appVersion() { return QString::fromLatin1(kProgmanVersion); }
inline QString buildVersion() { return QString::fromLatin1(kProgmanBuildVersion); }

// The two together, for a line that has room for both.
inline QString fullVersion() {
    return appVersion() + QStringLiteral(" (build ") + buildVersion() + QLatin1Char(')');
}
