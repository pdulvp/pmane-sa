#pragma once

#include <QSize>
#include <QVector>
#include <QString>
#include <QStringList>

// Running an old Windows program at a multiple of its own size.
//
// Wine hands a program the window it asks for, and a program written for a
// 640x480 screen asks for a small one. Scaling it is not something wine can
// be told to do -- its DPI lives in the prefix's registry, set when the
// prefix is built -- so the program is run inside gamescope instead, which
// renders it at its own size and scales the result on the way out.

// Whether gamescope is on this machine at all. Nothing can be scaled
// without it.
bool gamescopeAvailable();

// The scales on offer, as percentages: a whole number keeps the arithmetic
// exact where a fraction of a pixel would not.
const QVector<int> &windowScales();

// How a scale reads to a person: 100 -> "1x", 110 -> "1.1x".
QString windowScaleLabel(int percent);

// The size a window comes out at, which is what the scale is really for.
QSize scaledSize(const QSize &native, int percent);

// The command that runs `program args...` at `percent` of `native`, or an
// empty list when there is nothing to do: no scaling asked for, no size
// known, or no gamescope installed.
//
// A whole multiple is scaled by repeating pixels, which is what suits
// artwork drawn one pixel at a time: doubled, it stays crisp rather than
// being smeared. A fraction cannot repeat pixels evenly, so it is filtered
// instead -- softer, but without the uneven rows that nearest-neighbour
// would leave at 1.5x.
QStringList gamescopeCommand(const QSize &native, int percent, const QString &program,
                             const QStringList &args);

// Whether this is a 16-bit Windows program -- an "NE" executable, as
// everything written for Windows 3.x is. Wine runs one of these inside
// winevdm rather than directly, which is why it matters here.
bool isWin16Executable(const QString &exePath);

// The window class wine gives a program: the name of its executable, or
// winevdm's for a 16-bit one, since that is the program wine actually runs.
// A hint rather than a rule -- every 16-bit program shares the one class --
// so it breaks ties rather than deciding.
QString wineWindowClass(const QString &exePath);

// Every window on the display now. Taken before a program is started, so
// that the window it puts up can be told from everything already there:
// neither the class nor the process id can be relied on to find it. A
// reparenting window manager owns the frame, wine's own pid sits on a window
// two pixels across, and every 16-bit program's windows belong to winevdm
// rather than to the program.
QStringList windowSnapshot();

// Waits for a window that was not in `before` and answers how big it is,
// giving up after `timeoutMs`. The largest new one, preferring `classHint`
// where something matches it: a program puts up several windows and the one
// that matters is the one being drawn in.
//
// Only meaningful for a program started outside gamescope: inside one it has
// an X server of its own, where nothing here can see it.
QSize measureNewWindow(const QStringList &before, const QString &classHint, int timeoutMs);
