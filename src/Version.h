#pragma once

#include <QString>

// What build this is: the date and time it was made, as yy.mm.ddhhmmss.
// There is no release number to go with it -- the Program Manager is built
// rather than released -- so the moment of the build is the whole version,
// and no two builds carry the same one.
//
// Defined in a file written at each build; see cmake/GenerateVersion.cmake.
extern const char *const kProgmanBuildVersion;

inline QString buildVersion() { return QString::fromLatin1(kProgmanBuildVersion); }
