#pragma once

#include <QIcon>
#include <QVector>
#include <QWidget>

// A minimized Program Group, drawn as an icon on the Program Manager
// desktop rather than as a stub title bar, the way Progman did it. The
// icon previews the group's contents as a miniature window of program
// icons. Double-clicking restores the group.
class MinimizedGroupIcon : public QWidget {
    Q_OBJECT
public:
    static constexpr int kCellWidth = 92;
    static constexpr int kCellHeight = 78;
    // A 3x2 grid, as many as read clearly at this size.
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

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QString m_title;
    QColor m_desktop;
    QVector<QIcon> m_preview;
    bool m_selected = false;
};
