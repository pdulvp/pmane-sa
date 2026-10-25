#pragma once

#include <QIcon>

// Re-renders a flat legacy icon in the style of the Windows XP icon set:
// tilted slightly about the vertical axis into a shallow perspective, with a
// soft drop shadow.
QIcon xpStyleIcon(const QIcon &source);
