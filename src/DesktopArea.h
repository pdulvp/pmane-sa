#pragma once

#include <QMdiArea>
#include <QString>

// The Program Manager desktop. Beyond being the MDI area the group
// windows live in, it signs itself with the name of the screen currently
// loaded, so which workspace is on screen is readable at a glance.
class DesktopArea : public QMdiArea {
public:
    explicit DesktopArea(QWidget *parent = nullptr);

    void setScreenName(const QString &name);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_screenName;
};
