#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>

#include "DesktopColorScheme.h"
#include "Theme.h"

#include "AlignmentGuides.h"
#include "ProgramItem.h"
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
class NotificationArea;
class ScreenTransition;
class TitleBar;

// The main "Program Manager" window: an MDI desktop holding Program Group
// windows, with progman.ini-style persistence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // startupScreen, when given and known, overrides the screen last in use.
    explicit MainWindow(const QString &startupScreen = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    // Loads a screen by name, saving the current desktop first. Also the entry
    // point a second launch with --profile uses on the running instance.
    void switchToScreen(const QString &name);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    // The window draws its own frame, so these handle resizing from the
    // margin along its edges and the matching cursor shape.
    void changeEvent(QEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private slots:
    void newGroup();
    void newItem();
    // Adds an installed application to the active group, picked from a list.
    void insertApplication();
    void insertSteamGame();
    // Deletes the group given, or the active one. The argument is needed
    // because a minimized group is hidden, and a hidden window cannot be made
    // active.
    void deleteGroup(QMdiSubWindow *sub = nullptr);
    // Removes a group from the desktop whatever state it is in: its window,
    // its minimized icon, and its recorded geometry. The caller decides what
    // happens to its items.
    void removeGroupWindow(QMdiSubWindow *sub);
    // Renames the active group, or the one whose caption was double clicked.
    void renameGroup(QMdiSubWindow *sub = nullptr);
    // Moves a group to another screen: off this desktop and into that
    // screen's file, which is not loaded.
    void moveGroupToScreen(QMdiSubWindow *sub, const QString &screen);
    // The menu shown on a right click on a group's caption. The window's own
    // system menu is Qt's, reachable from the box at the left of the caption.
    void showGroupCaptionMenu(QMdiSubWindow *sub, const QPoint &where);
    // Fills a menu with every screen but the one the group is on. Filled as
    // the menu opens, since screens are added, renamed and deleted while the
    // desktop is running.
    void fillMoveToScreenMenu(QMenu *menu, QMdiSubWindow *sub);
    void updateWindowMenu();
    void onSubWindowActivated(QMdiSubWindow *active);
    void chooseColors();
    // The desktop's own settings, the wallpaper among them.
    void editSettings();
    // The launcher programs and the file types each one handles.
    void editLaunchers();
    void about();
    void newScreen();
    void duplicateScreen();
    void renameScreen();
    void deleteScreen();

private:
    void buildMenus();
    // Asks systemd to suspend, restart or shut the machine down, after
    // confirming with the user. `verb` is the systemctl verb; the desktop is
    // saved first for the two that end the session.
    void requestSystemState(const QString &verb, const QString &question, bool endsSession);
    void rebuildScreenMenu();
    void updateWindowTitle();
    GroupWindow *activeGroup() const;

public:
    // Brings an item into view and selects it, switching screens and raising
    // its group as needed. Called when one of this application's results is
    // clicked in the desktop search.
    bool revealItem(const QString &screen, int groupIndex, int itemIndex,
                    const QString &itemName);
    // The groups currently on the desktop, in creation order: what the search
    // offers and what revealItem() indexes into.
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
    // `moved` animates from the colours on screen to the new ones. Used when
    // the whole desktop changes at once, such as the dark mode toggle, but not
    // while arrowing through the Colors dialog, where each step would blend
    // into the next and feel slow.
    void applyTheme(const QString &themeName, bool moved = false);
    // Applies these colours to every palette in the application and to the
    // surfaces the dialogs paint themselves. Called per frame of an animated
    // change, or once for an immediate one.
    void applyThemeColours(const Theme &theme);

    // Follows the desktop between light and dark, so this window does not
    // stay bright when the rest of the session has gone dark.
    void followDesktopScheme(DesktopScheme scheme);
    DesktopColorSchemeWatch *m_schemeWatch = nullptr;

    // Reads the desktop's wallpaper, or clears it, according to the setting.
    // Called whenever anything it depends on could have changed: the setting,
    // the light/dark scheme, which can have different pictures, and the
    // desktop background itself.
    void refreshDesktopWallpaper();
    GnomeWallpaper *m_wallpaperWatch = nullptr;
    bool m_useDesktopWallpaper = false;

    QVariantAnimation *m_themeFade = nullptr;
    // The colours actually on screen. currentTheme() gives the configured
    // theme, which changes as soon as dark mode is switched; this holds what
    // is still displayed, which is where an animation starts from.
    Theme m_wornTheme;



    QPalette captionPalette(const QPalette &base) const;
    // Snaps the edge being dragged to the sides of the neighbouring groups.
    // Called at each step of a resize; does nothing outside one.
    void snapWhileResizing(QMdiSubWindow *sub,
                           int reach = AlignmentGuides::kSnapDistance);
    // The same while a group is being moved: the whole window is shifted onto
    // a neighbour's edge rather than one edge being held to it.
    void snapWhileMoving(QMdiSubWindow *sub, int reach = AlignmentGuides::kSnapDistance);

    // Moves to the screen `step` places away in menu order, which is what a
    // horizontal wheel step over the bare desktop does. Stops at either end:
    // the list does not wrap.
    void stepToNeighbourScreen(int step);

    // The slide shown while one screen replaces another, drawn over the
    // desktop. Created on the MDI area's viewport.
    ScreenTransition *m_screenSlide = nullptr;
    // Horizontal wheel movement not yet spent on a screen change. A notch is
    // 120 units; a touchpad or a free-spinning wheel sends smaller amounts,
    // which are added up here rather than taken as a step each.
    int m_wheelTravel = 0;

    void minimizeToIcon(QMdiSubWindow *sub);
    void restoreFromIcon(QMdiSubWindow *sub);
    void arrangeMinimizedIcons();
    void clearMinimizedIconSelection();
    void clearGroupSelections();

    DesktopArea *m_mdiArea;
    // Where a failed launch is reported, in the corner of the desktop.
    NotificationArea *m_notices;
    QMenu *m_windowMenu;
    // Kept so their icons can be recoloured when the scheme changes: menus
    // are drawn on the scheme's button colour, and a monochrome icon has to
    // be tinted to stay legible on it.
    QAction *m_suspendAction = nullptr;
    QAction *m_restartAction = nullptr;
    QAction *m_shutdownAction = nullptr;

    // The position of every group window, one entry per group in desktop
    // order. A rectangle and a flag each, so keeping a long history of them
    // is cheap.
    struct LayoutSnapshot {
        QVector<QRect> geometry;
        QVector<bool> minimized;
        bool operator==(const LayoutSnapshot &other) const {
            return geometry == other.geometry && minimized == other.minimized;
        }
        bool operator!=(const LayoutSnapshot &other) const { return !(*this == other); }
    };

    // One undoable change to the desktop. Deletions are the reason this holds
    // more than arrangements: an item or a whole group goes on a single
    // confirmation click, and nothing but undo brings it back.
    struct Edit {
        enum Kind {
            Arrangement, // the group windows moved or resized
            ItemDeleted,
            GroupDeleted,
        };
        Kind kind = Arrangement;

        // Arrangement: the positions before and after.
        LayoutSnapshot before;
        LayoutSnapshot after;

        // The group affected, identified by id rather than position: the
        // desktop is rearranged between an edit and its undo, and a restored
        // group comes back at the end of the order.
        QString group;

        // ItemDeleted: the item and its position in the group.
        int itemIndex = -1;
        ProgramItem item;

        // GroupDeleted: everything needed to recreate the group.
        QString title;
        QVector<ProgramItem> items;
        QRect geometry;
        bool minimized = false;
    };

    LayoutSnapshot currentLayout() const;
    void applyLayout(const LayoutSnapshot &layout);
    // Called once a move or resize has settled, so one drag is one entry
    // rather than one per step.
    void recordLayoutChange();
    void record(const Edit &edit);
    void undoEdit();
    void redoEdit();
    // Applies one edit in either direction: undoing a deletion restores it,
    // redoing it removes it again.
    void applyEdit(const Edit &edit, bool undoing);
    void updateEditActions();
    // The group with this id, or null when it is no longer on the desktop.
    GroupWindow *groupById(const QString &id) const;
    // Adding or removing a group invalidates the recorded arrangements, which
    // are lists of windows by position. The recorded deletions are kept,
    // since each one carries everything it needs.
    void forgetArrangements();
    // Drops the whole history, for when the desktop is replaced outright.
    void forgetEdits();

    QVector<Edit> m_undo;
    QVector<Edit> m_redo;
    LayoutSnapshot m_layoutNow;
    QTimer *m_layoutSettle = nullptr;
    // Set while an arrangement is being restored, so the restore is not itself
    // recorded as a change.
    bool m_applyingLayout = false;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;

    QMenu *m_screenMenu;
    // Makes the per-screen radio buttons mutually exclusive.
    QActionGroup *m_screenGroup;
    QAction *m_saveOnExitAction;
    // The menu bar, the window title and the caption buttons in one strip.
    TitleBar *m_titleBar;
    // The alignment guides shown while a group is dragged, plus the group
    // being dragged and its geometry when the drag began, from which the
    // edges being moved are deduced.
    AlignmentGuides *m_guides;
    QMdiSubWindow *m_resizingSub = nullptr;
    QRect m_resizeStartGeometry;
    // Set while a snap is being applied, so the geometry we set does not come
    // back as another step of the resize.
    bool m_snapping = false;
    QString m_themeName;
    ScreenManager m_screens;
    // Answers the desktop search, when one is listening on the bus.
    SearchProvider *m_searchProvider = nullptr;

    // Groups shown as desktop icons instead of stub title bars, in the order
    // they were minimized.
    QHash<QMdiSubWindow *, MinimizedGroupIcon *> m_minimizedIcons;
    QList<QMdiSubWindow *> m_minimizedOrder;

    // Each group's geometry while open. Qt collapses a minimized window to its
    // stub and normalGeometry() does not survive hiding it, so the real
    // rectangle is recorded as the window is moved and resized.
    QHash<QMdiSubWindow *, QRect> m_normalGeometry;
};
