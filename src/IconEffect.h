#pragma once

#include <QIcon>

// Re-renders a flat legacy icon the way the Windows XP icon set was drawn:
// tipped slightly about the vertical axis into a shallow perspective, and
// lifted off the background with a soft drop shadow.
QIcon xpStyleIcon(const QIcon &source);
