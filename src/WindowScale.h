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

// The two ways of making a program bigger, which are not the same thing.
//
// Gamescope renders the program at its own size and scales the picture: the
// program knows nothing about it, so everything grows together and nothing
// can be laid out wrongly. Blown up, though, it is the same number of pixels.
//
// Wine's own scaling tells the program it is on a higher-resolution screen,
// by WINE_DPI_SCALE and the DPI recorded in the prefix. The program then
// draws itself bigger -- real pixels, sharp text -- but only as well as it
// was written to: a program from Windows 3.x may not honour it at all, or
// may honour it and misplace things.
enum class WindowScaleMode { Gamescope, WineDpi };

// One entry in the list of scales an item can be set to.
struct WindowScaleOption {
    int percent = 100;
    WindowScaleMode mode = WindowScaleMode::Gamescope;
};

// The scales to offer. `wineDpi` adds wine's own scaling to the list, which
// only the wine launcher can do -- a Proton build handles its own.
QVector<WindowScaleOption> windowScaleOptions(bool wineDpi);

// How a scale reads to a person: 100 -> "1x", 110 -> "1.1x", and wine's own
// scaling named apart from gamescope's.
QString windowScaleLabel(int percent);
QString windowScaleLabel(const WindowScaleOption &option);

// What wine is told about the screen it is drawing on: the value of
// WINE_DPI_SCALE, and the dots per inch to record in the prefix. 96 is what
// Windows calls 100%, so a doubled program is 192.
QString wineDpiScaleValue(int percent);
int wineLogPixels(int percent);

// What dots per inch is the default -- what Windows calls 100%, and what a
// prefix that has never been told otherwise uses.
constexpr int kDefaultLogPixels = 96;

// The command that writes a DPI into the prefix's registry. Wine reads it
// when a program starts, so it has to be there before one does.
QStringList wineDpiRegistryArgs(int percent);
// The same, for a DPI given outright rather than as a scale.
QStringList wineLogPixelsRegistryArgs(int logPixels);

// The DPI a prefix currently records, or 0 if it records none -- which means
// the default. Read straight out of the prefix's user.reg, a text file, so
// that finding out costs nothing: starting wine to ask would cost as much as
// setting it.
int prefixLogPixels(const QString &prefixDir);

// Stops the wineserver looking after a prefix, so that the next program
// started there gets a fresh one. Wine reads the DPI as a server comes up
// and keeps it: without this, a program started just after the DPI changed
// still draws at the old one.
void stopWineserver(const QString &wineCommand, const QString &prefixDir);

// Makes `prefixDir` record `logPixels`, and stops its wineserver if anything
// had to change. Does nothing when the prefix already agrees, which costs no
// wine startup at all -- the value is read out of a text file.
//
// Both launching and measuring go through here: a program has to be measured
// at the DPI it will be run at, and in the prefix it will be run in, or the
// size measured is not the size it draws.
void setPrefixDpi(const QString &wineCommand, const QString &prefixDir, int logPixels);

// Whether a prefix has been built: wine writes user.reg as it builds one.
bool prefixExists(const QString &prefixDir);

// Builds a prefix, waiting for it to finish. Wine would do it anyway on the
// first program started there, but it puts up windows of its own while it
// does -- a rundll32 dialog the better part of a screen wide -- and those
// must be over and done with before anything is measured.
void initialisePrefix(const QString &wineCommand, const QString &prefixDir);

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
