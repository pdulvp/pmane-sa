#pragma once

#include <QHash>
#include <QList>
#include <QMainWindow>

class QMdiArea;
class QMdiSubWindow;
class QMenu;
class GroupWindow;
class MinimizedGroupIcon;

// The main "Program Manager" window: an MDI desktop holding Program
// Group windows, with progman.ini-style persistence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

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

private:
    void buildMenus();
    GroupWindow *activeGroup() const;
    QMdiSubWindow *addGroupWindow(GroupWindow *group);
    QString configFilePath() const;
    void load();
    void save();
    void createDefaultGroups();
    void applyTheme(const QString &themeName);

    QPalette captionPalette(const QPalette &base) const;
    void minimizeToIcon(QMdiSubWindow *sub);
    void restoreFromIcon(QMdiSubWindow *sub);
    void arrangeMinimizedIcons();
    void clearMinimizedIconSelection();
    void clearGroupSelections();

    QMdiArea *m_mdiArea;
    QMenu *m_windowMenu;
    QAction *m_saveOnExitAction;
    QAction *m_xpIconsAction;
    QString m_themeName;

    // Groups shown as desktop icons instead of stub title bars, in the order
    // they were minimized.
    QHash<QMdiSubWindow *, MinimizedGroupIcon *> m_minimizedIcons;
    QList<QMdiSubWindow *> m_minimizedOrder;

    // Each group's geometry while open. Qt collapses a minimized window to
    // its stub and normalGeometry() does not survive hiding it, so the real
    // rect has to be remembered as the window is moved and resized.
    QHash<QMdiSubWindow *, QRect> m_normalGeometry;
};
