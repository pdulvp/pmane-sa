#pragma once

#include <QDialog>
#include <QWidget>
#include "Theme.h"

class QComboBox;

// Paints a miniature Program Manager window in a given scheme, as the Windows
// 3.1 Color applet previewed its colours.
class ThemePreview : public QWidget {
    Q_OBJECT
public:
    explicit ThemePreview(QWidget *parent = nullptr);
    void setTheme(const Theme &theme);
    QSize sizeHint() const override { return QSize(260, 150); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Theme m_theme;
};

// "Colors" dialog: pick a scheme, see it previewed, apply on OK.
class ThemeDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThemeDialog(const QString &currentTheme, QWidget *parent = nullptr);

    QString selectedTheme() const;

signals:
    void themePreviewed(const QString &name);

private:
    QComboBox *m_combo;
    ThemePreview *m_preview;
};
