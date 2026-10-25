#pragma once

#include <QString>

// The version of this build and when it was made.
//
// The version comes from the VERSION file at the root of the source: three
// numbers, incremented by hand. The build stamp is the moment the binary was
// compiled, as yymmddhhmmss, so no two builds share one, which is what
// distinguishes yesterday's 1.1.0 from today's, and being a single number it
// only increases.
//
// A released binary carries no build stamp, its version alone identifying it,
// so the stamp is empty there and callers have to allow for that.
//
// Both are defined in a file written at each build; see
// cmake/GenerateVersion.cmake.
extern const char *const kProgmanVersion;
extern const char *const kProgmanBuildVersion;

inline QString appVersion() { return QString::fromLatin1(kProgmanVersion); }
inline QString buildVersion() { return QString::fromLatin1(kProgmanBuildVersion); }

// Both together, where there is room for both, or the version alone for a
// release, which has no build stamp.
inline QString fullVersion() {
    const QString build = buildVersion();
    if (build.isEmpty()) {
        return appVersion();
    }
    return appVersion() + QStringLiteral(" (build ") + build + QLatin1Char(')');
}
