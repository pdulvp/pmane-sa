#include "MainWindow.h"

#include "SearchProvider.h"

#include "AlignmentGuides.h"
#include "DefaultIconsDialog.h"
#include "LaunchersDialog.h"
#include "TextPromptDialog.h"
#include "ApplicationPickerDialog.h"
#include "SteamGamePickerDialog.h"
#include "DesktopArea.h"
#include "GroupWindow.h"
#include "Autostart.h"
#include "Kiosk.h"
#include "LaunchNotices.h"
#include "MinimizedGroupIcon.h"
#include "NotificationArea.h"
#include "ScreenTransition.h"
#include "TitleBar.h"
#include "WindowEdges.h"
#include "Theme.h"
#include "Version.h"
#include "GnomeWallpaper.h"
#include "SettingsDialog.h"
#include "ThemeDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QContextMenuEvent>
#include <QKeySequence>
#include <QScrollBar>
#include <QLineEdit>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QMessageBox>

#include <functional>
#include <QScreen>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantAnimation>

namespace {

// Serializes one item. Kept beside readProgramItem() so the two stay in step;
// moving a group to another screen writes its items through here as well.
void writeProgramItem(QSettings &settings, const ProgramItem &item) {
    // Identity: distinguishes two icons for the same program, and names the
    // item's own wine prefix.
    settings.setValue("id", item.id);
    settings.setValue("name", item.name);
    // Only a user-chosen name is stored; a default one is derived on read.
    if (item.customName) settings.setValue("customName", true);
    settings.setValue("command", item.command);
    settings.setValue("arguments", item.arguments);
    // Absent for a plain wine item.
    if (!item.launcher.isEmpty()) settings.setValue("launcher", item.launcher);
    // Stored only when the item shares a named prefix; otherwise it uses its
    // own, derived from the id.
    if (!item.prefix.isEmpty()) settings.setValue("prefix", item.prefix);
    // Stored only when a core was picked for it; otherwise the launcher's
    // table decides by file type.
    if (!item.core.isEmpty()) settings.setValue("core", item.core);
    // Stored only when the user picked one of the several icons in the file.
    if (item.iconIndex >= 0) {
        settings.setValue("iconIndex", item.iconIndex);
        if (!item.iconSource.isEmpty()) settings.setValue("iconSource", item.iconSource);
    }
    if (item.xpStyleIcon) settings.setValue("xpStyleIcon", true);
    // Scaling settings are stored only when scaling is on. The measured
    // native size is stored either way, so toggling scaling off and on does
    // not force a new measurement.
    if (item.windowScalePercent > 100) {
        settings.setValue("windowScalePercent", item.windowScalePercent);
        if (item.windowScaleMode == WindowScaleMode::WineDpi) {
            settings.setValue("windowScaleMode", "wineDpi");
        }
    }
    if (!item.shader.isEmpty()) settings.setValue("shader", item.shader);
    if (item.nativeSize.isValid() && !item.nativeSize.isEmpty()) {
        settings.setValue("nativeSize", item.nativeSize);
    }
}

ProgramItem readProgramItem(const QSettings &settings) {
    ProgramItem item;
    // Empty for items saved before ids existed; completeProgramItem() assigns
    // one when the item is placed on the desktop.
    item.id = settings.value("id").toString();
    item.name = settings.value("name").toString();
    item.customName = settings.value("customName", false).toBool();
    item.command = settings.value("command").toString();
    item.arguments = settings.value("arguments").toString();
    item.launcher = settings.value("launcher").toString();
    item.prefix = settings.value("prefix").toString();
    item.core = settings.value("core").toString();
    item.iconIndex = settings.value("iconIndex", -1).toInt();
    item.iconSource = settings.value("iconSource").toString();
    item.xpStyleIcon = settings.value("xpStyleIcon", false).toBool();
    item.windowScalePercent = settings.value("windowScalePercent", 100).toInt();
    item.windowScaleMode = settings.value("windowScaleMode").toString() == "wineDpi"
                               ? WindowScaleMode::WineDpi
                               : WindowScaleMode::Gamescope;
    item.shader = settings.value("shader").toString();
    const QSize native = settings.value("nativeSize").toSize();
    if (native.isValid() && !native.isEmpty()) item.nativeSize = native;
    return item;
}

} // namespace

MainWindow::MainWindow(const QString &startupScreen, QWidget *parent) : QMainWindow(parent) {
    resize(900, 650);

    // No window manager decoration: the menu strip carries the title and the
    // caption buttons, and a margin along the other three edges serves as the
    // resize grip.
    setWindowFlag(Qt::FramelessWindowHint);
    setContentsMargins(kResizeMargin, 0, kResizeMargin, kResizeMargin);
    setMouseTracking(true);

    m_titleBar = new TitleBar(this);
    setMenuWidget(m_titleBar);

    m_mdiArea = new DesktopArea(this);
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    setCentralWidget(m_mdiArea);
    // Sibling of the group windows, so it can be raised above them.
    m_guides = new AlignmentGuides(m_mdiArea->viewport());

    // Launch notices, on the viewport like the drag guides so they draw over
    // the group windows.
    m_notices = new NotificationArea(m_mdiArea->viewport());
    connect(LaunchNotices::instance(), &LaunchNotices::posted, m_notices,
            &NotificationArea::post);

    // Scrolling moves every child of the viewport, so the minimized-group row
    // and the notice area have to be repositioned to stay in place.
    for (QScrollBar *bar : {m_mdiArea->horizontalScrollBar(), m_mdiArea->verticalScrollBar()}) {
        connect(bar, &QScrollBar::valueChanged, this, [this] {
            arrangeMinimizedIcons();
            m_notices->updatePosition();
        });
    }

    buildMenus();
    // --profile on the command line overrides the screen last in use.
    if (!startupScreen.isEmpty() && m_screens.contains(startupScreen)) {
        m_screens.setCurrent(startupScreen);
    }
    applyTheme(builtinThemes().first().name);
    load();
    rebuildScreenMenu();
    updateWindowTitle();

    // Registered after load(): the provider answers out of the groups
    // currently on screen.
    m_searchProvider = new SearchProvider(this);
    m_searchProvider->registerOnBus();

    // Coalesces a drag into a single undo entry without a noticeable delay.
    m_layoutSettle = new QTimer(this);
    m_layoutSettle->setSingleShot(true);
    m_layoutSettle->setInterval(400);
    connect(m_layoutSettle, &QTimer::timeout, this, &MainWindow::recordLayoutChange);
    m_layoutNow = currentLayout();
    updateEditActions();

    // Follow the desktop light/dark preference, now and on every change.
    m_schemeWatch = new DesktopColorSchemeWatch(this);
    connect(m_schemeWatch, &DesktopColorSchemeWatch::schemeChanged, this,
            &MainWindow::followDesktopScheme);
    followDesktopScheme(desktopColorScheme());

    // Same for the wallpaper, when the setting asks for it.
    m_wallpaperWatch = new GnomeWallpaper(this);
    connect(m_wallpaperWatch, &GnomeWallpaper::changed, this,
            &MainWindow::refreshDesktopWallpaper);
    refreshDesktopWallpaper();

    connect(m_titleBar, &TitleBar::darkModeToggled, this, [this](bool dark) {
        setDarkMode(dark);
        // Every palette derives from the scheme, so reapply the whole theme
        // rather than patch it; animated, since everything changes at once.
        applyTheme(m_themeName, true);
        save();
    });

    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::updateWindowMenu);
    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);
    m_mdiArea->viewport()->installEventFilter(this);
    // Above the group windows, since those are children of the same viewport.
    m_screenSlide = new ScreenTransition(m_mdiArea->viewport());
}

MainWindow::~MainWindow() {
    // Child widgets are destroyed after this body runs but before QObject
    // severs connections, so the area can still emit subWindowActivated into
    // a half-destroyed MainWindow. Cut it loose first.
    disconnect(m_mdiArea, nullptr, this, nullptr);
}

void MainWindow::buildMenus() {
    QMenuBar *bar = m_titleBar->menuBar();

    QMenu *fileMenu = bar->addMenu(tr("&File"));

    // Everything that adds to the desktop: an empty group, an empty item, and
    // the two that fill an item in from an installed application or game.
    QMenu *newMenu = fileMenu->addMenu(tr("&New"));
    newMenu->addAction(tr("Program &Group..."), this, &MainWindow::newGroup);
    newMenu->addAction(tr("&Item..."), this, &MainWindow::newItem);
    newMenu->addSeparator();
    newMenu->addAction(tr("&Application..."), this, &MainWindow::insertApplication);
    newMenu->addAction(tr("&Steam Game..."), this, &MainWindow::insertSteamGame);

    fileMenu->addAction(tr("&Rename Group..."), this, [this] { renameGroup(); });
    fileMenu->addAction(tr("&Delete Group"), this, [this] { deleteGroup(); });
    fileMenu->addSeparator();
    m_suspendAction = fileMenu->addAction(tr("S&uspend Computer"), this, [this] {
        requestSystemState(QStringLiteral("suspend"), tr("Suspend the computer?"), false);
    });
    m_restartAction = fileMenu->addAction(tr("&Restart Computer"), this, [this] {
        requestSystemState(QStringLiteral("reboot"),
                            tr("Restart the computer? Anything not saved, here or in another "
                               "application, will be lost."),
                            true);
    });
    m_shutdownAction = fileMenu->addAction(tr("Shut Do&wn Computer"), this, [this] {
        requestSystemState(QStringLiteral("poweroff"),
                            tr("Shut the computer down? Anything not saved, here or in another "
                               "application, will be lost."),
                            true);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);


    QMenu *optionsMenu = bar->addMenu(tr("&Options"));
    optionsMenu->addAction(tr("&Colors..."), this, &MainWindow::chooseColors);
    optionsMenu->addAction(tr("&Launchers..."), this, &MainWindow::editLaunchers);
    optionsMenu->addAction(tr("Default &Icons..."), this, &MainWindow::editDefaultIcons);
    optionsMenu->addAction(tr("&Settings..."), this, &MainWindow::editSettings);
    optionsMenu->addSeparator();
    m_saveOnExitAction = optionsMenu->addAction(tr("&Save Settings on Exit"));
    m_saveOnExitAction->setCheckable(true);
    m_saveOnExitAction->setChecked(true);

    m_screenMenu = bar->addMenu(tr("&Screens"));
    m_screenGroup = new QActionGroup(this);
    m_screenGroup->setExclusive(true);

    // Undo covers accidental drags, not the Window menu's arrange commands.
    // Owned by the window so the Window menu can be rebuilt around them.
    m_undoAction = new QAction(tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoEdit);

    m_redoAction = new QAction(tr("&Redo"), this);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(tr("Ctrl+Y"))});
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoEdit);

    // Added to the window as well as the menu, so the shortcuts work before
    // the menu has been opened once.
    addAction(m_undoAction);
    addAction(m_redoAction);
    updateEditActions();

    m_windowMenu = bar->addMenu(tr("&Window"));
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

    QMenu *helpMenu = bar->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About Program Manager..."), this, &MainWindow::about);
}

GroupWindow *MainWindow::activeGroup() const {
    QMdiSubWindow *sub = m_mdiArea->activeSubWindow();
    if (!sub) return nullptr;
    return qobject_cast<GroupWindow *>(sub->widget());
}

QMdiSubWindow *MainWindow::addGroupWindow(GroupWindow *group) {
    QMdiSubWindow *sub = m_mdiArea->addSubWindow(group);
    // A group is never closed, only minimized to an icon; it is removed
    // through File > Delete Group, which asks first. Hence no close box on
    // the caption, and the system menu's Close entry and its shortcut
    // disabled to match.
    if (QMenu *systemMenu = sub->systemMenu()) {
        for (QAction *action : systemMenu->actions()) {
            if (action->shortcut() == QKeySequence(QKeySequence::Close)) {
                action->setEnabled(false);
                action->setVisible(false);
            }
        }
    }
    if (QMenu *systemMenu = sub->systemMenu()) {
        // Right-clicking a group's caption opens this menu, so it is also
        // where the group is sent to another screen from.
        systemMenu->addSeparator();
        QMenu *move = systemMenu->addMenu(tr("Move to &Screen"));
        connect(systemMenu, &QMenu::aboutToShow, this,
                [this, move, sub] { fillMoveToScreenMenu(move, sub); });
    }

    sub->setWindowTitle(group->title());
    sub->resize(320, 240);
    connect(group, &GroupWindow::titleChanged, sub, &QMdiSubWindow::setWindowTitle);
    connect(group, &GroupWindow::itemSelected, this, &MainWindow::clearMinimizedIconSelection);
    connect(group, &GroupWindow::itemDeleted, this,
            [this, group](int index, const ProgramItem &item) {
                Edit edit;
                edit.kind = Edit::ItemDeleted;
                edit.group = group->id();
                edit.itemIndex = index;
                edit.item = item;
                record(edit);
            });

    sub->setPalette(captionPalette(sub->palette()));

    sub->installEventFilter(this);

    connect(sub, &QMdiSubWindow::windowStateChanged, this,
            [this, sub](Qt::WindowStates, Qt::WindowStates newState) {
                if (newState & Qt::WindowMinimized) minimizeToIcon(sub);
            });

    sub->show();
    // The remembered arrangements described a different set of windows.
    forgetArrangements();
    return sub;
}

void MainWindow::newGroup() {
    bool ok = false;
    const QString title = askForText(this, tr("New Program Group"), tr("New Group"),
                                      tr("Description:"), QString(), &ok);
    if (!ok || title.isEmpty()) return;
    addGroupWindow(new GroupWindow(title));
}

void MainWindow::newItem() {
    GroupWindow *gw = activeGroup();
    if (!gw) {
        QMessageBox::information(this, tr("New Item"),
                                  tr("Select or open a Program Group first."));
        return;
    }
    gw->newItemDialog();
}

void MainWindow::requestSystemState(const QString &verb, const QString &question,
                                    bool endsSession) {
    if (QMessageBox::question(this, tr("Program Manager"), question) != QMessageBox::Yes) return;

    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        QMessageBox::warning(this, tr("Program Manager"),
                              tr("Could not find systemctl, so the computer cannot be asked to "
                                 "do that from here."));
        return;
    }

    // closeEvent() will not run when the session ends, so save here or the
    // layout is lost.
    if (endsSession && m_saveOnExitAction->isChecked()) save();

    // Detached rather than waited on: on success the process outlives us, and
    // otherwise the desktop's own authorisation agent takes over the prompt.
    if (!QProcess::startDetached(systemctl, {verb})) {
        QMessageBox::warning(this, tr("Program Manager"),
                              tr("Could not run '%1 %2'.").arg(systemctl, verb));
    }
}

void MainWindow::insertApplication() {
    GroupWindow *gw = activeGroup();
    if (!gw) {
        QMessageBox::information(this, tr("Insert Application"),
                                  tr("Select or open a Program Group first."));
        return;
    }

    ApplicationPickerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    for (const ProgramItem &item : dialog.chosenItems()) gw->addProgramItem(item);
}

void MainWindow::insertSteamGame() {
    GroupWindow *gw = activeGroup();
    if (!gw) {
        QMessageBox::information(this, tr("Insert Steam Game"),
                                 tr("Select or open a Program Group first."));
        return;
    }

    SteamGamePickerDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    for (const ProgramItem &item : dialog.chosenItems()) gw->addProgramItem(item);
}

void MainWindow::removeGroupWindow(QMdiSubWindow *sub) {
    if (!sub) return;

    // A minimized group is represented by an icon along the bottom, which has
    // to be deleted with the window.
    if (MinimizedGroupIcon *icon = m_minimizedIcons.take(sub)) {
        m_minimizedOrder.removeAll(sub);
        icon->deleteLater();
    }
    m_normalGeometry.remove(sub);
    delete sub; // owns and deletes the GroupWindow child widget too
    arrangeMinimizedIcons();
    forgetArrangements();
}

void MainWindow::deleteGroup(QMdiSubWindow *sub) {
    if (!sub) sub = m_mdiArea->activeSubWindow();
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw) return;

    auto reply = QMessageBox::question(
        this, tr("Delete"), tr("Delete group '%1' and all its items?").arg(gw->title()));
    if (reply != QMessageBox::Yes) return;

    // Record the group and all of its items before deleting, since undo is
    // the only way back from one confirmation click.
    Edit edit;
    edit.kind = Edit::GroupDeleted;
    edit.group = gw->id();
    edit.title = gw->title();
    edit.items = gw->items();
    edit.geometry = m_normalGeometry.value(sub, sub->geometry());
    edit.minimized = sub->isMinimized() || m_minimizedIcons.contains(sub);

    // removeGroupWindow() also drops the remembered arrangements, which no
    // longer describe what is on screen.
    removeGroupWindow(sub);
    record(edit);
}

void MainWindow::renameGroup(QMdiSubWindow *sub) {
    if (!sub) sub = m_mdiArea->activeSubWindow();
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw) return;

    bool ok = false;
    const QString title = askForText(this, tr("Rename Program Group"), tr("Rename"),
                                      tr("Description:"), gw->title(), &ok);
    if (!ok || title.isEmpty() || title == gw->title()) return;
    // The caption and the minimized icon both follow titleChanged().
    gw->setTitle(title);
}

void MainWindow::showGroupCaptionMenu(QMdiSubWindow *sub, const QPoint &where) {
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw) return;

    // Built on demand rather than kept: screens are added, renamed and
    // deleted while the desktop is running.
    QMenu menu(this);
    QMenu *move = menu.addMenu(tr("Move to &Screen"));
    fillMoveToScreenMenu(move, sub);
    menu.addSeparator();
    menu.addAction(tr("&Rename Group..."), this, [this, sub] { renameGroup(sub); });
    menu.addAction(tr("&Delete Group"), this, [this, sub] { deleteGroup(sub); });
    menu.exec(where);
}

void MainWindow::fillMoveToScreenMenu(QMenu *menu, QMdiSubWindow *sub) {
    menu->clear();

    const QString here = m_screens.current();
    bool any = false;
    for (const QString &name : m_screens.names()) {
        if (name == here) continue;
        any = true;
        menu->addAction(name, this, [this, sub, name] { moveGroupToScreen(sub, name); });
    }

    if (any) return;
    // A disabled entry rather than an empty menu, which would not say why.
    QAction *none = menu->addAction(tr("No other screens"));
    none->setEnabled(false);
}

void MainWindow::moveGroupToScreen(QMdiSubWindow *sub, const QString &screen) {
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw || !m_screens.contains(screen) || screen == m_screens.current()) return;

    const QString path = m_screens.filePath(screen);
    if (path.isEmpty()) return;

    const QString groupId = gw->id();
    const QString title = gw->title();
    const QVector<ProgramItem> items = gw->items();
    const QRect geometry = m_normalGeometry.value(sub, sub->geometry());
    const bool minimized = sub->isMinimized() || m_minimizedIcons.contains(sub);

    // Written straight into the other screen's file, since that screen is not
    // loaded. Appended after the groups already there, like a new group.
    {
        QSettings settings(path, QSettings::IniFormat);
        const int existing = settings.beginReadArray("Groups");
        settings.endArray();

        settings.beginWriteArray("Groups", existing + 1);
        settings.setArrayIndex(existing);
        settings.setValue("id", groupId);
        settings.setValue("title", title);
        settings.setValue("x", geometry.x());
        settings.setValue("y", geometry.y());
        settings.setValue("w", geometry.width());
        settings.setValue("h", geometry.height());
        settings.setValue("minimized", minimized);
        settings.beginWriteArray("Items", items.size());
        for (int i = 0; i < items.size(); ++i) {
            settings.setArrayIndex(i);
            writeProgramItem(settings, items[i]);
        }
        settings.endArray();
        settings.endArray();
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            m_notices->post(tr("Could not move '%1'").arg(title),
                            tr("Nothing could be written to %1's file.").arg(screen));
            return;
        }
    }

    // Removed here only once the write succeeded: no undo entry can recover a
    // group from another screen's file.
    removeGroupWindow(sub);

    // Write this screen's file too, so disk and screen agree on where the
    // group lives.
    save();

    m_notices->post(tr("'%1' moved to %2").arg(title, screen),
                    tr("It is on that screen the next time you open it."));
}

void MainWindow::updateWindowMenu() {
    m_windowMenu->clear();

    // The active group's entries come first; the rest of the menu is about
    // the main window.
    if (QMdiSubWindow *active = m_mdiArea->activeSubWindow()) {
        if (qobject_cast<GroupWindow *>(active->widget())) {
            QMenu *move = m_windowMenu->addMenu(tr("Move Group to &Screen"));
            fillMoveToScreenMenu(move, active);
            m_windowMenu->addSeparator();
        }
    }

    // The way out of a maximized window, whose caption has no buttons while
    // it fills the screen. Disabled under a kiosk lock, which exists to
    // prevent exactly that; the lock is lifted in Options > Settings.
    QAction *restore = m_windowMenu->addAction(tr("&Restore"), this, &QWidget::showNormal);
    restore->setEnabled(isMaximized() && !kioskSettings().locked);
    m_windowMenu->addSeparator();

    // Added rather than created: this menu is cleared and rebuilt each time
    // it opens, and a menu deletes the actions it owns. These belong to the
    // window instead, so they keep the enabled state the undo history sets
    // and their shortcuts work whether or not the menu has been opened.
    m_windowMenu->addAction(m_undoAction);
    m_windowMenu->addAction(m_redoAction);
    m_windowMenu->addSeparator();
    m_windowMenu->addAction(tr("&Tile"), m_mdiArea, &QMdiArea::tileSubWindows);
    m_windowMenu->addSeparator();

    const QList<QMdiSubWindow *> subs = m_mdiArea->subWindowList();
    for (int i = 0; i < subs.size(); ++i) {
        GroupWindow *gw = qobject_cast<GroupWindow *>(subs[i]->widget());
        if (!gw) continue;
        QMdiSubWindow *sub = subs[i];
        QAction *act = m_windowMenu->addAction(tr("&%1 %2").arg(i + 1).arg(gw->title()));
        act->setCheckable(true);
        act->setChecked(sub == m_mdiArea->activeSubWindow());
        connect(act, &QAction::triggered, this, [this, sub]() {
            if (m_minimizedIcons.contains(sub)) {
                restoreFromIcon(sub);
                return;
            }
            m_mdiArea->setActiveSubWindow(sub);
            sub->showNormal();
        });
    }
}

void MainWindow::rebuildScreenMenu() {
    // The menu owns the per-screen actions, so clearing it deletes them and
    // removes them from the exclusive group.
    m_screenMenu->clear();

    const QStringList names = m_screens.names();
    for (int i = 0; i < names.size(); ++i) {
        const QString &name = names[i];
        QAction *act = m_screenMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(name == m_screens.current());
        // Ctrl+1..9 for the first nine screens, in menu order.
        if (i < 9) act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)));
        m_screenGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, name]() { switchToScreen(name); });
    }

    m_screenMenu->addSeparator();
    m_screenMenu->addAction(tr("&New Screen..."), this, &MainWindow::newScreen);
    m_screenMenu->addAction(tr("D&uplicate Screen..."), this, &MainWindow::duplicateScreen);
    m_screenMenu->addAction(tr("&Rename Screen..."), this, &MainWindow::renameScreen);
    QAction *del = m_screenMenu->addAction(tr("&Delete Screen..."), this,
                                            &MainWindow::deleteScreen);
    // At least one screen must remain, so the last one cannot be deleted.
    del->setEnabled(names.size() > 1);
    m_screenMenu->addSeparator();
    m_screenMenu->addAction(tr("&Save Screen Now"), QKeySequence::Save, this,
                             &MainWindow::save);
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(tr("Program Manager - %1").arg(m_screens.current()));
    m_mdiArea->setScreenName(m_screens.current());
}

QString MainWindow::askScreenName(const QString &windowTitle, const QString &initial) {
    QString suggestion = initial;
    forever {
        bool ok = false;
        const QString name = askForText(this, windowTitle, tr("Screen"),
                                         tr("Screen name:"), suggestion, &ok);
        if (!ok || name.isEmpty()) return {};
        // Keeping the name a rename dialog opened with is not a clash.
        if (!m_screens.contains(name) || name == initial) return name;

        QMessageBox::warning(this, windowTitle,
                             tr("A screen named '%1' already exists.").arg(name));
        suggestion = name;
    }
}

void MainWindow::clearDesktop() {
    for (MinimizedGroupIcon *icon : m_minimizedIcons) delete icon;
    m_minimizedIcons.clear();
    m_minimizedOrder.clear();

    // Deleted rather than closed: closeAllSubWindows() only requests a close,
    // and the windows would still be alive when the next screen's groups are
    // created.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) delete sub;
    // After the deletions, so a resize during teardown cannot record geometry
    // for a window that is already gone.
    m_normalGeometry.clear();

    // The undo history refers to groups that are no longer on the desktop.
    forgetEdits();
}

void MainWindow::followDesktopScheme(DesktopScheme scheme) {
    // When the desktop states no preference, the toggle in the menu strip
    // decides on its own.
    if (scheme == DesktopScheme::Unknown) return;

    const bool wantsDark = scheme == DesktopScheme::Dark;
    if (wantsDark == darkMode()) return;

    setDarkMode(wantsDark);
    // Animated, like the toggle in the menu strip: everything changes at once.
    applyTheme(m_themeName, true);
    save();
}

QString MainWindow::currentScreen() const {
    return m_screens.current();
}

QVector<GroupWindow *> MainWindow::groups() const {
    QVector<GroupWindow *> found;
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (auto *group = qobject_cast<GroupWindow *>(sub->widget())) found.append(group);
    }
    return found;
}

bool MainWindow::revealItem(const QString &screen, int groupIndex, int itemIndex,
                            const QString &itemName) {
    // Switch screens first: a group on another screen is not loaded yet.
    if (!screen.isEmpty() && screen != m_screens.current()) switchToScreen(screen);

    const QVector<GroupWindow *> all = groups();
    GroupWindow *group = groupIndex >= 0 && groupIndex < all.size() ? all[groupIndex] : nullptr;

    // The desktop may have been rearranged since the search index was built,
    // so the name at that position is checked before it is trusted.
    const auto itemAt = [](GroupWindow *candidate, int index) {
        return candidate && index >= 0 && index < candidate->items().size()
                   ? candidate->items()[index].name
                   : QString();
    };
    // No group at that position, or a different item there now: fall back to
    // searching by name. The null group is tested separately, because an
    // unnamed item would otherwise compare equal and skip the search.
    if (!group || itemAt(group, itemIndex) != itemName) {
        group = nullptr;
        for (GroupWindow *candidate : all) {
            const QVector<ProgramItem> &items = candidate->items();
            for (int i = 0; i < items.size(); ++i) {
                if (items[i].name != itemName) continue;
                group = candidate;
                itemIndex = i;
                break;
            }
            if (group) break;
        }
    }
    if (!group) return false;

    // Bring the containing window out of whatever state it is in and make it
    // the active one.
    if (auto *sub = qobject_cast<QMdiSubWindow *>(group->parentWidget())) {
        if (sub->isMinimized() || !sub->isVisible()) sub->showNormal();
        m_mdiArea->setActiveSubWindow(sub);
        sub->raise();
    }
    group->selectItem(itemIndex);

    // Then the application itself, since this result reveals the item rather
    // than launching it.
    if (isMinimized()) showNormal();
    raise();
    activateWindow();
    return true;
}

void MainWindow::switchToScreen(const QString &name) {
    if (!m_screens.contains(name) || name == m_screens.current()) {
        rebuildScreenMenu(); // re-check the radio button a stray click moved
        return;
    }

    // Which way the slide runs: a screen further down the menu arrives from
    // the right, one further up from the left.
    const QStringList names = m_screens.names();
    const int direction = names.indexOf(name) > names.indexOf(m_screens.current()) ? 1 : -1;

    // The desktop as it stands, taken before anything is torn down. The
    // overlay is a child of the viewport, so it is kept out of the picture.
    QPixmap outgoing;
    if (m_screenSlide && isVisible()) {
        m_screenSlide->hide();
        outgoing = m_mdiArea->viewport()->grab();
        // Held over the desktop for the swap below, which empties it and
        // fills it again. Any of that which reaches the screen is covered.
        m_screenSlide->hold(outgoing);
    }

    save();
    clearDesktop();
    m_screens.setCurrent(name);
    load();
    rebuildScreenMenu();
    updateWindowTitle();

    if (!outgoing.isNull()) {
        // The group windows just created are stacked above the overlay, which
        // has to stay in front of them for the rest of this.
        m_screenSlide->raise();

        // A theme of its own on the screen arriving is applied through
        // qApp->setPalette(), which posts its change events rather than
        // sending them. Delivered here, so the picture taken next is the new
        // desktop in its own colours instead of the old ones. Only what is
        // already posted is delivered: no user input, no timers, so the
        // switch cannot be re-entered from here. The repaint it also delivers
        // lands under the overlay.
        QCoreApplication::sendPostedEvents();

        // The new desktop is already built and laid out; grab() paints it
        // without waiting for the repaint it has queued. Hidden for the grab
        // so it is not in its own picture, and shown again by run() before
        // the event loop is reached, so the desktop is never uncovered.
        m_screenSlide->hide();
        const QPixmap incoming = m_mdiArea->viewport()->grab();
        m_screenSlide->run(outgoing, incoming, direction);
    }
}

void MainWindow::stepToNeighbourScreen(int step) {
    const QStringList names = m_screens.names();
    const int next = names.indexOf(m_screens.current()) + step;
    if (next < 0 || next >= names.size()) return;
    switchToScreen(names[next]);
}

void MainWindow::newScreen() {
    const QString name = askScreenName(tr("New Screen"), QString());
    if (name.isEmpty() || !m_screens.create(name)) return;

    save();
    clearDesktop();
    m_screens.setCurrent(name);
    // Nothing saved under this name yet, so load() produces an empty desktop.
    load();
    rebuildScreenMenu();
    updateWindowTitle();
}

void MainWindow::duplicateScreen() {
    const QString source = m_screens.current();
    // The copy is made from disk, so write the current screen out first.
    save();

    const QString name = askScreenName(tr("Duplicate Screen"),
                                        tr("%1 (copy)").arg(source));
    if (name.isEmpty() || !m_screens.duplicate(source, name)) return;

    m_screens.setCurrent(name);
    rebuildScreenMenu();
    updateWindowTitle();
}

void MainWindow::renameScreen() {
    const QString from = m_screens.current();
    const QString to = askScreenName(tr("Rename Screen"), from);
    if (to.isEmpty() || to == from) return;
    if (!m_screens.rename(from, to)) return;

    rebuildScreenMenu();
    updateWindowTitle();
}

void MainWindow::deleteScreen() {
    const QString name = m_screens.current();
    if (m_screens.names().size() <= 1) {
        QMessageBox::information(this, tr("Delete Screen"),
                                 tr("'%1' is the only screen; there has to be one.").arg(name));
        return;
    }

    const auto reply = QMessageBox::question(
        this, tr("Delete Screen"),
        tr("Delete screen '%1' and all the groups in it?").arg(name));
    if (reply != QMessageBox::Yes) return;

    if (!m_screens.remove(name)) return;

    // remove() has already made a surviving screen the current one.
    clearDesktop();
    load();
    rebuildScreenMenu();
    updateWindowTitle();
}

QPalette MainWindow::captionPalette(const QPalette &base) const {
    const Theme &theme = themeByName(m_themeName);
    QPalette pal = base;
    pal.setColor(QPalette::Highlight, theme.titleActive);
    pal.setColor(QPalette::HighlightedText, theme.titleActiveText);
    pal.setColor(QPalette::Dark, theme.titleInactive);
    return pal;
}

void MainWindow::minimizeToIcon(QMdiSubWindow *sub) {
    if (m_minimizedIcons.contains(sub)) return;

    auto *group = qobject_cast<GroupWindow *>(sub->widget());
    if (!group) return;

    auto *icon = new MinimizedGroupIcon(group->title(), m_mdiArea->viewport());
    icon->setPalette(captionPalette(palette()));
    icon->setDesktopColor(themeByName(m_themeName).desktop);

    // Preview the group by its first few program icons.
    QVector<QIcon> preview;
    for (const ProgramItem &item : group->items()) {
        if (preview.size() >= MinimizedGroupIcon::kMaxPreviewIcons) break;
        preview.append(item.icon);
    }
    icon->setPreviewIcons(preview);
    connect(icon, &MinimizedGroupIcon::restoreRequested, this,
            [this, sub]() { restoreFromIcon(sub); });
    // The same menu the caption carries when the group is open: a minimized
    // group can still be renamed, moved to another screen or deleted.
    connect(icon, &MinimizedGroupIcon::contextMenuRequested, this,
            [this, sub](const QPoint &where) { showGroupCaptionMenu(sub, where); });
    connect(icon, &MinimizedGroupIcon::clicked, this, [this, icon]() {
        // Only the icon just clicked stays selected, here or in any group.
        clearGroupSelections();
        for (MinimizedGroupIcon *other : m_minimizedIcons) {
            if (other != icon) other->setSelected(false);
        }
    });
    connect(group, &GroupWindow::titleChanged, icon, &MinimizedGroupIcon::setTitle);

    m_minimizedIcons.insert(sub, icon);
    m_minimizedOrder.append(sub);

    // Hiding is deferred one event loop turn: this runs from inside
    // showMinimized(), which would show the stub again right after we hid it.
    // The window keeps its minimized state while hidden, so a later
    // showNormal() restores its previous geometry.
    QMetaObject::invokeMethod(
        sub, [sub]() { sub->hide(); }, Qt::QueuedConnection);
    arrangeMinimizedIcons();
}

void MainWindow::restoreFromIcon(QMdiSubWindow *sub) {
    MinimizedGroupIcon *icon = m_minimizedIcons.take(sub);
    if (!icon) return;
    m_minimizedOrder.removeAll(sub);
    icon->deleteLater();

    // Clear the minimized flag before showing. show() on a still-minimized
    // window puts the stub back and re-fires windowStateChanged, which would
    // turn it straight back into a desktop icon.
    sub->setWindowState(sub->windowState() & ~Qt::WindowMinimized);
    sub->show();
    if (m_normalGeometry.contains(sub)) sub->setGeometry(m_normalGeometry.value(sub));
    m_mdiArea->setActiveSubWindow(sub);
    arrangeMinimizedIcons();
}

void MainWindow::arrangeMinimizedIcons() {
    const int cellW = MinimizedGroupIcon::kCellWidth;
    const int cellH = MinimizedGroupIcon::kCellHeight;
    const QRect area = m_mdiArea->viewport()->rect();

    // Laid out left to right along the bottom, stacking upwards, as in
    // Progman.
    int x = 4;
    int y = area.height() - cellH - 4;
    for (QMdiSubWindow *sub : m_minimizedOrder) {
        MinimizedGroupIcon *icon = m_minimizedIcons.value(sub);
        if (!icon) continue;
        if (x + cellW > area.width() && x > 4) {
            x = 4;
            y -= cellH;
        }
        icon->move(x, y);
        icon->show();
        icon->raise();
        x += cellW;
    }
}

void MainWindow::clearMinimizedIconSelection() {
    for (MinimizedGroupIcon *icon : m_minimizedIcons) icon->setSelected(false);
}

void MainWindow::clearGroupSelections() {
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (auto *group = qobject_cast<GroupWindow *>(sub->widget())) group->clearSelection();
    }
}

MainWindow::LayoutSnapshot MainWindow::currentLayout() const {
    LayoutSnapshot layout;
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        // The restored geometry, not the icon's position: undoing a move has
        // to work whether or not the group is minimized at the time.
        layout.geometry.append(m_normalGeometry.value(sub, sub->geometry()));
        layout.minimized.append(sub->isMinimized() || m_minimizedIcons.contains(sub));
    }
    return layout;
}

void MainWindow::applyLayout(const LayoutSnapshot &layout) {
    const QList<QMdiSubWindow *> windows = m_mdiArea->subWindowList();
    if (windows.size() != layout.geometry.size()) return;

    m_applyingLayout = true;
    for (int i = 0; i < windows.size(); ++i) {
        QMdiSubWindow *sub = windows[i];
        const bool wasMinimized = sub->isMinimized() || m_minimizedIcons.contains(sub);
        if (wasMinimized && !layout.minimized[i]) sub->showNormal();

        m_normalGeometry[sub] = layout.geometry[i];
        if (!layout.minimized[i]) sub->setGeometry(layout.geometry[i]);
        if (!wasMinimized && layout.minimized[i]) sub->showMinimized();
    }
    m_applyingLayout = false;

    m_layoutNow = currentLayout();
    updateEditActions();
}

void MainWindow::recordLayoutChange() {
    if (m_applyingLayout) return;

    const LayoutSnapshot now = currentLayout();
    // Only a change that actually moved something counts; selecting a group
    // also produces geometry events.
    if (now.geometry.size() != m_layoutNow.geometry.size() || now == m_layoutNow) {
        m_layoutNow = now;
        return;
    }

    Edit edit;
    edit.kind = Edit::Arrangement;
    edit.before = m_layoutNow;
    edit.after = now;
    m_layoutNow = now;
    record(edit);
}

void MainWindow::record(const Edit &edit) {
    m_undo.append(edit);
    // Deep enough to recover from a mistake and still cheap: fifty
    // arrangements of a dozen groups is a few thousand integers.
    constexpr int kMaxRemembered = 50;
    while (m_undo.size() > kMaxRemembered) m_undo.removeFirst();
    // A new edit starts a new branch, so the redo stack no longer applies.
    m_redo.clear();
    updateEditActions();
}

GroupWindow *MainWindow::groupById(const QString &id) const {
    if (id.isEmpty()) return nullptr;
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        auto *group = qobject_cast<GroupWindow *>(sub->widget());
        if (group && group->id() == id) return group;
    }
    return nullptr;
}

void MainWindow::undoEdit() {
    if (m_undo.isEmpty()) return;
    const Edit edit = m_undo.takeLast();
    applyEdit(edit, true);
    m_redo.append(edit);
    updateEditActions();
}

void MainWindow::redoEdit() {
    if (m_redo.isEmpty()) return;
    const Edit edit = m_redo.takeLast();
    applyEdit(edit, false);
    m_undo.append(edit);
    updateEditActions();
}

void MainWindow::applyEdit(const Edit &edit, bool undoing) {
    switch (edit.kind) {
    case Edit::Arrangement:
        applyLayout(undoing ? edit.before : edit.after);
        return;

    case Edit::ItemDeleted: {
        GroupWindow *group = groupById(edit.group);
        // The group the item belonged to has since been deleted; restoring it
        // into some other group would be worse than doing nothing.
        if (!group) return;
        if (undoing) {
            group->insertProgramItem(edit.item, edit.itemIndex);
        } else {
            group->removeProgramItem(edit.itemIndex);
        }
        return;
    }

    case Edit::GroupDeleted:
        if (undoing) {
            auto *group = new GroupWindow(edit.title);
            // Restored under its original id, so the other recorded edits
            // still refer to it.
            group->setId(edit.group);
            for (const ProgramItem &item : edit.items) group->addProgramItem(item);
            QMdiSubWindow *sub = addGroupWindow(group);
            sub->setGeometry(edit.geometry);
            m_normalGeometry[sub] = edit.geometry;
            if (edit.minimized) minimizeToIcon(sub);
            // It comes back at the end of the window order rather than at its
            // old position, so the recorded arrangements no longer line up;
            // addGroupWindow() has already dropped them.
        } else if (GroupWindow *group = groupById(edit.group)) {
            removeGroupWindow(qobject_cast<QMdiSubWindow *>(group->parentWidget()));
        }
        return;
    }
}

void MainWindow::updateEditActions() {
    // The action is named after the edit it will reverse, so "Undo Delete
    // Group" says what a bare "Undo" would not.
    const auto name = [this](const Edit &edit) {
        switch (edit.kind) {
        case Edit::Arrangement: return tr("Arrangement");
        case Edit::ItemDeleted: return tr("Delete Item");
        case Edit::GroupDeleted: return tr("Delete Group");
        }
        return QString();
    };

    if (m_undoAction) {
        m_undoAction->setEnabled(!m_undo.isEmpty());
        m_undoAction->setText(m_undo.isEmpty() ? tr("&Undo")
                                               : tr("&Undo %1").arg(name(m_undo.last())));
    }
    if (m_redoAction) {
        m_redoAction->setEnabled(!m_redo.isEmpty());
        m_redoAction->setText(m_redo.isEmpty() ? tr("&Redo")
                                               : tr("&Redo %1").arg(name(m_redo.last())));
    }
}

void MainWindow::forgetArrangements() {
    // Deletions are kept: each one records everything it removed and where it
    // came from, so none of them depends on the current window positions.
    const auto isArrangement = [](const Edit &edit) { return edit.kind == Edit::Arrangement; };
    m_undo.removeIf(isArrangement);
    m_redo.removeIf(isArrangement);
    m_layoutNow = currentLayout();
    updateEditActions();
}

void MainWindow::forgetEdits() {
    m_undo.clear();
    m_redo.clear();
    m_layoutNow = currentLayout();
    updateEditActions();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_mdiArea->viewport()) {
        if (event->type() == QEvent::Resize) arrangeMinimizedIcons();
        // A wheel tilted left or right over the bare desktop steps through the
        // screens. The vertical wheel is left to the MDI area, which scrolls
        // the arrangement of group windows with it.
        if (event->type() == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            const int sideways = wheel->angleDelta().x();
            if (sideways == 0) return QMainWindow::eventFilter(watched, event);

            // A reversal is a new gesture, not a continuation of the last one.
            if ((sideways > 0) != (m_wheelTravel > 0)) m_wheelTravel = 0;
            m_wheelTravel += sideways;

            // One notch, one screen, and no second step while the slide from
            // the first is still running.
            const int kNotch = 120;
            if (qAbs(m_wheelTravel) >= kNotch &&
                !(m_screenSlide && m_screenSlide->running())) {
                // angleDelta().x() is positive when the wheel is tilted left,
                // which goes back through the menu.
                stepToNeighbourScreen(m_wheelTravel > 0 ? -1 : 1);
                m_wheelTravel = 0;
            }
            return true;
        }
        // A press that reaches the viewport landed on bare desktop; an icon
        // consumes its own presses.
        if (event->type() == QEvent::MouseButtonPress) {
            clearMinimizedIconSelection();
            clearGroupSelections();
            // Deactivate the group too, so no caption stays highlighted after
            // a click on bare desktop.
            m_mdiArea->setActiveSubWindow(nullptr);
        }
    }

    if (auto *sub = qobject_cast<QMdiSubWindow *>(watched)) {
        // A right click on a group's caption. Qt opens the system menu only
        // from the small box at its left, so the rest of the caption is handled
        // here, and the menu shown offers what can be done to the group rather
        // than to the window.
        if (event->type() == QEvent::ContextMenu) {
            auto *menu = static_cast<QContextMenuEvent *>(event);
            // Anything above the group's widget is the caption or the frame;
            // the client area handles its own context menu.
            const QWidget *client = sub->widget();
            if (client && menu->pos().y() < client->geometry().top()) {
                showGroupCaptionMenu(sub, menu->globalPos());
                return true;
            }
        }

        // A drag of this group has begun. Which edges it moves, and so which
        // are candidates for snapping, is deduced later from the geometry
        // change.
        if (event->type() == QEvent::MouseButtonPress) {
            m_resizingSub = sub;
            m_resizeStartGeometry = sub->geometry();
            QVector<int> xs;
            QVector<int> ys;
            for (QMdiSubWindow *other : m_mdiArea->subWindowList()) {
                if (other == sub || !other->isVisible()) continue;
                if (other->isMinimized() || m_minimizedIcons.contains(other)) continue;
                const QRect rect = other->geometry();
                xs << rect.left() << rect.right() + 1;
                ys << rect.top() << rect.bottom() + 1;
            }
            m_guides->setGuides(xs, ys);
        }
        // On release, snap to the guide that is showing even if the pointer
        // is still a few pixels off it.
        if (event->type() == QEvent::MouseButtonRelease) {
            // Only one of the two applies; each returns early according to
            // whether the size changed.
            snapWhileMoving(sub, AlignmentGuides::kShowDistance);
            snapWhileResizing(sub, AlignmentGuides::kShowDistance);
            m_resizingSub = nullptr;
            m_guides->clear();
        }
        // Each step of a resize arrives as a resize event. Tiling and loading
        // a screen resize windows too, so only resizes with the left button
        // down are snapped. The snap is deferred until this resize has
        // finished, because a geometry set from inside the subwindow's own
        // resize handling does not survive it; `sub` as the context object
        // drops the call if the group is closed in between.
        if (event->type() == QEvent::Resize &&
            (QApplication::mouseButtons() & Qt::LeftButton)) {
            QTimer::singleShot(0, sub, [this, sub] { snapWhileResizing(sub); });
        }
        // Each step of a move arrives as a move event. Loading a screen and
        // undoing an arrangement also move windows, hence the button test.
        if (event->type() == QEvent::Move &&
            (QApplication::mouseButtons() & Qt::LeftButton)) {
            QTimer::singleShot(0, sub, [this, sub] { snapWhileMoving(sub); });
        }

        // A group's caption has no maximize box, so a double click on it
        // renames the group instead. Elsewhere on the subwindow, where the
        // contents have already had the event, it does nothing.
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QWidget *contents = sub->widget();
            if (mouse->button() == Qt::LeftButton && contents &&
                mouse->position().y() < contents->geometry().top()) {
                renameGroup(sub);
            }
            return true;
        }
        // Should a group be maximized by another route, such as the system
        // menu, restore it at once.
        if (event->type() == QEvent::WindowStateChange && sub->isMaximized()) {
            sub->showNormal();
            return true;
        }
    }

    if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
        if (auto *sub = qobject_cast<QMdiSubWindow *>(watched)) {
            const bool collapsed = sub->isMinimized() || m_minimizedIcons.contains(sub);
            if (!collapsed && sub->isVisible()) {
                m_normalGeometry[sub] = sub->geometry();
            }
            // A drag produces a stream of these events; the arrangement is
            // recorded once movement stops, so one drag is one undo entry.
            if (!m_applyingLayout && m_layoutSettle) m_layoutSettle->start();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::snapWhileMoving(QMdiSubWindow *sub, int reach) {
    if (sub != m_resizingSub || m_snapping) return;

    // Shift keeps the position exactly under the pointer: the guides stay
    // drawn as a visual aid, but nothing snaps to them.
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) reach = -1;

    const QRect rect = sub->geometry();
    // Moves only; a size change is the resize path's business.
    if (rect.size() != m_resizeStartGeometry.size()) return;

    const QRect held = m_guides->applyMove(rect, reach);
    if (held == rect) return;

    m_snapping = true;
    sub->setGeometry(held);
    m_snapping = false;
}

void MainWindow::snapWhileResizing(QMdiSubWindow *sub, int reach) {
    if (sub != m_resizingSub || m_snapping) return;

    // Shift keeps the size exactly under the pointer. A negative reach leaves
    // the guides drawn, as a visual aid, but makes nothing snap to them.
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) reach = -1;

    const QRect rect = sub->geometry();
    // A move shifts all four edges at once; snapping two opposite edges to
    // different guides would resize a window that was only being moved.
    if (rect.size() == m_resizeStartGeometry.size()) return;

    Qt::Edges edges;
    if (rect.left() != m_resizeStartGeometry.left()) edges |= Qt::LeftEdge;
    if (rect.right() != m_resizeStartGeometry.right()) edges |= Qt::RightEdge;
    if (rect.top() != m_resizeStartGeometry.top()) edges |= Qt::TopEdge;
    if (rect.bottom() != m_resizeStartGeometry.bottom()) edges |= Qt::BottomEdge;
    if (!edges) return;

    const QSize minimum = sub->minimumSizeHint().expandedTo(sub->minimumSize());
    const QRect held = m_guides->apply(rect, edges, minimum, reach);
    if (held == rect) return;

    m_snapping = true;
    sub->setGeometry(held);
    m_snapping = false;
}

void MainWindow::onSubWindowActivated(QMdiSubWindow *active) {
    // Only the active group keeps a highlighted icon, so there is never more
    // than one selection on screen.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (sub == active) continue;
        if (auto *group = qobject_cast<GroupWindow *>(sub->widget())) {
            group->clearSelection();
        }
    }
    if (active) clearMinimizedIconSelection();
}

void MainWindow::applyTheme(const QString &themeName, bool moved) {
    // The animation starts from the colours on screen, not from the scheme:
    // dark mode is already switched by the time this runs, so the scheme
    // would give the destination and there would be nothing to animate.
    const Theme from = m_wornTheme;

    m_themeName = themeByName(themeName).name;
    // Published so the dialogs, which paint surfaces no palette role covers,
    // can read the theme; it also resolves to the light or dark variant
    // actually in use.
    setCurrentTheme(m_themeName);
    const Theme to = currentTheme();

    if (m_themeFade) m_themeFade->stop();

    // Nothing to animate before the window is shown, between identical
    // colours, or when the caller did not ask for it.
    if (!moved || !isVisible() || !from.windowBg.isValid() ||
        from.windowBg == to.windowBg) {
        applyThemeColours(to);
        refreshDesktopWallpaper();
        return;
    }

    if (!m_themeFade) {
        m_themeFade = new QVariantAnimation(this);
        // Linear: easing slows at both ends, which on a flat colour looks
        // like hesitation.
        m_themeFade->setEasingCurve(QEasingCurve::Linear);
    }
    m_themeFade->setStartValue(0.0);
    m_themeFade->setEndValue(1.0);
    // Long enough to read as a transition, short enough not to delay anyone.
    m_themeFade->setDuration(220);
    m_themeFade->disconnect(this);
    connect(m_themeFade, &QVariantAnimation::valueChanged, this,
            [this, from, to](const QVariant &value) {
                applyThemeColours(blendThemes(from, to, value.toReal()));
            });
    connect(m_themeFade, &QVariantAnimation::finished, this,
            [this, to] { applyThemeColours(to); });
    // Once, at the start: the wallpaper does not fade with the colours, and
    // re-reading the setting every frame would be wasteful.
    refreshDesktopWallpaper();
    m_themeFade->start();
}

void MainWindow::applyThemeColours(const Theme &theme) {
    setCurrentThemeColours(theme);
    m_wornTheme = theme;
    m_mdiArea->setBackground(QBrush(theme.desktop));

    QPalette pal;
    pal.setColor(QPalette::Window, theme.buttonFace);
    pal.setColor(QPalette::WindowText, theme.windowText);
    pal.setColor(QPalette::Base, theme.windowBg);
    pal.setColor(QPalette::AlternateBase, theme.buttonFace);
    pal.setColor(QPalette::Text, theme.windowText);
    pal.setColor(QPalette::Button, theme.buttonFace);
    pal.setColor(QPalette::ButtonText, theme.buttonText);
    pal.setColor(QPalette::Highlight, theme.highlight);
    pal.setColor(QPalette::HighlightedText, theme.highlightText);
    pal.setColor(QPalette::ToolTipBase, theme.windowBg);
    pal.setColor(QPalette::ToolTipText, theme.windowText);
    qApp->setPalette(pal);

    // The menu strip takes the desktop colour, so the window reads as one
    // surface rather than a grey band above a coloured one. Set directly,
    // because a platform theme gives QMenuBar its own palette, which
    // outranks the application's.
    QPalette stripPal = pal;
    const QColor stripText = readableTextOn(theme.desktop);
    stripPal.setColor(QPalette::Window, theme.desktop);
    stripPal.setColor(QPalette::Button, theme.desktop);
    stripPal.setColor(QPalette::WindowText, stripText);
    stripPal.setColor(QPalette::ButtonText, stripText);
    stripPal.setColor(QPalette::Text, stripText);
    m_titleBar->applyPalette(stripPal);

    // The menus that drop out of the strip use the ordinary window colours;
    // only the bar itself uses the desktop colour. Submenus are set too,
    // since a platform theme gives each menu its own palette, and one left
    // out would open in the desktop colour instead of the scheme's.
    const std::function<void(QMenu *)> paletteMenu = [&](QMenu *menu) {
        menu->setPalette(pal);
        for (QAction *action : menu->actions()) {
            if (QMenu *submenu = action->menu()) paletteMenu(submenu);
        }
    };
    for (QAction *action : m_titleBar->menuBar()->actions()) {
        if (QMenu *menu = action->menu()) paletteMenu(menu);
    }

    // Themed icons for the three power actions, tinted to the menu text
    // colour. Suspend has no dedicated icon in most sets, so the night-mode
    // moon stands in for it.
    const QColor menuInk = theme.buttonText;
    if (m_suspendAction) {
        m_suspendAction->setIcon(tintedIcon({QStringLiteral("system-suspend-symbolic"),
                                             QStringLiteral("weather-clear-night-symbolic"),
                                             QStringLiteral("system-suspend")},
                                            menuInk));
    }
    if (m_restartAction) {
        m_restartAction->setIcon(tintedIcon({QStringLiteral("system-reboot-symbolic"),
                                             QStringLiteral("view-refresh-symbolic"),
                                             QStringLiteral("system-reboot")},
                                            menuInk));
    }
    if (m_shutdownAction) {
        m_shutdownAction->setIcon(tintedIcon({QStringLiteral("system-shutdown-symbolic"),
                                              QStringLiteral("system-shutdown")},
                                             menuInk));
    }

    // Styles draw the MDI child captions from the subwindow's own
    // Highlight/HighlightedText roles.
    const QPalette subPal = captionPalette(pal);
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        sub->setPalette(subPal);
    }
    for (MinimizedGroupIcon *icon : m_minimizedIcons) {
        icon->setPalette(subPal);
        icon->setDesktopColor(theme.desktop);
    }

    // A notice paints its own surface, like a dialog, so it is re-themed
    // rather than given a palette.
    if (m_notices) m_notices->applyTheme();
}

void MainWindow::editLaunchers() {
    LaunchersDialog dialog(this);
    // The items currently on the desktop, so the dialog can tell which wine
    // prefixes are unused: this screen is only written on save, so an item
    // added since then exists nowhere else.
    QVector<ProgramItem> open;
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (GroupWindow *gw = qobject_cast<GroupWindow *>(sub->widget())) {
            open += gw->items();
        }
    }
    dialog.setOpenScreen(currentScreen(), open);
    if (dialog.exec() != QDialog::Accepted) return;
    // Stored with the rest of the settings and used from the next launch on;
    // items on the desktop are unchanged, since their command line is built
    // each time they run.
    setLaunchers(dialog.launchers());
}

void MainWindow::editDefaultIcons() {
    DefaultIconsDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    setDefaultIcons(dialog.icons());

    // The desktop in front of us is already drawn: the items that took their
    // icon from a file type rather than from artwork of their own derive it
    // again, so the change is visible without reopening the screen.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (GroupWindow *gw = qobject_cast<GroupWindow *>(sub->widget())) {
            gw->refreshDerivedIcons();
        }
    }
}

void MainWindow::chooseColors() {
    const QString previous = m_themeName;
    ThemeDialog dialog(m_themeName, this);
    // Live preview on the real window, as the Control Panel did. Applied
    // without animation: arrowing down the list would otherwise blend one
    // scheme into the next and feel slow.
    connect(&dialog, &ThemeDialog::themePreviewed, this,
            [this](const QString &name) { applyTheme(name); });

    if (dialog.exec() == QDialog::Accepted) {
        applyTheme(dialog.selectedTheme());
    } else {
        applyTheme(previous);
    }
}

void MainWindow::refreshDesktopWallpaper() {
    if (!m_useDesktopWallpaper) {
        m_mdiArea->setWallpaper(QString());
        return;
    }
    // Asked for at the size of the screen rather than of the window: it is
    // the screen a themed wallpaper offers its pictures for, and the answer
    // then does not change every time the window is resized.
    const QScreen *screen = QGuiApplication::primaryScreen();
    m_mdiArea->setWallpaper(
        GnomeWallpaper::currentPath(darkMode(), screen ? screen->size() : QSize()));
}

void MainWindow::editSettings() {
    const bool previous = m_useDesktopWallpaper;
    // Read from the session rather than from the settings: the autostart
    // state is a file in ~/.config/autostart, which something else may have
    // removed since.
    const bool wasAutostart = autostartEnabled();
    SettingsDialog dialog(m_useDesktopWallpaper, wasAutostart, kioskSettings(), this);
    // Previewed on the real desktop as the checkbox is toggled, the way the
    // Colors dialog previews a scheme.
    connect(&dialog, &SettingsDialog::desktopWallpaperPreviewed, this, [this](bool use) {
        m_useDesktopWallpaper = use;
        refreshDesktopWallpaper();
    });

    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_useDesktopWallpaper = accepted ? dialog.useDesktopWallpaper() : previous;
    refreshDesktopWallpaper();

    if (accepted) {
        if (dialog.autostart() != wasAutostart && !setAutostartEnabled(dialog.autostart())) {
            m_notices->post(dialog.autostart()
                                ? tr("Could not set it to start with the session")
                                : tr("Could not stop it starting with the session"),
                            tr("Its entry in ~/.config/autostart could not be written."));
        }
        setKioskSettings(dialog.kiosk());
        // The menu strip's contents depend on the kiosk settings.
        m_titleBar->applyKioskSettings();
        // So does Window > Restore, which is disabled under the lock.
        updateWindowMenu();
    }
    save();
}

void MainWindow::about() {
    QMessageBox::about(
        this, tr("About Program Manager"),
        tr("Program Manager for Linux\n"
           "Version %1\n"
           "Build %2\n\n"
           "A Progman-style launcher for Windows programs, run through Wine.")
            .arg(appVersion(), buildVersion()));
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange) {
        // A maximized window has no draggable edges, so the resize margin is
        // dropped.
        const int margin = isMaximized() ? 0 : kResizeMargin;
        setContentsMargins(margin, 0, margin, margin);
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    setCursor(cursorForWindowEdges(windowEdgesAt(this, event->globalPosition().toPoint())));
    QMainWindow::mouseMoveEvent(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton &&
        startWindowResize(this, windowEdgesAt(this, event->globalPosition().toPoint()))) {
        event->accept();
        return;
    }
    QMainWindow::mousePressEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_saveOnExitAction->isChecked()) save();
    event->accept();
}

void MainWindow::createDefaultGroups() {
    addGroupWindow(new GroupWindow(tr("Main")))->setGeometry(20, 20, 320, 240);
    addGroupWindow(new GroupWindow(tr("StartUp")))->setGeometry(360, 20, 320, 240);
}

void MainWindow::load() {
    const QString path = m_screens.filePath(m_screens.current());
    if (path.isEmpty() || !QFile::exists(path)) {
        // A screen never saved before starts with the default desktop.
        setDarkMode(false);
        applyTheme(builtinThemes().first().name);
        createDefaultGroups();
        return;
    }

    QSettings settings(path, QSettings::IniFormat);
    setDarkMode(settings.value("dark", false).toBool());
    m_useDesktopWallpaper = settings.value("desktopWallpaper", false).toBool();
    applyTheme(settings.value("theme", builtinThemes().first().name).toString());
    refreshDesktopWallpaper();

    // XP icon styling used to be a single application-wide switch. A desktop
    // saved then has no per-item setting, so the old key is read once more
    // and applied to the items it used to affect; the next save writes the
    // per-item values and drops the key.
    const bool globalXpIcons = settings.value("xpStyleIcons", false).toBool();

    // The main window's own geometry; saveGeometry() also carries the display
    // and the maximized state. Applied only at startup, so switching screens
    // mid-session does not move the window.
    const QByteArray windowGeometry = settings.value("windowGeometry").toByteArray();
    if (!windowGeometry.isEmpty() && !isVisible()) restoreGeometry(windowGeometry);

    int groupCount = settings.beginReadArray("Groups");
    for (int i = 0; i < groupCount; ++i) {
        settings.setArrayIndex(i);
        const QString id = settings.value("id").toString();
        QString title = settings.value("title").toString();
        int x = settings.value("x", 20 + i * 20).toInt();
        int y = settings.value("y", 20 + i * 20).toInt();
        int w = settings.value("w", 320).toInt();
        int h = settings.value("h", 240).toInt();
        bool minimized = settings.value("minimized", false).toBool();

        auto *group = new GroupWindow(title);
        // Ignored when the file holds no id, in which case the group keeps
        // the one it was just assigned.
        group->setId(id);
        int itemCount = settings.beginReadArray("Items");
        for (int j = 0; j < itemCount; ++j) {
            settings.setArrayIndex(j);
            group->addProgramItem(readProgramItem(settings));
        }
        settings.endArray();
        if (globalXpIcons) group->adoptGlobalXpStyleIcons();

        QMdiSubWindow *sub = addGroupWindow(group);
        sub->setGeometry(x, y, w, h);
        m_normalGeometry[sub] = QRect(x, y, w, h);
        if (minimized) minimizeToIcon(sub);
    }
    settings.endArray();

    if (groupCount == 0) createDefaultGroups();
}

void MainWindow::save() {
    const QString path = m_screens.filePath(m_screens.current());
    if (path.isEmpty()) return;

    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    settings.setValue("theme", m_themeName);
    settings.setValue("dark", darkMode());
    settings.setValue("desktopWallpaper", m_useDesktopWallpaper);
    settings.setValue("windowGeometry", saveGeometry());

    const QList<QMdiSubWindow *> subs = m_mdiArea->subWindowList();
    settings.beginWriteArray("Groups");
    int idx = 0;
    for (QMdiSubWindow *sub : subs) {
        GroupWindow *gw = qobject_cast<GroupWindow *>(sub->widget());
        if (!gw) continue;

        settings.setArrayIndex(idx++);
        settings.setValue("id", gw->id());
        settings.setValue("title", gw->title());
        // The restored geometry, not the stub a minimized window collapsed to.
        const QRect geo = m_normalGeometry.value(sub, sub->geometry());
        settings.setValue("x", geo.x());
        settings.setValue("y", geo.y());
        settings.setValue("w", geo.width());
        settings.setValue("h", geo.height());
        settings.setValue("minimized", m_minimizedIcons.contains(sub) || sub->isMinimized());

        const auto &items = gw->items();
        settings.beginWriteArray("Items");
        for (int j = 0; j < items.size(); ++j) {
            settings.setArrayIndex(j);
            writeProgramItem(settings, items[j]);
        }
        settings.endArray();
    }
    settings.endArray();
}
