#pragma once

#include <Qt>
#include <QPoint>

class QWidget;

// A window without a frame has to offer its own edges to drag: the window
// manager is no longer drawing any. These say which edges a point is close
// enough to, what the pointer should look like there, and hand the resize
// over to the window manager once one starts.

// How wide the band along each edge is, in pixels.
constexpr int kResizeMargin = 4;

// The edges `globalPos` is within the margin of. Empty away from them, and
// for a window that cannot be resized where it is -- maximized or full
// screen.
Qt::Edges windowEdgesAt(const QWidget *window, const QPoint &globalPos);

// The pointer that says which way those edges would stretch.
Qt::CursorShape cursorForWindowEdges(Qt::Edges edges);

// Asks the window manager to take over a resize from these edges. False
// when there is nothing to hand over, so the caller can treat the press as
// an ordinary one.
bool startWindowResize(QWidget *window, Qt::Edges edges);
