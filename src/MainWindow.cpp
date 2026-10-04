#include "MainWindow.h"

#include "SearchProvider.h"

#include "AlignmentGuides.h"
#include "LaunchersDialog.h"
#include "TextPromptDialog.h"
#include "ApplicationPickerDialog.h"
#include "SteamGamePickerDialog.h"
#include "DesktopArea.h"
#include "GroupWindow.h"
#include "Kiosk.h"
#include "LaunchNotices.h"
#include "MinimizedGroupIcon.h"
#include "NotificationArea.h"
#include "TitleBar.h"
#include "WindowEdges.h"
#include "Theme.h"
#include "GnomeWallpaper.h"
#include "SettingsDialog.h"
#include "ThemeDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QKeySequence>
#include <QScrollBar>
#include <QLineEdit>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QMessageBox>

#include <functional>
#include <QScreen>
#include <QSettings>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QVariantAnimation>

MainWindow::MainWindow(const QString &startupScreen, QWidget *parent) : QMainWindow(parent) {
    resize(900, 650);

    // No window manager caption: the menu strip carries the title and the
    // caption buttons itself, and the window keeps a band along its other
    // three edges to be resized by.
    setWindowFlag(Qt::FramelessWindowHint);
    setContentsMargins(kResizeMargin, 0, kResizeMargin, kResizeMargin);
    setMouseTracking(true);

    m_titleBar = new TitleBar(this);
    setMenuWidget(m_titleBar);

    m_mdiArea = new DesktopArea(this);
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    setCentralWidget(m_mdiArea);
    // A sibling of the group windows, so it can be raised in front of them.
    m_guides = new AlignmentGuides(m_mdiArea->viewport());

    // The corner the desktop says things in. On the same layer as the drag
    // guides, which is the one that draws over the group windows.
    m_notices = new NotificationArea(m_mdiArea->viewport());
    connect(LaunchNotices::instance(), &LaunchNotices::posted, m_notices,
            &NotificationArea::post);

    // Scrolling the desktop moves everything the viewport holds, including
    // the things that are not on the desktop but over it: the row of
    // minimized groups along the bottom and the corner the notices sit in
    // both stay where they are.
    for (QScrollBar *bar : {m_mdiArea->horizontalScrollBar(), m_mdiArea->verticalScrollBar()}) {
        connect(bar, &QScrollBar::valueChanged, this, [this] {
            arrangeMinimizedIcons();
            m_notices->updatePosition();
        });
    }

    buildMenus();
    // --profile on the command line wins over whatever was open last.
    if (!startupScreen.isEmpty() && m_screens.contains(startupScreen)) {
        m_screens.setCurrent(startupScreen);
    }
    applyTheme(builtinThemes().first().name);
    load();
    rebuildScreenMenu();
    updateWindowTitle();

    // Offered to the desktop's search once there is a desktop to search:
    // it answers out of the groups now on screen, so it has to come after
    // they are loaded.
    m_searchProvider = new SearchProvider(this);
    m_searchProvider->registerOnBus();

    // Long enough that a drag is one change, short enough that the undo is
    // there by the time anyone reaches for it.
    m_layoutSettle = new QTimer(this);
    m_layoutSettle->setSingleShot(true);
    m_layoutSettle->setInterval(400);
    connect(m_layoutSettle, &QTimer::timeout, this, &MainWindow::recordLayoutChange);
    m_layoutNow = currentLayout();
    updateEditActions();

    // Whatever the desktop is wearing, worn here too -- now, and again
    // whenever it is switched over.
    m_schemeWatch = new DesktopColorSchemeWatch(this);
    connect(m_schemeWatch, &DesktopColorSchemeWatch::schemeChanged, this,
            &MainWindow::followDesktopScheme);
    followDesktopScheme(desktopColorScheme());

    // The same for the wallpaper, when the setting asks for it: shown now,
    // and shown again whenever the desktop's background is changed.
    m_wallpaperWatch = new GnomeWallpaper(this);
    connect(m_wallpaperWatch, &GnomeWallpaper::changed, this,
            &MainWindow::refreshDesktopWallpaper);
    refreshDesktopWallpaper();

    connect(m_titleBar, &TitleBar::darkModeToggled, this, [this](bool dark) {
        setDarkMode(dark);
        // Every palette in the application comes from the scheme, so the
        // whole thing is put on again rather than patched -- and moved to,
        // the whole desktop changing at once being worth easing.
        applyTheme(m_themeName, true);
        save();
    });

    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::updateWindowMenu);
    connect(m_mdiArea, &QMdiArea::subWindowActivated, this, &MainWindow::onSubWindowActivated);
    m_mdiArea->viewport()->installEventFilter(this);
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

    // Everything that puts something new on the desktop, in one place: an
    // empty group, an empty item, and the two that fill an item in from
    // somewhere the machine already knows about.
    QMenu *newMenu = fileMenu->addMenu(tr("&New"));
    newMenu->addAction(tr("Program &Group..."), this, &MainWindow::newGroup);
    newMenu->addAction(tr("&Item..."), this, &MainWindow::newItem);
    newMenu->addSeparator();
    newMenu->addAction(tr("&Application..."), this, &MainWindow::insertApplication);
    newMenu->addAction(tr("&Steam Game..."), this, &MainWindow::insertSteamGame);

    fileMenu->addAction(tr("&Rename Group..."), this, [this] { renameGroup(); });
    fileMenu->addAction(tr("&Delete Group"), this, &MainWindow::deleteGroup);
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
    optionsMenu->addAction(tr("&Settings..."), this, &MainWindow::editSettings);
    optionsMenu->addSeparator();
    m_saveOnExitAction = optionsMenu->addAction(tr("&Save Settings on Exit"));
    m_saveOnExitAction->setCheckable(true);
    m_saveOnExitAction->setChecked(true);

    m_screenMenu = bar->addMenu(tr("&Screens"));
    m_screenGroup = new QActionGroup(this);
    m_screenGroup->setExclusive(true);

    // Undoing a move is what anyone wants after dragging a group somewhere
    // by accident; cascading them all is what nobody wants. Owned by the
    // window so the Window menu can be rebuilt around them.
    m_undoAction = new QAction(tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoEdit);

    m_redoAction = new QAction(tr("&Redo"), this);
    m_redoAction->setShortcuts({QKeySequence::Redo, QKeySequence(tr("Ctrl+Y"))});
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoEdit);

    // On the window as well as in the menu, so the shortcuts answer before
    // the menu has ever been opened.
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
    // A group is never closed, only minimized to an icon: it leaves the
    // desktop through File > Delete Group, which asks first. So the caption
    // carries no close box, and the system menu's Close entry -- along with
    // the shortcut that lives on it -- is greyed out to match.
    if (QMenu *systemMenu = sub->systemMenu()) {
        for (QAction *action : systemMenu->actions()) {
            if (action->shortcut() == QKeySequence(QKeySequence::Close)) {
                action->setEnabled(false);
                action->setVisible(false);
            }
        }
    }
    sub->setWindowTitle(group->title());
    sub->resize(320, 240);
    connect(group, &GroupWindow::titleChanged, sub, &QMdiSubWindow::setWindowTitle);
    connect(group, &GroupWindow::itemSelected, this, &MainWindow::clearMinimizedIconSelection);
    connect(group, &GroupWindow::itemDeleted, this,
            [this, group](int index, const ProgramItem &item) {
                Edit edit;
                edit.kind = Edit::ItemDeleted;
                edit.group = indexOfGroup(group);
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
    // The arrangements remembered were of a different set of windows.
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

    // The desktop would otherwise go down with the session unsaved, closeEvent
    // never having run.
    if (endsSession && m_saveOnExitAction->isChecked()) save();

    // Started detached rather than waited on: when the machine is allowed to
    // do this the process outlives us, and when it is not, the desktop's own
    // authorisation agent takes over the conversation.
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

void MainWindow::deleteGroup() {
    QMdiSubWindow *sub = m_mdiArea->activeSubWindow();
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw) return;

    auto reply = QMessageBox::question(
        this, tr("Delete"), tr("Delete group '%1' and all its items?").arg(gw->title()));
    if (reply != QMessageBox::Yes) return;

    // The whole of it, before it is gone: a group is its items, and they go
    // with it. Undo is the only thing between one answer to one question
    // and setting the lot up again by hand.
    Edit edit;
    edit.kind = Edit::GroupDeleted;
    edit.group = indexOfGroup(gw);
    edit.title = gw->title();
    edit.items = gw->items();
    edit.geometry = m_normalGeometry.value(sub, sub->geometry());
    edit.minimized = sub->isMinimized() || m_minimizedIcons.contains(sub);

    m_normalGeometry.remove(sub);
    delete sub; // owns and deletes the GroupWindow child widget too

    // a group taken away: the arrangements remembered no longer describe
    // what is on screen.
    forgetArrangements();
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

void MainWindow::updateWindowMenu() {
    m_windowMenu->clear();

    // The way back from a maximized window, whose caption carries no buttons
    // while it fills the screen.
    QAction *restore = m_windowMenu->addAction(tr("&Restore"), this, &QWidget::showNormal);
    // Locked, there is no way back out of full screen at all -- that being
    // what the lock is -- and a menu entry that undid it would be a hole in
    // the middle of it. Options > Settings is where it is unlocked.
    restore->setEnabled(isMaximized() && !kioskSettings().locked);
    m_windowMenu->addSeparator();

    // Added rather than created: this menu is emptied and built again every
    // time it opens, and a menu deletes the actions it owns. These belong to
    // the window, so that they keep the enabled state the history gives them
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
    // they fall out of the exclusive group on the way.
    m_screenMenu->clear();

    const QStringList names = m_screens.names();
    for (int i = 0; i < names.size(); ++i) {
        const QString &name = names[i];
        QAction *act = m_screenMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(name == m_screens.current());
        // The first nine are a keystroke away, in menu order.
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
    // There is always one screen to fall back on, so the last cannot go.
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
        // Leaving a rename dialog on the name it opened with is not a clash.
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

    // Deleted rather than closeAllSubWindows()'d: a close is only a request,
    // and the windows would still be alive when the next screen's groups
    // opened on top of them.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) delete sub;
    // After the deletions, so a resize on the way out cannot file geometry
    // against a window that is already gone.
    m_normalGeometry.clear();

    // the desktop emptied: nothing remembered is about the groups that will
    // take their place.
    forgetEdits();
}

void MainWindow::followDesktopScheme(DesktopScheme scheme) {
    // A desktop that says nothing either way is left alone: the switch in
    // the strip is then the only thing that decides.
    if (scheme == DesktopScheme::Unknown) return;

    const bool wantsDark = scheme == DesktopScheme::Dark;
    if (wantsDark == darkMode()) return;

    setDarkMode(wantsDark);
    // Moved to, as the switch in the strip is: the whole desktop changing
    // at once is worth easing whoever asked for it.
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
    // The screen it lives on first: a group on another one is not on the
    // desktop at all until that screen is loaded.
    if (!screen.isEmpty() && screen != m_screens.current()) switchToScreen(screen);

    const QVector<GroupWindow *> all = groups();
    GroupWindow *group = groupIndex >= 0 && groupIndex < all.size() ? all[groupIndex] : nullptr;

    // The desktop may have been rearranged since the search last looked, so
    // the name is checked and, failing that, searched for. Better a slightly
    // wrong group than the wrong item.
    const auto itemAt = [](GroupWindow *candidate, int index) {
        return candidate && index >= 0 && index < candidate->items().size()
                   ? candidate->items()[index].name
                   : QString();
    };
    // No group at that position, or something else sitting there now: look
    // for the item by name instead. The null case has to be checked outright
    // -- comparing names would let an item with no name match nothing at all
    // and skip the search.
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

    // The window it is in, brought out of whatever state it is in and made
    // the one in front.
    if (auto *sub = qobject_cast<QMdiSubWindow *>(group->parentWidget())) {
        if (sub->isMinimized() || !sub->isVisible()) sub->showNormal();
        m_mdiArea->setActiveSubWindow(sub);
        sub->raise();
    }
    group->selectItem(itemIndex);

    // And the application itself, which is the whole point of a result that
    // shows something rather than starting it.
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

    save();
    clearDesktop();
    m_screens.setCurrent(name);
    load();
    rebuildScreenMenu();
    updateWindowTitle();
}

void MainWindow::newScreen() {
    const QString name = askScreenName(tr("New Screen"), QString());
    if (name.isEmpty() || !m_screens.create(name)) return;

    save();
    clearDesktop();
    m_screens.setCurrent(name);
    // Nothing saved under this name yet, so load() lays out a fresh desktop.
    load();
    rebuildScreenMenu();
    updateWindowTitle();
}

void MainWindow::duplicateScreen() {
    const QString source = m_screens.current();
    // Copied from disk, so what is on screen has to be written out first.
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

    // remove() already moved current on to a surviving screen.
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

    // Hiding has to wait a turn: this runs from within showMinimized(), which
    // would show the stub again straight after we hid it. The window stays in
    // its minimized state while hidden, so showNormal() later brings back the
    // geometry it had before.
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

    // Filled left to right along the bottom, stacking upwards, like Progman.
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
        // Where a minimized group would go back to, not where its icon sits:
        // undoing a move should put it back where it was, whether or not it
        // happens to be minimized now.
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
    // Only a gesture that moved something counts; a click that selects a
    // group sends geometry events too.
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
    // Deep enough to get out of real trouble, and still nothing: fifty
    // arrangements of a dozen groups is a few thousand numbers, and a
    // deleted group is the group.
    constexpr int kMaxRemembered = 50;
    while (m_undo.size() > kMaxRemembered) m_undo.removeFirst();
    // Doing something by hand is a new branch: what was undone away is gone.
    m_redo.clear();
    updateEditActions();
}

GroupWindow *MainWindow::groupAt(int index) const {
    const QList<QMdiSubWindow *> windows = m_mdiArea->subWindowList();
    if (index < 0 || index >= windows.size()) return nullptr;
    return qobject_cast<GroupWindow *>(windows[index]->widget());
}

int MainWindow::indexOfGroup(const GroupWindow *group) const {
    const QList<QMdiSubWindow *> windows = m_mdiArea->subWindowList();
    for (int i = 0; i < windows.size(); ++i) {
        if (windows[i]->widget() == group) return i;
    }
    return -1;
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
        GroupWindow *group = groupAt(edit.group);
        // The group it was in has since gone itself. Nothing sensible can be
        // done with the item, and putting it somewhere else would be a
        // worse answer than leaving it where it is not.
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
            for (const ProgramItem &item : edit.items) group->addProgramItem(item);
            QMdiSubWindow *sub = addGroupWindow(group);
            sub->setGeometry(edit.geometry);
            m_normalGeometry[sub] = edit.geometry;
            if (edit.minimized) minimizeToIcon(sub);
            // It comes back at the end of the desktop's order rather than in
            // the place it had, so the arrangements remembered no longer
            // line up -- addGroupWindow() has said so already.
        } else if (GroupWindow *group = groupAt(edit.group)) {
            if (QMdiSubWindow *sub = qobject_cast<QMdiSubWindow *>(group->parentWidget())) {
                m_normalGeometry.remove(sub);
                delete sub;
                forgetArrangements();
            }
        }
        return;
    }
}

void MainWindow::updateEditActions() {
    // Named after what it will actually do: "Undo" alone, in front of a
    // desktop someone has just emptied a group off, says nothing about
    // whether the group is coming back.
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
    // The deletions stay: each carries the whole of what it took away and
    // where it came from, so none of them depends on the windows standing
    // where they stood.
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
        // A press that reaches the viewport landed on bare desktop; one on an
        // icon is consumed by the icon itself.
        if (event->type() == QEvent::MouseButtonPress) {
            clearMinimizedIconSelection();
            clearGroupSelections();
            // Deactivate the group too, so no caption stays highlighted when
            // the click landed on bare desktop.
            m_mdiArea->setActiveSubWindow(nullptr);
        }
    }

    if (auto *sub = qobject_cast<QMdiSubWindow *>(watched)) {
        // A drag of this group has begun: which edges it moves, and so which
        // of them are to be lined up with the neighbours, is whatever has
        // changed by the time the geometry comes through.
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
        // Letting go is the answer to the line that is showing: whatever the
        // pointer was still a few pixels off, the edge lands on it.
        if (event->type() == QEvent::MouseButtonRelease) {
            // Whichever it was: one of the two does nothing, each being
            // told apart by whether the size changed.
            snapWhileMoving(sub, AlignmentGuides::kShowDistance);
            snapWhileResizing(sub, AlignmentGuides::kShowDistance);
            m_resizingSub = nullptr;
            m_guides->clear();
        }
        // Each step of the resize arrives as a geometry change; a move drags
        // all four edges at once and is left alone.
        // Cascade, tile and loading a screen resize windows too; only a
        // resize the pointer is carrying out is worth lining up. The hold is
        // left until this resize has been seen through -- a geometry set from
        // inside the subwindow's own resize does not survive it -- and `sub`
        // as the context means a group closed in between drops it.
        if (event->type() == QEvent::Resize &&
            (QApplication::mouseButtons() & Qt::LeftButton)) {
            QTimer::singleShot(0, sub, [this, sub] { snapWhileResizing(sub); });
        }
        // And each step of a carry arrives as a move. Loading a screen and
        // undoing an arrangement move windows too; only one the pointer is
        // carrying is worth lining up.
        if (event->type() == QEvent::Move &&
            (QApplication::mouseButtons() & Qt::LeftButton)) {
            QTimer::singleShot(0, sub, [this, sub] { snapWhileMoving(sub); });
        }

        // A group's caption has no maximize box, so double clicking it is
        // free for what a caption double click is otherwise no use for here:
        // renaming the group. Anywhere else on the subwindow -- its own
        // contents having had first refusal -- it does nothing.
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QWidget *contents = sub->widget();
            if (mouse->button() == Qt::LeftButton && contents &&
                mouse->position().y() < contents->geometry().top()) {
                renameGroup(sub);
            }
            return true;
        }
        // And should a group be maximized by some other route -- the system
        // menu still on a right click -- it is put straight back.
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
            // A drag arrives as a run of these. The arrangement is taken
            // down once it has stopped moving, so one drag is one thing to
            // undo rather than a hundred.
            if (!m_applyingLayout && m_layoutSettle) m_layoutSettle->start();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::snapWhileMoving(QMdiSubWindow *sub, int reach) {
    if (sub != m_resizingSub || m_snapping) return;

    // Shift asks for the position under the pointer and nothing else: the
    // lines stay up to judge by eye, but nothing is held on one.
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) reach = -1;

    const QRect rect = sub->geometry();
    // Only a carry: anything that changed the size is the resize's business.
    if (rect.size() != m_resizeStartGeometry.size()) return;

    const QRect held = m_guides->applyMove(rect, reach);
    if (held == rect) return;

    m_snapping = true;
    sub->setGeometry(held);
    m_snapping = false;
}

void MainWindow::snapWhileResizing(QMdiSubWindow *sub, int reach) {
    if (sub != m_resizingSub || m_snapping) return;

    // Shift asks for the size under the pointer and nothing else: the lines
    // stay up, as something to judge the edge against by eye, but no edge is
    // held on one. A reach nothing can be within is what leaves them drawn
    // and inert.
    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) reach = -1;

    const QRect rect = sub->geometry();
    // A move drags all four edges at once, and holding two opposite ones to
    // different lines would resize what was only being carried about.
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
    // Only the active group keeps its highlighted icon, so there is never
    // more than one selection on screen.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (sub == active) continue;
        if (auto *group = qobject_cast<GroupWindow *>(sub->widget())) {
            group->clearSelection();
        }
    }
    if (active) clearMinimizedIconSelection();
}

void MainWindow::applyTheme(const QString &themeName, bool moved) {
    // Where it is coming from: the colours on screen, not what the desktop
    // is set to. Dark mode is flipped before this is called, so asking the
    // scheme would say the destination and there would be nothing to move.
    const Theme from = m_wornTheme;

    m_themeName = themeByName(themeName).name;
    // So the dialogs, which paint surfaces no palette role covers, can reach
    // the scheme too -- and so that what follows is the half being worn
    // rather than the light one it was derived from.
    setCurrentTheme(m_themeName);
    const Theme to = currentTheme();

    if (m_themeFade) m_themeFade->stop();

    // Nothing to move on the way in, nothing to move between when the
    // colours are the same, and nothing to move when it was not asked for.
    if (!moved || !isVisible() || !from.windowBg.isValid() ||
        from.windowBg == to.windowBg) {
        applyThemeColours(to);
        refreshDesktopWallpaper();
        return;
    }

    if (!m_themeFade) {
        m_themeFade = new QVariantAnimation(this);
        // Straight through, at one rate: an eased move slows at both ends,
        // which on a flat colour reads as hesitating rather than as easing.
        m_themeFade->setEasingCurve(QEasingCurve::Linear);
    }
    m_themeFade->setStartValue(0.0);
    m_themeFade->setEndValue(1.0);
    // Long enough to be seen as a move, short enough that nobody waits.
    m_themeFade->setDuration(220);
    m_themeFade->disconnect(this);
    connect(m_themeFade, &QVariantAnimation::valueChanged, this,
            [this, from, to](const QVariant &value) {
                applyThemeColours(blendThemes(from, to, value.toReal()));
            });
    connect(m_themeFade, &QVariantAnimation::finished, this,
            [this, to] { applyThemeColours(to); });
    // Once, at the start: the picture does not fade with the colours, and
    // reading the setting on every frame of the move would be absurd.
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

    // The menu strip takes the desktop's own colour, so the window reads as
    // one surface rather than a grey band sitting above a coloured one. It is
    // told directly: a platform theme hands QMenuBar a palette of its own,
    // and that outranks the application's.
    QPalette stripPal = pal;
    const QColor stripText = readableTextOn(theme.desktop);
    stripPal.setColor(QPalette::Window, theme.desktop);
    stripPal.setColor(QPalette::Button, theme.desktop);
    stripPal.setColor(QPalette::WindowText, stripText);
    stripPal.setColor(QPalette::ButtonText, stripText);
    stripPal.setColor(QPalette::Text, stripText);
    m_titleBar->applyPalette(stripPal);

    // What drops out of the strip is an ordinary menu, in the ordinary window
    // colours: only the bar itself wears the desktop's. Submenus with it,
    // since a platform theme hands each menu a palette of its own and that
    // outranks the application's -- so one left out would open in the
    // desktop's colours rather than the scheme's.
    const std::function<void(QMenu *)> paletteMenu = [&](QMenu *menu) {
        menu->setPalette(pal);
        for (QAction *action : menu->actions()) {
            if (QMenu *submenu = action->menu()) paletteMenu(submenu);
        }
    };
    for (QAction *action : m_titleBar->menuBar()->actions()) {
        if (QMenu *menu = action->menu()) paletteMenu(menu);
    }

    // The desktop's own icons for the three that act on the machine, in the
    // menu's ink. Suspend has no icon of its own in the usual sets, so the
    // moon a desktop uses for night stands in for it.
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

    // A notice paints its own surface, as a dialog does, so it is redressed
    // rather than repalettised.
    if (m_notices) m_notices->applyTheme();
}

void MainWindow::editLaunchers() {
    LaunchersDialog dialog(this);
    // What is on the desktop now, so that the dialog can say which wine
    // prefixes nothing runs in any more: this screen is only written to its
    // file when it is saved, and an item added since then is only here.
    QVector<ProgramItem> open;
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (GroupWindow *gw = qobject_cast<GroupWindow *>(sub->widget())) {
            open += gw->items();
        }
    }
    dialog.setOpenScreen(currentScreen(), open);
    if (dialog.exec() != QDialog::Accepted) return;
    // Kept with the rest of the settings and used from the next launch on --
    // items already on the desktop are unchanged, what they are started with
    // being worked out each time they are run.
    setLaunchers(dialog.launchers());
}

void MainWindow::chooseColors() {
    const QString previous = m_themeName;
    ThemeDialog dialog(m_themeName, this);
    // Live preview on the real window, like the Control Panel did.
    // Put on outright rather than moved to: picking down the list of
    // schemes, each step would smear into the next and the list would feel
    // slow to answer.
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
    SettingsDialog dialog(m_useDesktopWallpaper, kioskSettings(), this);
    // Shown on the real desktop as it is switched, the way the Colors
    // dialog previews a scheme: a background is to be looked at, not read
    // about in a checkbox.
    connect(&dialog, &SettingsDialog::desktopWallpaperPreviewed, this, [this](bool use) {
        m_useDesktopWallpaper = use;
        refreshDesktopWallpaper();
    });

    const bool accepted = dialog.exec() == QDialog::Accepted;
    m_useDesktopWallpaper = accepted ? dialog.useDesktopWallpaper() : previous;
    refreshDesktopWallpaper();

    if (accepted) {
        setKioskSettings(dialog.kiosk());
        // The strip takes what it carries from them, and so does whether
        // the window can be brought back out of full screen.
        m_titleBar->applyKioskSettings();
        // Window > Restore is the other way out, and it goes with the rest
        // while the window is locked.
        updateWindowMenu();
    }
    save();
}

void MainWindow::about() {
    QMessageBox::about(
        this, tr("About Program Manager"),
        tr("Program Manager for Linux\n\n"
           "A Progman-style launcher for Windows programs, run through Wine."));
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange) {
        // A maximized window has no edges of its own to drag, so the band
        // that offers them goes away along with them.
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
        // A screen that has never been saved starts on the stock desktop.
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

    // XP icon styling used to be one switch for the whole Program Manager.
    // A desktop saved back then carries no per-item setting, so the switch is
    // read one last time and handed to the items it used to restyle; saving
    // the screen again writes them out individually and drops the key.
    const bool globalXpIcons = settings.value("xpStyleIcons", false).toBool();

    // Where the Program Manager window itself was last left. saveGeometry
    // carries the screen and the maximized state, not just a rectangle.
    // Only honoured at startup: moving the window out from under someone
    // mid-session, just because they switched screen, would be rude.
    const QByteArray windowGeometry = settings.value("windowGeometry").toByteArray();
    if (!windowGeometry.isEmpty() && !isVisible()) restoreGeometry(windowGeometry);

    int groupCount = settings.beginReadArray("Groups");
    for (int i = 0; i < groupCount; ++i) {
        settings.setArrayIndex(i);
        QString title = settings.value("title").toString();
        int x = settings.value("x", 20 + i * 20).toInt();
        int y = settings.value("y", 20 + i * 20).toInt();
        int w = settings.value("w", 320).toInt();
        int h = settings.value("h", 240).toInt();
        bool minimized = settings.value("minimized", false).toBool();

        auto *group = new GroupWindow(title);
        int itemCount = settings.beginReadArray("Items");
        for (int j = 0; j < itemCount; ++j) {
            settings.setArrayIndex(j);
            ProgramItem item;
            item.name = settings.value("name").toString();
            item.customName = settings.value("customName", false).toBool();
            item.command = settings.value("command").toString();
            item.arguments = settings.value("arguments").toString();
            item.launcher = settings.value("launcher").toString();
            item.prefix = settings.value("prefix").toString();
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
            group->addProgramItem(item);
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
        settings.setValue("title", gw->title());
        // Never the stub a minimized window collapsed to: save the rect it
        // will actually reopen at.
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
            settings.setValue("name", items[j].name);
            // Only a name someone chose has anything to remember: the rest
            // are worked out again each time the item is read.
            if (items[j].customName) settings.setValue("customName", true);
            settings.setValue("command", items[j].command);
            settings.setValue("arguments", items[j].arguments);
            // Only written when set, so a plain wine item stays as it was.
            if (!items[j].launcher.isEmpty()) settings.setValue("launcher", items[j].launcher);
            // Likewise the prefix: only an item sharing a named one has
            // anything to remember, the rest having one of their own.
            if (!items[j].prefix.isEmpty()) settings.setValue("prefix", items[j].prefix);
            // Likewise the icon: only an item wearing one somebody picked out
            // of the several its file holds has anything to remember.
            if (items[j].iconIndex >= 0) {
                settings.setValue("iconIndex", items[j].iconIndex);
                if (!items[j].iconSource.isEmpty()) {
                    settings.setValue("iconSource", items[j].iconSource);
                }
            }
            if (items[j].xpStyleIcon) settings.setValue("xpStyleIcon", true);
            // Only a program being scaled has anything to remember about it,
            // and the size is kept whatever the scale, so that turning it off
            // and on again does not mean measuring again.
            if (items[j].windowScalePercent > 100) {
                settings.setValue("windowScalePercent", items[j].windowScalePercent);
                if (items[j].windowScaleMode == WindowScaleMode::WineDpi) {
                    settings.setValue("windowScaleMode", "wineDpi");
                }
            }
            if (!items[j].shader.isEmpty()) settings.setValue("shader", items[j].shader);
            if (items[j].nativeSize.isValid() && !items[j].nativeSize.isEmpty()) {
                settings.setValue("nativeSize", items[j].nativeSize);
            }
        }
        settings.endArray();
    }
    settings.endArray();
}
