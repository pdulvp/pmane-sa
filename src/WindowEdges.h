#pragma once

#include <Qt>
#include <QPoint>

class QWidget;

// A frameless window has to provide its own resize edges, the window manager
// drawing none. These report which edges a point is close enough to, the
// cursor to show there, and hand a started resize over to the window manager.

// The width of the resize band along each edge, in pixels.
constexpr int kResizeMargin = 4;

// The edges `globalPos` is within the margin of. Empty elsewhere, and for a
// window that cannot be resized in its current state, maximized or full screen.
Qt::Edges windowEdgesAt(const QWidget *window, const QPoint &globalPos);

// Whether the window fills the screen, maximized or full screen. While it does,
// the caption must not be dragged or double clicked out of that state by
// accident; the caption buttons are the way back.
bool windowFillsScreen(const QWidget *window);

// Whether a drag on the window's caption should move it. A window filling the
// screen is not moved: the compositor would restore it to its old size just to
// follow the pointer, which is rarely what was intended.
bool windowCanBeMoved(const QWidget *window);

// The cursor shape indicating which way those edges would resize.
Qt::CursorShape cursorForWindowEdges(Qt::Edges edges);

// Asks the window manager to take over a resize from these edges. Returns false
// when there is nothing to hand over, so the caller can treat the press as an
// ordinary one.
bool startWindowResize(QWidget *window, Qt::Edges edges);
