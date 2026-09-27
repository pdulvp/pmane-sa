#pragma once

#include <QMainWindow>

class QMdiArea;
class QMdiSubWindow;
class QMenu;
class GroupWindow;

// The main "Program Manager" window: an MDI desktop holding Program
// Group windows, with progman.ini-style persistence.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void newGroup();
    void newItem();
    void deleteGroup();
    void updateWindowMenu();
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

    QMdiArea *m_mdiArea;
    QMenu *m_windowMenu;
    QAction *m_saveOnExitAction;
    QAction *m_xpIconsAction;
    QString m_themeName;
};
