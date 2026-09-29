#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>

#include "ProfileManager.h"

class QActionGroup;
class QMdiSubWindow;
class QMenu;
class DesktopArea;
class GroupWindow;
class MinimizedGroupIcon;

// The main "Program Manager" window: an MDI desktop holding Program
// Group windows, with progman.ini-style persistence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    // startupProfile, when given and known, overrides the profile that was
    // open last.
    explicit MainWindow(const QString &startupProfile = QString(), QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    // Loads a profile by name, saving whatever is on screen first. Also how
    // a second launch with --profile reaches the running instance.
    void switchToProfile(const QString &name);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void newGroup();
    void newItem();
    void deleteGroup();
    void updateWindowMenu();
    void onSubWindowActivated(QMdiSubWindow *active);
    void chooseColors();
    void applyXpStyleIcons(bool enabled);
    void about();
    void newProfile();
    void duplicateProfile();
    void renameProfile();
    void deleteProfile();

private:
    void buildMenus();
    void rebuildProfileMenu();
    void updateWindowTitle();
    GroupWindow *activeGroup() const;
    QMdiSubWindow *addGroupWindow(GroupWindow *group);
    void load();
    void save();
    // Tears the desktop down to bare background, ready for another profile.
    void clearDesktop();
    // Asks for a profile name, rejecting blanks and names already in use.
    QString askProfileName(const QString &windowTitle, const QString &initial);
    void createDefaultGroups();
    void applyTheme(const QString &themeName);

    QPalette captionPalette(const QPalette &base) const;
    void minimizeToIcon(QMdiSubWindow *sub);
    void restoreFromIcon(QMdiSubWindow *sub);
    void arrangeMinimizedIcons();
    void clearMinimizedIconSelection();
    void clearGroupSelections();

    DesktopArea *m_mdiArea;
    QMenu *m_windowMenu;
    QMenu *m_profileMenu;
    // Keeps the one radio button beside the open profile exclusive.
    QActionGroup *m_profileGroup;
    QAction *m_saveOnExitAction;
    QAction *m_xpIconsAction;
    QString m_themeName;
    ProfileManager m_profiles;

    // Groups shown as desktop icons instead of stub title bars, in the order
    // they were minimized.
    QHash<QMdiSubWindow *, MinimizedGroupIcon *> m_minimizedIcons;
    QList<QMdiSubWindow *> m_minimizedOrder;

    // Each group's geometry while open. Qt collapses a minimized window to
    // its stub and normalGeometry() does not survive hiding it, so the real
    // rect has to be remembered as the window is moved and resized.
    QHash<QMdiSubWindow *, QRect> m_normalGeometry;
};
