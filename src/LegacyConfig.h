#pragma once

// The settings were kept under `~/.config/pmane-sa/` when the organisation
// name was the program's own; it is `pdulvp` now, so QStandardPaths points
// somewhere else and an install made before the rename would come up with an
// empty desktop. Qt keeps no record of a previous name, so the folder is moved
// here, once, at startup.
//
// Does nothing when there is nothing to move, when the current folder already
// holds a desktop, or when a folder was named on the command line.
void migrateLegacyConfigFolder();
