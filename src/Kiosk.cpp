#include "Kiosk.h"

#include <QSettings>

namespace {

// Kept with the application rather than with a screen: which machine this
// is does not change when the desktop on show does.
const char *kGroup = "kiosk";

KioskSettings *loaded() {
    static KioskSettings settings;
    static bool read = false;
    if (read) return &settings;
    read = true;

    QSettings store;
    store.beginGroup(QLatin1String(kGroup));
    const KioskSettings defaults;
    settings.locked = store.value(QStringLiteral("locked"), defaults.locked).toBool();
    settings.showCalendar =
        store.value(QStringLiteral("showCalendar"), defaults.showCalendar).toBool();
    settings.showVolume =
        store.value(QStringLiteral("showVolume"), defaults.showVolume).toBool();
    settings.showCalculator =
        store.value(QStringLiteral("showCalculator"), defaults.showCalculator).toBool();
    settings.showSearch =
        store.value(QStringLiteral("showSearch"), defaults.showSearch).toBool();
    store.endGroup();
    return &settings;
}

} // namespace

const KioskSettings &kioskSettings() { return *loaded(); }

void setKioskSettings(const KioskSettings &settings) {
    *loaded() = settings;

    QSettings store;
    store.beginGroup(QLatin1String(kGroup));
    store.setValue(QStringLiteral("locked"), settings.locked);
    store.setValue(QStringLiteral("showCalendar"), settings.showCalendar);
    store.setValue(QStringLiteral("showVolume"), settings.showVolume);
    store.setValue(QStringLiteral("showCalculator"), settings.showCalculator);
    store.setValue(QStringLiteral("showSearch"), settings.showSearch);
    store.endGroup();
}
