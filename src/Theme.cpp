#include "Theme.h"

#include <QPainter>
#include <QPixmap>

namespace {

Theme make(const QString &name, const char *desktop, const char *titleActive,
           const char *titleActiveText, const char *titleInactive, const char *titleInactiveText,
           const char *windowBg, const char *windowText, const char *buttonFace,
           const char *buttonText, const char *highlight, const char *highlightText) {
    Theme t;
    t.name = name;
    t.desktop = QColor(desktop);
    t.titleActive = QColor(titleActive);
    t.titleActiveText = QColor(titleActiveText);
    t.titleInactive = QColor(titleInactive);
    t.titleInactiveText = QColor(titleInactiveText);
    t.windowBg = QColor(windowBg);
    t.windowText = QColor(windowText);
    t.buttonFace = QColor(buttonFace);
    t.buttonText = QColor(buttonText);
    t.highlight = QColor(highlight);
    t.highlightText = QColor(highlightText);
    return t;
}

} // namespace

QColor readableTextOn(const QColor &background) {
    return (background.isValid() && background.lightnessF() > 0.55) ? Qt::black : Qt::white;
}

QIcon tintedIcon(const QStringList &names, const QColor &ink) {
    QIcon source;
    for (const QString &name : names) {
        source = QIcon::fromTheme(name);
        if (!source.isNull()) break;
    }
    if (source.isNull()) return {};

    // Each size the icon offers, painted through: the shape is kept and the
    // colour thrown away, which is what tinting a one-colour icon means.
    QIcon tinted;
    for (int size : {16, 22, 24, 32, 48}) {
        QPixmap artwork = source.pixmap(size, size);
        if (artwork.isNull()) continue;

        QPixmap coloured(artwork.size());
        coloured.setDevicePixelRatio(artwork.devicePixelRatio());
        coloured.fill(Qt::transparent);

        QPainter painter(&coloured);
        painter.drawPixmap(0, 0, artwork);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(coloured.rect(), ink);
        painter.end();

        tinted.addPixmap(coloured);
    }
    return tinted;
}

const QVector<Theme> &builtinThemes() {
    static const QVector<Theme> themes = {
        //                     desktop    titleAct   titleActTx titleInact titleInactTx windowBg   windowTx   btnFace    btnText    highlight  highlightTx
        make("Windows Default", "#008080", "#000080", "#ffffff", "#808080", "#c0c0c0", "#ffffff", "#000000", "#c0c0c0", "#000000", "#000080", "#ffffff"),
        make("Pastel",          "#d8bfd8", "#9370db", "#ffffff", "#b8a8c8", "#efe8f4", "#fffafd", "#2c2130", "#e6e0ee", "#2c2130", "#9370db", "#ffffff"),
        make("Rose",            "#ffe4e1", "#c08081", "#ffffff", "#d8b8b6", "#fff0ef", "#fffaf9", "#3a2526", "#f3dedd", "#3a2526", "#c08081", "#ffffff"),
        make("Mint",            "#c8e6c9", "#4c9a83", "#ffffff", "#a9c9b4", "#eaf5ee", "#fbfffb", "#1f3329", "#dcecdf", "#1f3329", "#4c9a83", "#ffffff"),
        make("Sky",             "#b3d9f2", "#4a7fa5", "#ffffff", "#a8c2d4", "#e8f2f9", "#fbfdff", "#16303f", "#d8e8f4", "#16303f", "#4a7fa5", "#ffffff"),
        make("Wheat",           "#e8d9b5", "#8b6f3f", "#ffffff", "#c9b894", "#f5eedd", "#fffdf6", "#3a2f18", "#ece1c9", "#3a2f18", "#8b6f3f", "#ffffff"),
        make("Arizona",         "#c19a6b", "#804000", "#ffffff", "#a89078", "#e8dccc", "#fffaf0", "#2b1a08", "#d9c4a8", "#2b1a08", "#804000", "#ffffff"),
        make("The Blues",       "#4682b4", "#000080", "#ffffff", "#7590a8", "#d8e4ee", "#f0f4ff", "#000030", "#b0c4de", "#000030", "#000080", "#ffffff"),
        make("Emerald City",    "#2e8b57", "#004030", "#ffffff", "#6f9c85", "#dceee5", "#f4fff8", "#00241a", "#b8cfc2", "#00241a", "#004030", "#ffffff"),
        make("Ocean",           "#5f9ea0", "#2f4f4f", "#ffffff", "#8fb0b1", "#e0ecec", "#f6ffff", "#102626", "#c0d4d4", "#102626", "#2f4f4f", "#ffffff"),
        make("Monochrome",      "#808080", "#000000", "#ffffff", "#a0a0a0", "#e0e0e0", "#ffffff", "#000000", "#c0c0c0", "#000000", "#000000", "#ffffff"),
        make("Hotdog Stand",    "#ff0000", "#000000", "#ffff00", "#808080", "#ffff00", "#ffff00", "#000000", "#ffff00", "#000000", "#000000", "#ffff00"),
    };
    return themes;
}

const Theme &themeByName(const QString &name) {
    const QVector<Theme> &themes = builtinThemes();
    for (const Theme &t : themes) {
        if (t.name == name) return t;
    }
    return themes.first();
}

namespace {

// A colour at a given brightness, keeping the hue it is recognisable by and
// easing the saturation: a surface carries colour less well the darker it
// gets, and a dark grey with a trace of rose in it reads as rose where a
// fully saturated one reads as a mistake.
// A surface in the scheme's own colour: the hue taken from the scheme, the
// saturation set outright rather than scaled. Scaling it is what turned the
// dark halves into a row of near-blacks -- a soft caption has little
// saturation to begin with, and a fraction of little is nothing.
QColor tintedSurface(const QColor &hueFrom, qreal value, qreal saturation) {
    const QColor hsv = hueFrom.toHsv();
    if (hsv.hue() < 0) {
        // A scheme with no colour in it stays without: Monochrome's dark
        // half should be grey, not faintly red.
        return QColor::fromHsvF(0.0, 0.0, qBound(0.0, value, 1.0));
    }
    return QColor::fromHsvF(hsv.hue() / 360.0, qBound(0.0, saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

QColor atValue(const QColor &colour, qreal value, qreal saturation) {
    const QColor hsv = colour.toHsv();
    const int hue = hsv.hue() < 0 ? 0 : hsv.hue();
    // Over 360, not 359: the wheel closes, and dividing by one less walks
    // every hue a little way round -- which showed up as Rose coming back
    // one degree off itself.
    return QColor::fromHsvF(hue / 360.0, qBound(0.0, hsv.saturationF() * saturation, 1.0),
                            qBound(0.0, value, 1.0));
}

// Whatever was applied last, the first built-in until then, and whether its
// dark half is the one being worn.
QString g_currentThemeName;
bool g_dark = false;
Theme g_worn;
bool g_wornBuilt = false;

void rebuildWorn() {
    const Theme &base = themeByName(g_currentThemeName);
    g_worn = g_dark ? darkVariant(base) : base;
    g_wornBuilt = true;
}

} // namespace

Theme darkVariant(const Theme &light) {
    Theme dark = light;

    // The scheme is known by its caption colour, so that is what the dark
    // surfaces are tinted with -- unless the caption has no colour in it, in
    // which case the desktop says what the scheme is. Hotdog Stand's caption
    // is black and its desktop is red; the red is the part worth keeping.
    QColor character = light.titleActive;
    if (character.toHsv().saturationF() < 0.15) character = light.desktop;

    // The desktop is the lighter surface and the groups sit darker on it,
    // so a group reads as a window cut into the desktop rather than as a
    // panel laid over it. Set below, once there is a group to clear.
    dark.desktop = atValue(light.desktop, 0.26, 1.0);

    // The paper and the ink change places, each keeping a trace of the
    // scheme: easier on the eye than a flat grey on a flat black.
    dark.buttonFace = tintedSurface(character, 0.25, 0.30);
    dark.buttonText = tintedSurface(character, 0.92, 0.10);
    dark.windowBg = tintedSurface(character, 0.20, 0.34);
    dark.windowText = tintedSurface(character, 0.95, 0.08);

    // And now the desktop is lifted until it is plainly lighter than the
    // groups on it. A strong colour reads far darker than its brightness
    // suggests -- a saturated teal or red at the same brightness as a grey
    // comes out below it -- so the gap is measured rather than assumed.
    for (qreal value = 0.26;
         value < 0.65 && dark.desktop.lightnessF() < dark.windowBg.lightnessF() + 0.05;
         value += 0.03) {
        dark.desktop = atValue(light.desktop, value, 1.0);
    }

    // A caption that is already dark cannot go darker and still be seen
    // against the window behind it; one that is bright is left alone.
    const qreal captionLightness = light.titleActive.lightnessF();
    dark.titleActive = captionLightness < 0.30
                           ? atValue(light.titleActive, 0.45, 1.0)
                           : light.titleActive;
    dark.titleActiveText = readableTextOn(dark.titleActive);
    dark.titleInactive = atValue(light.titleInactive, 0.28, 0.7);
    dark.titleInactiveText = readableTextOn(dark.titleInactive);

    // What is selected keeps the colour it is selected in: the point of a
    // highlight is that it is the one thing not in the scheme's own key.
    dark.highlight = light.highlight;
    dark.highlightText = readableTextOn(light.highlight);
    return dark;
}

Theme blendThemes(const Theme &from, const Theme &to, qreal t) {
    const qreal mix = qBound(0.0, t, 1.0);
    const auto between = [mix](const QColor &a, const QColor &b) {
        return QColor(qRound(a.red() + (b.red() - a.red()) * mix),
                      qRound(a.green() + (b.green() - a.green()) * mix),
                      qRound(a.blue() + (b.blue() - a.blue()) * mix));
    };

    Theme blended = to; // the name and anything not a colour comes from the end
    blended.desktop = between(from.desktop, to.desktop);
    blended.titleActive = between(from.titleActive, to.titleActive);
    blended.titleActiveText = between(from.titleActiveText, to.titleActiveText);
    blended.titleInactive = between(from.titleInactive, to.titleInactive);
    blended.titleInactiveText = between(from.titleInactiveText, to.titleInactiveText);
    blended.windowBg = between(from.windowBg, to.windowBg);
    blended.windowText = between(from.windowText, to.windowText);
    blended.buttonFace = between(from.buttonFace, to.buttonFace);
    blended.buttonText = between(from.buttonText, to.buttonText);
    blended.highlight = between(from.highlight, to.highlight);
    blended.highlightText = between(from.highlightText, to.highlightText);
    return blended;
}

void setCurrentThemeColours(const Theme &theme) {
    g_worn = theme;
    g_wornBuilt = true;
}

bool darkMode() { return g_dark; }

void setDarkMode(bool dark) {
    if (dark == g_dark && g_wornBuilt) return;
    g_dark = dark;
    rebuildWorn();
}

const Theme &currentTheme() {
    if (!g_wornBuilt) rebuildWorn();
    return g_worn;
}

void setCurrentTheme(const QString &name) {
    g_currentThemeName = themeByName(name).name;
    rebuildWorn();
}
