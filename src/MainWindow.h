#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>

#include "DesktopColorScheme.h"
#include "Theme.h"

#include "AlignmentGuides.h"
#include "ScreenManager.h"

class QActionGroup;
class QMdiSubWindow;
class QMenu;
class DesktopArea;
class GroupWindow;
class DesktopColorSchemeWatch;
class GnomeWallpaper;
class QVariantAnimation;
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
    // The desktop's own switches, the wallpaper among them.
    void editSettings();
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
    // `moved` eases from the colours on screen to the new ones. Wanted when
    // the whole desktop changes at once -- the dark switch -- and not while
    // picking through schemes in the Colors dialog, where each step would
    // smear into the next and the list would feel slow.
    void applyTheme(const QString &themeName, bool moved = false);
    // Puts these colours on: every palette in the application, and the
    // surfaces the dialogs paint themselves. One frame of a move, or the
    // whole of a change that is not moved to.
    void applyThemeColours(const Theme &theme);

    // Follows the desktop between light and dark, so the Program Manager
    // is not the one window still bright when everything else has dimmed.
    void followDesktopScheme(DesktopScheme scheme);
    DesktopColorSchemeWatch *m_schemeWatch = nullptr;

    // Reads the desktop's wallpaper, or clears it, according to the
    // setting. Called whenever either end could have changed: the switch,
    // the scheme -- light and dark can carry different pictures -- and the
    // desktop's own background being swapped.
    void refreshDesktopWallpaper();
    GnomeWallpaper *m_wallpaperWatch = nullptr;
    bool m_useDesktopWallpaper = false;

    QVariantAnimation *m_themeFade = nullptr;
    // The colours actually on screen. currentTheme() says what the desktop
    // is set to, which changes the moment dark mode is flipped -- this says
    // what it still looks like, which is where a move has to start from.
    Theme m_wornTheme;



    QPalette captionPalette(const QPalette &base) const;
    // While a group is resized, holds the edge being dragged to the sides of
    // the groups around it. Called for each step of the resize; does nothing
    // outside one.
    void snapWhileResizing(QMdiSubWindow *sub,
                           int reach = AlignmentGuides::kSnapDistance);
    // The same while a group is being carried: the lines are its
    // neighbours' sides, and the whole window is shifted onto them rather
    // than any one edge being held.
    void snapWhileMoving(QMdiSubWindow *sub, int reach = AlignmentGuides::kSnapDistance);

    void minimizeToIcon(QMdiSubWindow *sub);
    void restoreFromIcon(QMdiSubWindow *sub);
    void arrangeMinimizedIcons();
    void clearMinimizedIconSelection();
    void clearGroupSelections();

    DesktopArea *m_mdiArea;
    QMenu *m_windowMenu;
    // Kept so their icons can be recoloured when the scheme changes: a menu
    // is drawn on the scheme's button colour, and a one-colour icon has to
    // be given ink that reads against it.
    QAction *m_suspendAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_shutdownAction = nullptr;

    // Where the group windows were: one entry per group, in the order the
    // desktop holds them. Small enough -- a rectangle and a flag each --
    // that keeping a long run of them costs nothing worth counting.
    struct LayoutSnapshot {
        QVector<QRect> geometry;
        QVector<bool> minimized;
        bool operator==(const LayoutSnapshot &other) const {
            return geometry == other.geometry && minimized == other.minimized;
        }
        bool operator!=(const LayoutSnapshot &other) const { return !(*this == other); }
    };

    LayoutSnapshot currentLayout() const;
    void applyLayout(const LayoutSnapshot &layout);
    // Called as a move or a resize settles. One entry per gesture, not one
    // per step of it, which is what the pause is for.
    void recordLayoutChange();
    void undoLayoutChange();
    void redoLayoutChange();
    void updateLayoutActions();
    // A group added or taken away makes the remembered arrangements
    // meaningless: they are a list of windows by position.
    void forgetLayoutHistory();

    QVector<LayoutSnapshot> m_layoutUndo;
    QVector<LayoutSnapshot> m_layoutRedo;
    LayoutSnapshot m_layoutNow;
    QTimer *m_layoutSettle = nullptr;
    // Set while an arrangement is being put back, so putting it back is not
    // itself remembered as a change.
    bool m_applyingLayout = false;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;

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
