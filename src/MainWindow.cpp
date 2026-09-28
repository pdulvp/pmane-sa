#include "MainWindow.h"
#include "GroupWindow.h"
#include "MinimizedGroupIcon.h"
#include "Theme.h"
#include "ThemeDialog.h"

#include <QAction>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QLineEdit>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle(tr("Program Manager"));
    resize(900, 650);

    m_mdiArea = new QMdiArea(this);
    m_mdiArea->setActivationOrder(QMdiArea::ActivationHistoryOrder);
    setCentralWidget(m_mdiArea);

    buildMenus();
    applyTheme(builtinThemes().first().name);
    load();

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
    m_xpIconsAction = optionsMenu->addAction(tr("&XP Style Icons"));
    m_xpIconsAction->setCheckable(true);
    connect(m_xpIconsAction, &QAction::toggled, this, &MainWindow::applyXpStyleIcons);
    optionsMenu->addSeparator();
    m_saveOnExitAction = optionsMenu->addAction(tr("&Save Settings on Exit"));
    m_saveOnExitAction->setCheckable(true);
    m_saveOnExitAction->setChecked(true);

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
    sub->setWindowTitle(group->title());
    sub->resize(320, 240);
    connect(group, &GroupWindow::titleChanged, sub, &QMdiSubWindow::setWindowTitle);

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

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_mdiArea->viewport() && event->type() == QEvent::Resize) {
        arrangeMinimizedIcons();
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

void MainWindow::applyXpStyleIcons(bool enabled) {
    for (QMdiSubWindow *sub : m_mdiArea->subWindowList()) {
        if (auto *group = qobject_cast<GroupWindow *>(sub->widget())) {
            group->setXpStyleIcons(enabled);
        }
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

QString MainWindow::configFilePath() const {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir + "/groups.ini";
}

void MainWindow::createDefaultGroups() {
    addGroupWindow(new GroupWindow(tr("Main")))->setGeometry(20, 20, 320, 240);
    addGroupWindow(new GroupWindow(tr("StartUp")))->setGeometry(360, 20, 320, 240);
}

void MainWindow::load() {
    QString path = configFilePath();
    if (!QFile::exists(path)) {
        createDefaultGroups();
        return;
    }

    QSettings settings(path, QSettings::IniFormat);
    applyTheme(settings.value("theme", m_themeName).toString());

    // Where the Program Manager window itself was last left. saveGeometry
    // carries the screen and the maximized state, not just a rectangle.
    const QByteArray windowGeometry = settings.value("windowGeometry").toByteArray();
    if (!windowGeometry.isEmpty()) restoreGeometry(windowGeometry);

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
            group->addProgramItem(item);
        }
        settings.endArray();

        QMdiSubWindow *sub = addGroupWindow(group);
        sub->setGeometry(x, y, w, h);
        m_normalGeometry[sub] = QRect(x, y, w, h);
        if (minimized) minimizeToIcon(sub);
    }
    settings.endArray();

    if (groupCount == 0) createDefaultGroups();

    // After the groups exist, so each one picks the setting up.
    m_xpIconsAction->setChecked(settings.value("xpStyleIcons", false).toBool());
}

void MainWindow::save() {
    QSettings settings(configFilePath(), QSettings::IniFormat);
    settings.clear();
    settings.setValue("theme", m_themeName);
    settings.setValue("windowGeometry", saveGeometry());
    settings.setValue("xpStyleIcons", m_xpIconsAction->isChecked());

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
        }
        settings.endArray();
    }
    settings.endArray();
}
