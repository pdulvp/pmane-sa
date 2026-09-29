#include "WindowEdges.h"

#include <QPoint>
#include <QWidget>
#include <QWindow>

Qt::Edges windowEdgesAt(const QWidget *window, const QPoint &globalPos) {
    if (!window) return {};
    if (windowFillsScreen(window)) return {};

    const QRect frame = window->frameGeometry();
    if (!frame.adjusted(-kResizeMargin, -kResizeMargin, kResizeMargin, kResizeMargin)
             .contains(globalPos)) {
        return {};
    }

    Qt::Edges edges;
    if (globalPos.x() <= frame.left() + kResizeMargin) edges |= Qt::LeftEdge;
    if (globalPos.x() >= frame.right() - kResizeMargin) edges |= Qt::RightEdge;
    if (globalPos.y() <= frame.top() + kResizeMargin) edges |= Qt::TopEdge;
    if (globalPos.y() >= frame.bottom() - kResizeMargin) edges |= Qt::BottomEdge;
    return edges;
}

bool windowFillsScreen(const QWidget *window) {
    return window && (window->isMaximized() || window->isFullScreen());
}

bool windowCanBeMoved(const QWidget *window) {
    return window && !windowFillsScreen(window);
}

Qt::CursorShape cursorForWindowEdges(Qt::Edges edges) {
    if ((edges & Qt::LeftEdge && edges & Qt::TopEdge) ||
        (edges & Qt::RightEdge && edges & Qt::BottomEdge)) {
        return Qt::SizeFDiagCursor;
    }
    if ((edges & Qt::RightEdge && edges & Qt::TopEdge) ||
        (edges & Qt::LeftEdge && edges & Qt::BottomEdge)) {
        return Qt::SizeBDiagCursor;
    }
    if (edges & (Qt::LeftEdge | Qt::RightEdge)) return Qt::SizeHorCursor;
    if (edges & (Qt::TopEdge | Qt::BottomEdge)) return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}

bool startWindowResize(QWidget *window, Qt::Edges edges) {
    if (!edges || !window) return false;
    QWindow *handle = window->windowHandle();
    // The resize is the window manager's to carry out: it knows about
    // snapping, the other windows, and the pointer grab.
    return handle && handle->startSystemResize(edges);
}
