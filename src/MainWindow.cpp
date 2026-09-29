#include "MainWindow.h"
#include "DesktopArea.h"
#include "GroupWindow.h"
#include "MinimizedGroupIcon.h"
#include "Theme.h"
#include "ThemeDialog.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

MainWindow::MainWindow(const QString &startupProfile, QWidget *parent) : QMainWindow(parent) {
    resize(900, 650);

    m_mdiArea = new DesktopArea(this);
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    setCentralWidget(m_mdiArea);

    buildMenus();
    // --profile on the command line wins over whatever was open last.
    if (!startupProfile.isEmpty() && m_profiles.contains(startupProfile)) {
        m_profiles.setCurrent(startupProfile);
    }
    applyTheme(builtinThemes().first().name);
    load();
    rebuildProfileMenu();
    updateWindowTitle();

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
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&New Program Group..."), this, &MainWindow::newGroup);
    fileMenu->addAction(tr("New &Item..."), this, &MainWindow::newItem);
    fileMenu->addAction(tr("&Delete Group"), this, &MainWindow::deleteGroup);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &QWidget::close);

    QMenu *optionsMenu = menuBar()->addMenu(tr("&Options"));
    optionsMenu->addAction(tr("&Colors..."), this, &MainWindow::chooseColors);
    optionsMenu->addSeparator();
    m_saveOnExitAction = optionsMenu->addAction(tr("&Save Settings on Exit"));
    m_saveOnExitAction->setCheckable(true);
    m_saveOnExitAction->setChecked(true);

    m_profileMenu = menuBar()->addMenu(tr("&Profiles"));
    m_profileGroup = new QActionGroup(this);
    m_profileGroup->setExclusive(true);

    m_windowMenu = menuBar()->addMenu(tr("&Window"));
    connect(m_windowMenu, &QMenu::aboutToShow, this, &MainWindow::updateWindowMenu);

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
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

    sub->setPalette(captionPalette(sub->palette()));

    sub->installEventFilter(this);

    connect(sub, &QMdiSubWindow::windowStateChanged, this,
            [this, sub](Qt::WindowStates, Qt::WindowStates newState) {
                if (newState & Qt::WindowMinimized) minimizeToIcon(sub);
            });

    sub->show();
    return sub;
}

void MainWindow::newGroup() {
    bool ok;
    QString title = QInputDialog::getText(this, tr("New Program Group"), tr("Description:"),
                                           QLineEdit::Normal, QString(), &ok);
    if (!ok || title.trimmed().isEmpty()) return;
    addGroupWindow(new GroupWindow(title.trimmed()));
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

void MainWindow::deleteGroup() {
    QMdiSubWindow *sub = m_mdiArea->activeSubWindow();
    GroupWindow *gw = sub ? qobject_cast<GroupWindow *>(sub->widget()) : nullptr;
    if (!gw) return;

    auto reply = QMessageBox::question(
        this, tr("Delete"), tr("Delete group '%1' and all its items?").arg(gw->title()));
    if (reply != QMessageBox::Yes) return;

    m_normalGeometry.remove(sub);
    delete sub; // owns and deletes the GroupWindow child widget too
}

void MainWindow::updateWindowMenu() {
    m_windowMenu->clear();
    m_windowMenu->addAction(tr("&Cascade"), m_mdiArea, &QMdiArea::cascadeSubWindows);
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

void MainWindow::rebuildProfileMenu() {
    // The menu owns the per-profile actions, so clearing it deletes them and
    // they fall out of the exclusive group on the way.
    m_profileMenu->clear();

    const QStringList names = m_profiles.names();
    for (int i = 0; i < names.size(); ++i) {
        const QString &name = names[i];
        QAction *act = m_profileMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(name == m_profiles.current());
        // The first nine are a keystroke away, in menu order.
        if (i < 9) act->setShortcut(QKeySequence(Qt::CTRL | Qt::Key(Qt::Key_1 + i)));
        m_profileGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, name]() { switchToProfile(name); });
    }

    m_profileMenu->addSeparator();
    m_profileMenu->addAction(tr("&New Profile..."), this, &MainWindow::newProfile);
    m_profileMenu->addAction(tr("D&uplicate Profile..."), this, &MainWindow::duplicateProfile);
    m_profileMenu->addAction(tr("&Rename Profile..."), this, &MainWindow::renameProfile);
    QAction *del = m_profileMenu->addAction(tr("&Delete Profile..."), this,
                                            &MainWindow::deleteProfile);
    // There is always one profile to fall back on, so the last cannot go.
    del->setEnabled(names.size() > 1);
    m_profileMenu->addSeparator();
    m_profileMenu->addAction(tr("&Save Profile Now"), QKeySequence::Save, this,
                             &MainWindow::save);
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(tr("Program Manager - %1").arg(m_profiles.current()));
    m_mdiArea->setProfileName(m_profiles.current());
}

QString MainWindow::askProfileName(const QString &windowTitle, const QString &initial) {
    QString suggestion = initial;
    forever {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, windowTitle, tr("Profile name:"), QLineEdit::Normal,
                                  suggestion, &ok)
                .trimmed();
        if (!ok || name.isEmpty()) return {};
        // Leaving a rename dialog on the name it opened with is not a clash.
        if (!m_profiles.contains(name) || name == initial) return name;

        QMessageBox::warning(this, windowTitle,
                             tr("A profile named '%1' already exists.").arg(name));
        suggestion = name;
    }
}

void MainWindow::clearDesktop() {
    for (MinimizedGroupIcon *icon : m_minimizedIcons) delete icon;
    m_minimizedIcons.clear();
    m_minimizedOrder.clear();

    // Deleted rather than closeAllSubWindows()'d: a close is only a request,
    // and the windows would still be alive when the next profile's groups
    // opened on top of them.
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) delete sub;
    // After the deletions, so a resize on the way out cannot file geometry
    // against a window that is already gone.
    m_normalGeometry.clear();
}

void MainWindow::switchToProfile(const QString &name) {
    if (!m_profiles.contains(name) || name == m_profiles.current()) {
        rebuildProfileMenu(); // re-check the radio button a stray click moved
        return;
    }

    save();
    clearDesktop();
    m_profiles.setCurrent(name);
    load();
    rebuildProfileMenu();
    updateWindowTitle();
}

void MainWindow::newProfile() {
    const QString name = askProfileName(tr("New Profile"), QString());
    if (name.isEmpty() || !m_profiles.create(name)) return;

    save();
    clearDesktop();
    m_profiles.setCurrent(name);
    // Nothing saved under this name yet, so load() lays out a fresh desktop.
    load();
    rebuildProfileMenu();
    updateWindowTitle();
}

void MainWindow::duplicateProfile() {
    const QString source = m_profiles.current();
    // Copied from disk, so what is on screen has to be written out first.
    save();

    const QString name = askProfileName(tr("Duplicate Profile"),
                                        tr("%1 (copy)").arg(source));
    if (name.isEmpty() || !m_profiles.duplicate(source, name)) return;

    m_profiles.setCurrent(name);
    rebuildProfileMenu();
    updateWindowTitle();
}

void MainWindow::renameProfile() {
    const QString from = m_profiles.current();
    const QString to = askProfileName(tr("Rename Profile"), from);
    if (to.isEmpty() || to == from) return;
    if (!m_profiles.rename(from, to)) return;

    rebuildProfileMenu();
    updateWindowTitle();
}

void MainWindow::deleteProfile() {
    const QString name = m_profiles.current();
    if (m_profiles.names().size() <= 1) {
        QMessageBox::information(this, tr("Delete Profile"),
                                 tr("'%1' is the only profile; there has to be one.").arg(name));
        return;
    }

    const auto reply = QMessageBox::question(
        this, tr("Delete Profile"),
        tr("Delete profile '%1' and all the groups in it?").arg(name));
    if (reply != QMessageBox::Yes) return;

    if (!m_profiles.remove(name)) return;

    // remove() already moved current on to a surviving profile.
    clearDesktop();
    load();
    rebuildProfileMenu();
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

    if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
        if (auto *sub = qobject_cast<QMdiSubWindow *>(watched)) {
            const bool collapsed = sub->isMinimized() || m_minimizedIcons.contains(sub);
            if (!collapsed && sub->isVisible()) {
                m_normalGeometry[sub] = sub->geometry();
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
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

void MainWindow::applyTheme(const QString &themeName) {
    const Theme &theme = themeByName(themeName);
    m_themeName = theme.name;

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
}

void MainWindow::chooseColors() {
    const QString previous = m_themeName;
    ThemeDialog dialog(m_themeName, this);
    // Live preview on the real window, like the Control Panel did.
    connect(&dialog, &ThemeDialog::themePreviewed, this, &MainWindow::applyTheme);

    if (dialog.exec() == QDialog::Accepted) {
        applyTheme(dialog.selectedTheme());
    } else {
        applyTheme(previous);
    }
}

void MainWindow::about() {
    QMessageBox::about(
        this, tr("About Program Manager"),
        tr("Program Manager for Linux\n\n"
           "A Progman-style launcher for Windows programs, run through Wine."));
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
    const QString path = m_profiles.filePath(m_profiles.current());
    if (path.isEmpty() || !QFile::exists(path)) {
        // A profile that has never been saved starts on the stock desktop.
        applyTheme(builtinThemes().first().name);
        createDefaultGroups();
        return;
    }

    QSettings settings(path, QSettings::IniFormat);
    applyTheme(settings.value("theme", builtinThemes().first().name).toString());

    // XP icon styling used to be one switch for the whole Program Manager.
    // A desktop saved back then carries no per-item setting, so the switch is
    // read one last time and handed to the items it used to restyle; saving
    // the profile again writes them out individually and drops the key.
    const bool globalXpIcons = settings.value("xpStyleIcons", false).toBool();

    // Where the Program Manager window itself was last left. saveGeometry
    // carries the screen and the maximized state, not just a rectangle.
    // Only honoured at startup: moving the window out from under someone
    // mid-session, just because they switched profile, would be rude.
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
            item.command = settings.value("command").toString();
            item.arguments = settings.value("arguments").toString();
            item.workingDir = settings.value("workingDir").toString();
            item.runner = settings.value("runner").toString();
            item.iconIndex = settings.value("iconIndex", -1).toInt();
            item.iconSource = settings.value("iconSource").toString();
            item.xpStyleIcon = settings.value("xpStyleIcon", false).toBool();
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
    const QString path = m_profiles.filePath(m_profiles.current());
    if (path.isEmpty()) return;

    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    settings.setValue("theme", m_themeName);
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
            settings.setValue("command", items[j].command);
            settings.setValue("arguments", items[j].arguments);
            settings.setValue("workingDir", items[j].workingDir);
            // Only written when set, so a plain wine item stays as it was.
            if (!items[j].runner.isEmpty()) settings.setValue("runner", items[j].runner);
            // Likewise the icon: only an item wearing one somebody picked out
            // of the several its file holds has anything to remember.
            if (items[j].iconIndex >= 0) {
                settings.setValue("iconIndex", items[j].iconIndex);
                if (!items[j].iconSource.isEmpty()) {
                    settings.setValue("iconSource", items[j].iconSource);
                }
            }
            if (items[j].xpStyleIcon) settings.setValue("xpStyleIcon", true);
        }
        settings.endArray();
    }
    settings.endArray();
}
