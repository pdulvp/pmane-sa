#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class MainWindow;

// An icon handed over as pixels rather than as somewhere to find one. The
// shell takes a file path for an icon only when the file is an image, and
// an item's artwork is as often inside an executable -- so the picture
// itself goes across, in the shape the shell expects: size, layout, and the
// rows of bytes.
struct SearchIconData {
    int width = 0;
    int height = 0;
    int rowstride = 0;
    bool hasAlpha = true;
    int bitsPerSample = 8;
    int channels = 4;
    QByteArray pixels;
};
Q_DECLARE_METATYPE(SearchIconData)

QDBusArgument &operator<<(QDBusArgument &argument, const SearchIconData &icon);
const QDBusArgument &operator>>(const QDBusArgument &argument, SearchIconData &icon);

// Puts this application's program items into the desktop's own search, so
// that what is on the Program Manager's groups can be found the same way an
// application or a file is.
//
// The other half of the bargain GNOME offers: an application cannot type
// into the shell's search (see DesktopShell.h), but it can answer it. A
// result activated here does not start anything -- it brings the item into
// view and selects it, the desktop search being a way of finding where
// something is rather than another way of running it.
//
// Answers only while the application is running: the shell asks over the
// session bus, and there is nothing to ask when nothing is there. That suits
// a result whose whole purpose is to show something in a window.
class SearchProvider : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.Shell.SearchProvider2")

public:
    explicit SearchProvider(MainWindow *window);

    // Puts it on the session bus. False when the name or the object could
    // not be taken, which is not worth troubling anyone about: the desktop
    // simply returns no results from here.
    bool registerOnBus();

public slots:
    // The shell's own vocabulary. Results are identified by a string of our
    // choosing, which carries where the item sits so activating one can go
    // straight back to it.
    QStringList GetInitialResultSet(const QStringList &terms);
    QStringList GetSubsearchResultSet(const QStringList &previous, const QStringList &terms);
    QList<QVariantMap> GetResultMetas(const QStringList &identifiers);
    void ActivateResult(const QString &identifier, const QStringList &terms, uint timestamp);
    void LaunchSearch(const QStringList &terms, uint timestamp);

private:
    MainWindow *m_window;
};
