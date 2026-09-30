#include "Theme.h"

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
// Whatever was applied last, the first built-in until then.
QString g_currentThemeName;
} // namespace

const Theme &currentTheme() { return themeByName(g_currentThemeName); }

void setCurrentTheme(const QString &name) { g_currentThemeName = themeByName(name).name; }
