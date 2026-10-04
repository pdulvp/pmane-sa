#include "Ids.h"

#include <QRandomGenerator>

QString newId() {
    // Eight hex digits: short enough to sit in a folder name without making
    // it unreadable, and one in four thousand million.
    return QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8,
                                                                                     QLatin1Char('0'));
}
