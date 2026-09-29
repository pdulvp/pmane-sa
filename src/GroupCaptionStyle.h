#pragma once

#include <QProxyStyle>

// The caption a Program Group wears: its title and a minimize box, and
// nothing else.
//
// A group is never closed -- it leaves the desktop through File > Delete
// Group, which asks first -- and never maximized, being a window on a
// desktop rather than a workspace of its own. The system menu box goes with
// them, what it offered being those same commands; the menu itself is still
// there on a right click of the caption.
//
// A QMdiSubWindow draws its caption through the style rather than with
// widgets of its own, and which buttons it offers comes from the window
// flags, not from anything a caller can switch off. Rewriting the option on
// the way through the style is what removes them: the same call decides
// where the buttons sit, which are painted, and which one a click landed on,
// so each button stops being drawn, stops reserving room, and stops
// responding, all at once.
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
