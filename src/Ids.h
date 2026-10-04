#pragma once

#include <QString>

// An identity for a thing on the desktop that nothing else about it can
// provide.
//
// A group was only ever its title and a program item only ever its command,
// which is fine until the same program is wanted twice: two icons for the
// same executable, one set up for the demo and one for the full game, or one
// in Games and one in StartUp. Anything worked out from the name or the path
// would be the same for both, and the two would then share whatever was
// keyed on it -- a wine prefix, most of all, which is where the saved games
// are.
//
// So each is given one of these when it is made, and keeps it: random, short
// enough to read in a folder name, and long enough that two of them are not
// going to collide on one desktop.
QString newId();
