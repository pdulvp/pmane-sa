#pragma once

#include <QIcon>
#include <QVector>
#include <QWidget>

// A minimized Program Group, drawn as an icon on the Program Manager desktop
// rather than as a stub title bar, as Progman did. The icon previews the
// group's contents as a miniature window of program icons. Double-clicking
// restores the group.
class MinimizedGroupIcon : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCellWidth = 92;
    static constexpr int kCellHeight = 78;
    // A 3x2 grid, as many as stay legible at this size.
    static constexpr int kPreviewColumns = 3;
    static constexpr int kPreviewRows = 2;
    static constexpr int kMaxPreviewIcons = kPreviewColumns * kPreviewRows;

    explicit MinimizedGroupIcon(const QString &title, QWidget *parent = nullptr);

    void setTitle(const QString &title);
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void setDesktopColor(const QColor &color);
    void setPreviewIcons(const QVector<QIcon> &icons);

signals:
    void restoreRequested();
    void clicked();
    // Right-clicked. A minimized group offers the same commands as its caption
    // does when open, and the Program Manager builds that menu, not this icon.
    void contextMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QString m_title;
    QColor m_desktop;
    QVector<QIcon> m_preview;
    bool m_selected = false;
};
