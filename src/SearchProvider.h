#pragma once

#include <QByteArray>
#include <QDBusArgument>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class MainWindow;

// An icon passed as pixels rather than as a path. The shell accepts a file path
// only when the file is an image, and an item's artwork is as often inside an
// executable, so the image itself is sent, in the form the shell expects: size,
// layout and rows of bytes.
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

// Publishes this application's program items to the desktop search, so the
// items in the Program Manager's groups can be found like an application or a
// file.
//
// The counterpart to DesktopShell.h: an application cannot type into the
// shell's search, but it can answer it. Activating a result here starts
// nothing; it brings the item into view and selects it, the desktop search
// being a way of locating something rather than another way of running it.
//
// It answers only while the application is running, the shell querying it over
// the session bus, which suits a result whose purpose is to show something in a
// window.
class SearchProvider : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gnome.Shell.SearchProvider2")

public:
    explicit SearchProvider(MainWindow *window);

    // Registers it on the session bus. False when the name or the object could
    // not be claimed, which is not reported to the user: the desktop simply
    // returns no results from here.
    bool registerOnBus();

public slots:
    // The shell's own interface. Results are identified by a string of this
    // application's choosing, which encodes where the item is so activating one
    // can go straight to it.
    QStringList GetInitialResultSet(const QStringList &terms);
    QStringList GetSubsearchResultSet(const QStringList &previous, const QStringList &terms);
    QList<QVariantMap> GetResultMetas(const QStringList &identifiers);
    void ActivateResult(const QString &identifier, const QStringList &terms, uint timestamp);
    void LaunchSearch(const QStringList &terms, uint timestamp);

private:
    MainWindow *m_window;
};
