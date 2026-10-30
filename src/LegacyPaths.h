#pragma once

// The program's folders were under `pmane-sa/pmane-sa` when the organisation
// name was the program's own; the organisation is `pdulvp` now, so
// QStandardPaths points somewhere else and an install made before the rename
// would come up with an empty desktop and none of its wine prefixes. Qt keeps
// no record of a previous name, so the folders are moved here, once, at
// startup: the settings under `~/.config`, the prefixes under
// `~/.local/share`, and the launch logs under `~/.cache`.
//
// Does nothing where there is nothing to move, where the current folder
// already holds files, or when a folder was named on the command line.
void migrateLegacyFolders();
