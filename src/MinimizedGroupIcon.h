#pragma once

#include <QWidget>

// A minimized Program Group, drawn as an icon on the Program Manager
// desktop rather than as a stub title bar, the way Progman did it.
// Double-clicking restores the group window.
class MinimizedGroupIcon : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCellWidth = 90;
    static constexpr int kCellHeight = 62;

    explicit MinimizedGroupIcon(const QString &title, QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setDesktopColor(const QColor &color);

signals:
    void restoreRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QString m_title;
    QColor m_desktop;
    bool m_selected = false;
};
