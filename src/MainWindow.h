#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>

#include "AlignmentGuides.h"
#include "ScreenManager.h"

class QActionGroup;
class QMdiSubWindow;
class QMenu;
class DesktopArea;
class GroupWindow;
class SearchProvider;
class MinimizedGroupIcon;
class TitleBar;

// The main "Program Manager" window: an MDI desktop holding Program
// Group windows, with progman.ini-style persistence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // startupScreen, when given and known, overrides the screen that was
    // open last.
    explicit MainWindow(const QString &startupScreen = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    // Loads a screen by name, saving whatever is on the desktop first. Also how
    // a second launch with --profile reaches the running instance.
    void switchToScreen(const QString &name);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    // The window draws its own frame, so the band along its edges is where
    // a resize starts and what the pointer reports.
    void changeEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void newGroup();
    void newItem();
    // Adds one of the applications installed on the machine to the active
    // group, picked from a list of them.
    void insertApplication();
    void insertSteamGame();
    void deleteGroup();
    // Renames the active group, or the one whose caption was double clicked.
    void renameGroup(QMdiSubWindow *sub = nullptr);
    void updateWindowMenu();
    void onSubWindowActivated(QMdiSubWindow *active);
    void chooseColors();
    // What each kind of file is started with: the programs and the file types
    // they take.
    void editLaunchers();
    void about();
    void newScreen();
    void duplicateScreen();
    void renameScreen();
    void deleteScreen();

private:
    void buildMenus();
    // Asks systemd to suspend, restart or shut the machine down, after
    // asking the user whether that is really what they meant. `verb` is the
    // systemctl one; the desktop is saved first for the two that end the
    // session.
    void requestSystemState(const QString &verb, const QString &question, bool endsSession);
    void rebuildScreenMenu();
    void updateWindowTitle();
    GroupWindow *activeGroup() const;

public:
    // Brings an item into view and selects it: the screen it is on, the
    // group holding it, raised and selected. What the desktop's own search
    // does when one of this application's results is clicked.
    bool revealItem(const QString &screen, int groupIndex, int itemIndex,
                    const QString &itemName);
    // The groups on show now, in the order they were built: what the search
    // offers, and what revealItem() counts through.
    QVector<GroupWindow *> groups() const;
    QString currentScreen() const;

private:
    QMdiSubWindow *addGroupWindow(GroupWindow *group);
    void load();
    void save();
    // Tears the desktop down to bare background, ready for another screen.
    void clearDesktop();
    // Asks for a screen name, rejecting blanks and names already in use.
    QString askScreenName(const QString &windowTitle, const QString &initial);
    void createDefaultGroups();
    void applyTheme(const QString &themeName);

    QPalette captionPalette(const QPalette &base) const;
    // While a group is resized, holds the edge being dragged to the sides of
    // the groups around it. Called for each step of the resize; does nothing
    // outside one.
    void snapWhileResizing(QMdiSubWindow *sub,
                           int reach = AlignmentGuides::kSnapDistance);

    void minimizeToIcon(QMdiSubWindow *sub);
    void restoreFromIcon(QMdiSubWindow *sub);
    void arrangeMinimizedIcons();
    void clearMinimizedIconSelection();
    void clearGroupSelections();

    DesktopArea *m_mdiArea;
    QMenu *m_windowMenu;
    QMenu *m_screenMenu;
    // Keeps the one radio button beside the open screen exclusive.
    QActionGroup *m_screenGroup;
    QAction *m_saveOnExitAction;
    // The menu bar, the window title and the caption buttons in one strip.
    TitleBar *m_titleBar;
    // The alignment lines shown while a group is resized, and the group
    // being resized with the size it was when the drag began -- what says
    // which of its edges are the ones being dragged.
    AlignmentGuides *m_guides;
    QMdiSubWindow *m_resizingSub = nullptr;
    QRect m_resizeStartGeometry;
    // Set while a snap is being applied, so the geometry we set ourselves
    // does not come back round as another step of the resize.
    bool m_snapping = false;
    QString m_themeName;
    ScreenManager m_screens;
    // Answers the desktop's own search, where there is one listening.
    SearchProvider *m_searchProvider = nullptr;

    // Groups shown as desktop icons instead of stub title bars, in the order
    // they were minimized.
    QHash<QMdiSubWindow *, MinimizedGroupIcon *> m_minimizedIcons;
    QList<QMdiSubWindow *> m_minimizedOrder;

    // Each group's geometry while open. Qt collapses a minimized window to
    // its stub and normalGeometry() does not survive hiding it, so the real
    // rect has to be remembered as the window is moved and resized.
    QHash<QMdiSubWindow *, QRect> m_normalGeometry;
};
