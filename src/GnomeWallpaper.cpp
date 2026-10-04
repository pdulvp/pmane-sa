#include "GnomeWallpaper.h"

#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QUrl>
#include <QXmlStreamReader>

#include <cmath>

namespace {

const char *kSchema = "org.gnome.desktop.background";

// Light and dark desktops can carry different pictures, and GNOME keeps
// them in two keys.
const char *kKey = "picture-uri";
const char *kDarkKey = "picture-uri-dark";

QString gsettingsGet(const QString &key) {
    QProcess process;
    process.start(QStringLiteral("gsettings"),
                  {QStringLiteral("get"), QLatin1String(kSchema), key});
    // A setting read is a local lookup; anything slower than this means
    // there is nothing to wait for.
    if (!process.waitForFinished(2000) || process.exitCode() != 0) return QString();

    QString value = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    // gsettings prints strings as GVariant text, quoted and escaped.
    if (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\'')) &&
        value.size() >= 2) {
        value = value.mid(1, value.size() - 2);
        value.replace(QLatin1String("\\'"), QLatin1String("'"));
        value.replace(QLatin1String("\\\\"), QLatin1String("\\"));
    }
    return value;
}

// A themed wallpaper is an XML file listing one picture per screen size,
// so the file the setting names is not always the file to show. Pulls out
// the sizes it offers and picks the nearest to the screen.
QString pictureFromTheme(const QString &xmlPath, const QSize &preferred) {
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();

    QString best;
    double bestDistance = 0;
    // Takes whichever is closest in shape and size; with no screen size to
    // go by every candidate ties at zero, so the first one offered wins.
    const auto offer = [&](const QString &path, const QSize &offered) {
        if (path.isEmpty()) return;
        double distance = 0;
        if (preferred.isValid() && offered.isValid()) {
            distance = std::hypot(double(offered.width() - preferred.width()),
                                  double(offered.height() - preferred.height()));
        }
        if (best.isEmpty() || distance < bestDistance) {
            best = path;
            bestDistance = distance;
        }
    };

    // A themed wallpaper nests one <size> per screen size inside <file>;
    // a plain one writes the path as the text of <file> itself. Both are
    // read in one pass rather than assuming which kind this is.
    bool inFile = false;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement:
            if (xml.name() == QLatin1String("file")) {
                inFile = true;
            } else if (inFile && xml.name() == QLatin1String("size")) {
                const QSize offered(xml.attributes().value(QLatin1String("width")).toInt(),
                                    xml.attributes().value(QLatin1String("height")).toInt());
                offer(xml.readElementText().trimmed(), offered);
            }
            break;
        case QXmlStreamReader::Characters:
            if (inFile && !xml.isWhitespace()) offer(xml.text().toString().trimmed(), QSize());
            break;
        case QXmlStreamReader::EndElement:
            if (xml.name() == QLatin1String("file")) inFile = false;
            break;
        default:
            break;
        }
    }
    return best;
}

} // namespace

QString GnomeWallpaper::currentPath(bool dark, const QSize &preferred) {
    // The dark picture only when the desktop is dark, and the ordinary one
    // when the dark key is unset -- which is how GNOME leaves it unless
    // someone has chosen a dark wallpaper of their own.
    QString uri = dark ? gsettingsGet(QLatin1String(kDarkKey)) : QString();
    if (uri.isEmpty()) uri = gsettingsGet(QLatin1String(kKey));
    if (uri.isEmpty()) return QString();

    const QString path = uri.startsWith(QLatin1String("file:")) ? QUrl(uri).toLocalFile() : uri;
    if (path.isEmpty() || !QFileInfo::exists(path)) return QString();

    if (QFileInfo(path).suffix().compare(QLatin1String("xml"), Qt::CaseInsensitive) == 0) {
        const QString picture = pictureFromTheme(path, preferred);
        return QFileInfo::exists(picture) ? picture : QString();
    }
    return path;
}

GnomeWallpaper::GnomeWallpaper(QObject *parent) : QObject(parent) {
    // gsettings says so on its standard output every time a key in the
    // schema changes, which is word enough without a GIO dependency. The
    // whole schema is watched: both picture keys matter, and so does the
    // theme file one of them may name being swapped for another.
    m_monitor = new QProcess(this);
    connect(m_monitor, &QProcess::readyReadStandardOutput, this, [this] {
        const QByteArray output = m_monitor->readAllStandardOutput();
        for (const QByteArray &line : output.split('\n')) {
            if (line.startsWith("picture-")) {
                emit changed();
                break;
            }
        }
    });
    m_monitor->start(QStringLiteral("gsettings"),
                     {QStringLiteral("monitor"), QLatin1String(kSchema)});
}
