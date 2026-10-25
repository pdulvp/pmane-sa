#pragma once

#include <QString>

// A unique id for a group or an item, which nothing else about it can provide.
//
// A group used to be identified by its title and a program item by its command,
// which works until the same program is wanted twice: two icons for the same
// executable, one for the demo and one for the full game, or one in Games and
// one in StartUp. Anything derived from the name or the path would be identical
// for both, and the two would then share whatever is keyed on it, above all a
// wine prefix, which holds the saved games.
//
// Each is therefore given one of these when it is created and keeps it: random,
// short enough to read in a folder name, and long enough that two will not
// collide on one desktop.
QString newId();
