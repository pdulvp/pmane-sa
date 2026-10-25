#pragma once

#include <QSize>
#include <QVector>
#include <QString>
#include <QStringList>

// Running an old Windows program at a multiple of its own size.
//
// Wine gives a program the window size it asks for, and a program written for
// a 640x480 screen asks for a small one. Wine cannot be told to scale it, its
// DPI being recorded in the prefix's registry, so the program is run inside
// gamescope, which renders it at its own size and scales the result.

// Whether gamescope is installed. Nothing can be scaled without it.
bool gamescopeAvailable();

// The two ways of enlarging a program, which differ.
//
// Gamescope renders the program at its own size and scales the image. The
// program knows nothing about it, so everything grows together and nothing can
// be mislaid out, but the result is the same number of pixels enlarged.
//
// Wine's own scaling tells the program it is on a higher-resolution screen, via
// the DPI recorded in the prefix. The program then draws itself larger, with
// real pixels and sharp text, but only as well as it was written to: a Windows
// 3.x program may ignore the DPI, or honour it and misplace things.
enum class WindowScaleMode { Gamescope, WineDpi };

// One entry in the list of scales an item can be set to.
struct WindowScaleOption {
    int percent = 100;
    WindowScaleMode mode = WindowScaleMode::Gamescope;
};

// The scales to offer. `wineDpi` adds wine's own scaling to the list, which
// only the wine launcher supports, a Proton build handling scaling itself.
QVector<WindowScaleOption> windowScaleOptions(bool wineDpi);

// The label for a scale: 100 -> "1x", 110 -> "1.1x", with wine's own scaling
// named separately from gamescope's.
QString windowScaleLabel(int percent);
QString windowScaleLabel(const WindowScaleOption &option);

// The dots per inch to record in the prefix for a given scale. 96 is Windows'
// 100%, so 144 is 150% and 192 is double size.
//
// There is no environment variable to set alongside it: WINE_DPI_SCALE, often
// cited as the way to do this, appears nowhere in wine, neither in the system
// build nor in a wine-ge one, so the registry is the whole mechanism.
int wineLogPixels(int percent);

// The default dots per inch: Windows' 100%, and what a prefix uses until it is
// set otherwise.
constexpr int kDefaultLogPixels = 96;

// The command that writes a DPI into the prefix's registry. Wine reads it when
// a program starts, so it has to be set beforehand.
QStringList wineDpiRegistryArgs(int percent);
// The same, for a DPI given directly rather than as a scale.
QStringList wineLogPixelsRegistryArgs(int logPixels);

// The DPI a prefix records, or 0 when it records none, which means the default.
// Read directly from the prefix's user.reg, a text file, so the check is cheap;
// starting wine to query it would cost as much as setting it.
int prefixLogPixels(const QString &prefixDir);

// Stops the wineserver for a prefix, so the next program started there gets a
// fresh one. Wine reads the DPI when a server starts and keeps it, so without
// this a program started just after a DPI change still draws at the old one.
void stopWineserver(const QString &wineCommand, const QString &prefixDir);

// Makes `prefixDir` record `logPixels`, and stops its wineserver if anything
// changed. Does nothing when the prefix already matches, which starts no wine
// at all, the value being read from a text file.
//
// Both launching and measuring go through here: a program has to be measured at
// the DPI and in the prefix it will run with, or the size measured is not the
// size it draws.
void setPrefixDpi(const QString &wineCommand, const QString &prefixDir, int logPixels);

// Whether a prefix has been created: wine writes user.reg when it creates one.
bool prefixExists(const QString &prefixDir);

// Creates a prefix and waits for it to finish. Wine would create it anyway with
// the first program started there, but it opens windows of its own while doing
// so, including a rundll32 dialog nearly a screen wide, which must be gone
// before anything is measured.
void initialisePrefix(const QString &wineCommand, const QString &prefixDir);

// The resulting window size for a given scale.
QSize scaledSize(const QSize &native, int percent);

// The installed ReShade effects, named as gamescope expects: the .fx files in
// its shader directories, without the extension. Empty when none are installed;
// Debian ships none, so they are whatever has been added by hand.
QStringList availableShaders();

// The command that runs `program args...` at `percent` of `native`, through
// `shader` when one is named. Returns an empty list when there is nothing to
// do: no scaling and no shader requested, no known size, or no gamescope
// installed.
//
// A whole multiple is scaled by repeating pixels, which suits pixel artwork:
// doubled, it stays crisp rather than being smeared. A fractional scale cannot
// repeat pixels evenly, so it is filtered instead: softer, but without the
// uneven rows nearest-neighbour would leave at 1.5x. At an exact multiple the
// filter makes no difference, every output pixel being one input pixel.
QStringList gamescopeCommand(const QSize &native, int percent, const QString &shader,
                             const QString &program, const QStringList &args);

// Whether this is a 16-bit Windows program, an "NE" executable, as everything
// written for Windows 3.x is. Wine runs one of these inside winevdm rather than
// directly, which is why it matters here.
bool isWin16Executable(const QString &exePath);

// The window class wine gives a program: the name of its executable, or
// winevdm's for a 16-bit one, that being the program wine actually runs. A hint
// rather than a rule, since every 16-bit program shares the one class, so it
// breaks ties rather than deciding.
QString wineWindowClass(const QString &exePath);

// Every window currently on the display. Taken before a program is started, so
// the window it opens can be told from those already there: neither the class
// nor the process id is reliable for finding it. A reparenting window manager
// owns the frame, wine's own pid sits on a window two pixels across, and every
// 16-bit program's windows belong to winevdm rather than to the program.
QStringList windowSnapshot();

// Waits for a window that was not in `before` and returns its size, giving up
// after `timeoutMs`. Picks the largest new window, preferring one matching
// `classHint`: a program may open several windows, and the one that matters is
// the one it draws in.
//
// Only meaningful for a program started outside gamescope: inside one it has an
// X server of its own, which nothing here can see.
QSize measureNewWindow(const QStringList &before, const QString &classHint, int timeoutMs);
