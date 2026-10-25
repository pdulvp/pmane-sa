#pragma once

#include <QProxyStyle>

// The caption of a Program Group: its title and a minimize box, nothing else.
//
// A group is never closed, leaving the desktop through File > Delete Group,
// which asks first, and never maximized, being a window on a desktop rather
// than a workspace. The system menu box goes too, since it offered those same
// commands; the menu itself is still available on a right click of the caption.
//
// A QMdiSubWindow draws its caption through the style rather than with widgets,
// and which buttons it offers comes from the window flags, which a caller
// cannot switch off. Rewriting the style option is what removes them: the same
// calls decide where the buttons sit, which are painted, and which one a click
// hit, so a button stops being drawn, stops reserving room and stops responding
// at once.
class GroupCaptionStyle : public QProxyStyle {
    Q_OBJECT
public:
    using QProxyStyle::QProxyStyle;

    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget) const override;
    SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                                     const QPoint &pos, const QWidget *widget) const override;
};
