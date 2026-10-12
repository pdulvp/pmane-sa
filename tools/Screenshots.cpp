// Takes the pictures README.md shows.
//
// Every one of them is the real widget, built the way the program builds it
// and photographed with QWidget::grab, so a dialog cannot drift away from its
// picture without the picture changing too. The desktop behind them is the
// one in docs/, the same that --configPath docs opens, so the groups and
// icons on show are the ones kept in the repository.
//
//   cmake --build build --target screenshots
//   QT_QPA_PLATFORM=offscreen ./build/screenshots docs
//
// It needs no display: the offscreen platform draws into memory, which is all
// a grab reads from.

#include <QApplication>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QListWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTextStream>
#include <QTimer>

#include "ApplicationPickerDialog.h"
#include "CalculatorPane.h"
#include "CalendarPane.h"
#include "GroupCaptionStyle.h"
#include "GroupWindow.h"
#include "IconListWidget.h"
#include "ItemPropertiesDialog.h"
#include "Kiosk.h"
#include "LaunchersDialog.h"
#include "MainWindow.h"
#include "ScreenManager.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "ThemeDialog.h"
#include "Version.h"

#include <algorithm>

namespace {

QString g_outDir;
int g_written = 0;

QTextStream &out() {
    static QTextStream stream(stdout);
    return stream;
}

// Qt settles on the bare "hicolor" theme when the desktop has not told it
// which to use. Same fallback the program itself picks, so the icons in the
// pictures are the icons on screen.
void ensureIconTheme() {
    const QString configured = QIcon::themeName();
    if (!configured.isEmpty() && configured != QLatin1String("hicolor")) return;

    const QStringList candidates = {QStringLiteral("Adwaita"), QStringLiteral("breeze"),
                                    QStringLiteral("oxygen"), QStringLiteral("Papirus"),
                                    QStringLiteral("gnome")};
    for (const QString &name : candidates) {
        for (const QString &dir : QIcon::themeSearchPaths()) {
            if (!QFileInfo::exists(dir + QLatin1Char('/') + name +
                                   QLatin1String("/index.theme"))) {
                continue;
            }
            QIcon::setFallbackThemeName(name);
            return;
        }
    }
}

// Long enough for a window to lay itself out, for an icon to be read out of
// the executable it lives in, and for a menu to open.
void settle(int rounds = 12) {
    for (int i = 0; i < rounds; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QCoreApplication::sendPostedEvents();
    }
}

void save(const QPixmap &shot, const QString &name) {
    if (shot.isNull()) {
        out() << "  !! " << name << " came out empty\n";
        return;
    }
    const QString path = g_outDir + QLatin1Char('/') + name + QStringLiteral(".png");
    if (!shot.save(path, "PNG")) {
        out() << "  !! " << name << " could not be written\n";
        return;
    }
    out() << QStringLiteral("  %1  %2x%3\n")
                 .arg(name + QStringLiteral(".png"), -26)
                 .arg(shot.width())
                 .arg(shot.height());
    ++g_written;
}

// A widget on its own: shown, left to settle, photographed, put away.
void shoot(QWidget *widget, const QString &name) {
    widget->show();
    settle();
    save(widget->grab(), name);
    widget->hide();
}

// Two menus as one picture. A submenu is a window of its own, so grabbing the
// parent menu alone would show the submenu's arrow and nothing it opens on;
// the two are photographed separately and laid out at the offset they stand
// at on screen.
QPixmap composite(const QList<QWidget *> &windows, const QColor &behind) {
    QRect bounds;
    for (QWidget *w : windows) bounds = bounds.united(w->geometry());
    if (bounds.isEmpty()) return {};

    QPixmap sheet(bounds.size());
    sheet.fill(behind);
    QPainter painter(&sheet);
    for (QWidget *w : windows) {
        painter.drawPixmap(w->geometry().topLeft() - bounds.topLeft(), w->grab());
    }
    painter.end();
    return sheet;
}

QMenu *menuTitled(QWidget *root, const QString &title) {
    for (QMenu *menu : root->findChildren<QMenu *>()) {
        if (menu->title() == title) return menu;
    }
    return nullptr;
}

// Grows the window until every group on the desktop is inside it. The screen
// file remembers a window size, but restoreGeometry refuses one larger than
// the screen it is being restored onto -- and the offscreen screen these are
// taken on is smaller than the desktop they were arranged on -- so the size
// is worked out from the groups themselves.
void fitToGroups(QMainWindow *window) {
    QMdiArea *mdi = window->findChild<QMdiArea *>();
    if (!mdi) return;

    QRect used;
    for (QMdiSubWindow *sub : mdi->subWindowList()) used = used.united(sub->geometry());
    if (!used.isValid()) return;

    // Room to the right of and below the furthest group, so the desktop does
    // not stop at the edge of the last caption.
    const int margin = 24;
    const int dx = used.right() + margin - mdi->viewport()->width();
    const int dy = used.bottom() + margin - mdi->viewport()->height();
    if (dx <= 0 && dy <= 0) return;
    window->resize(window->width() + std::max(0, dx), window->height() + std::max(0, dy));
}

// Photographs the next menu to open. A menu the program runs modally cannot
// be looked for after the fact -- exec() has torn it down by then, and under
// the offscreen platform it is torn down at once, having no input to wait for
// -- so it is grabbed as it is shown and then asked to close, which is what
// lets exec() return.
class MenuWatcher : public QObject {
public:
    void arm(const QString &name) { m_name = name; }
    bool armed() const { return !m_name.isEmpty(); }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (m_name.isEmpty() || event->type() != QEvent::Show) return false;
        QMenu *menu = qobject_cast<QMenu *>(watched);
        if (!menu) return false;

        // Sized and laid out by popup() before ever being shown, so there is
        // nothing to wait for: grab() paints it into the pixmap itself rather
        // than reading back anything drawn on a screen.
        menu->ensurePolished();
        save(menu->grab(), m_name);
        m_name.clear();
        QTimer::singleShot(0, menu, &QMenu::close);
        return false;
    }

private:
    QString m_name;
};

MenuWatcher *shotWatcher() {
    static MenuWatcher watcher;
    return &watcher;
}

// The items on every group of the open screen, in the order the groups sit in.
QVector<ProgramItem> everyItem(const QList<GroupWindow *> &groups) {
    QVector<ProgramItem> items;
    for (GroupWindow *group : groups) items += group->items();
    return items;
}

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    // The same identity the program runs under, so the launchers it reads out
    // of QSettings are the ones already set up rather than an empty set.
    QApplication::setOrganizationName("sansame-pmane");
    QApplication::setApplicationName("sansame-pmane");
    QApplication::setApplicationVersion(fullVersion());
    QApplication::setStyle(new GroupCaptionStyle);
    ensureIconTheme();

    const QStringList args = QCoreApplication::arguments();
    const QString configPath = args.size() > 1 ? args.at(1) : QStringLiteral("docs");
    g_outDir = configPath + QStringLiteral("/images");
    if (!QDir().mkpath(g_outDir)) {
        out() << "cannot write to " << g_outDir << "\n";
        return 1;
    }
    ScreenManager::useConfigPath(configPath);
    app.installEventFilter(shotWatcher());

    out() << "Screens from " << QFileInfo(configPath).absoluteFilePath() << "\n";

    // The desktop itself, at the size and on the screen the config names.
    // No screen asked for, so it opens the one the index says was last used.
    MainWindow window{QString()};
    window.show();
    settle(40);
    fitToGroups(&window);
    settle(20);
    save(window.grab(), QStringLiteral("main"));

    const QList<GroupWindow *> groups = window.findChildren<GroupWindow *>();
    const QVector<ProgramItem> items = everyItem(groups);
    out() << "  (" << groups.size() << " groups, " << items.size() << " items)\n";

    // Options > Launchers, told what the desktop holds so the prefixes it
    // lists say what still uses them.
    {
        LaunchersDialog dialog(&window);
        dialog.setOpenScreen(window.currentScreen(), items);
        shoot(&dialog, QStringLiteral("launchers"));
    }

    // An item's properties. A Windows program, which is the case with every
    // row in the dialog filled in.
    {
        ProgramItem subject;
        for (const ProgramItem &item : items) {
            if (!item.command.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) continue;
            subject = item;
            break;
        }
        ItemPropertiesDialog dialog(&window);
        dialog.setItem(subject);
        shoot(&dialog, QStringLiteral("item-properties"));
    }

    // File > New, with the submenu open under it.
    if (QMenu *fileMenu = menuTitled(&window, QObject::tr("&File"))) {
        QMenu *newMenu = menuTitled(fileMenu, QObject::tr("&New"));
        fileMenu->popup(QPoint(40, 40));
        settle();
        if (newMenu) {
            // Where a submenu actually opens: hard against the parent's right
            // edge, its first entry level with the row it belongs to.
            const QRect row = fileMenu->actionGeometry(newMenu->menuAction());
            newMenu->popup(QPoint(fileMenu->geometry().right() - 3,
                                  fileMenu->geometry().top() + row.top()));
            settle();
        }
        QList<QWidget *> both = {fileMenu};
        if (newMenu) both << newMenu;
        save(composite(both, currentTheme().desktop), QStringLiteral("new-menu"));
        if (newMenu) newMenu->hide();
        fileMenu->hide();
        settle(4);
    } else {
        out() << "  !! no File menu found\n";
    }

    // An item's own menu. The group builds it where it is shown and runs it
    // modally, so it is caught as it opens rather than looked for afterwards.
    if (!groups.isEmpty()) {
        QListWidget *list = groups.first()->findChild<QListWidget *>();
        if (list && list->count() > 0) {
            const QPoint where = list->visualItemRect(list->item(0)).center();
            shotWatcher()->arm(QStringLiteral("item-menu"));
            // The signal a right click on the list leads to. Sending a
            // context-menu event instead gets as far as the scroll area's
            // viewport and no further, which is not where the policy that
            // turns it into this signal lives.
            const bool asked = QMetaObject::invokeMethod(
                list, "customContextMenuRequested", Qt::DirectConnection, Q_ARG(QPoint, where));
            if (!asked || shotWatcher()->armed()) {
                out() << "  !! the item menu did not open\n";
            }
            settle(4);
        }
    }

    // The application picker, as File > New > Application opens it.
    {
        ApplicationPickerDialog dialog(&window);
        shoot(&dialog, QStringLiteral("insert-application"));
    }

    // Options > Colors, on the scheme the desktop is wearing.
    {
        ThemeDialog dialog(currentTheme().name, &window);
        shoot(&dialog, QStringLiteral("colors"));
    }

    // Options > Settings, with the kiosk switches at their defaults.
    {
        SettingsDialog dialog(false, false, KioskSettings{}, &window);
        shoot(&dialog, QStringLiteral("settings"));
    }

    // The two panes the strip along the top drops open.
    {
        CalendarPane calendar(&window);
        shoot(&calendar, QStringLiteral("calendar"));

        CalculatorPane calculator(&window);
        shoot(&calculator, QStringLiteral("calculator"));
    }

    out() << g_written << " pictures written to " << QFileInfo(g_outDir).absoluteFilePath()
          << "\n";
    out().flush();
    return 0;
}
