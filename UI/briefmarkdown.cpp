#include "briefmarkdown.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace {

const char *kIds[] = {
    "title",
    "style",
    "audience",
    "logline",
    "lastLine",
    "place",
    "change",
    "cast",
    "hero",
    "refs",
    "drawing",
    "shots",
    "action",
    "result",
    "seconds",
    "size",
    "materials",
    "storyboard",
    "motion"
};

QString bodyFor(const char *id, const BriefData &data)
{
    const QString key = QString::fromLatin1(id);
    if (key == QLatin1String("title")) return data.title;
    if (key == QLatin1String("style")) return data.style;
    if (key == QLatin1String("audience")) return data.audience;
    if (key == QLatin1String("logline")) return data.logline;
    if (key == QLatin1String("lastLine")) return data.lastLine;
    if (key == QLatin1String("place")) return data.place;
    if (key == QLatin1String("change")) return data.change;
    if (key == QLatin1String("cast")) return data.cast;
    if (key == QLatin1String("hero")) return data.hero;
    if (key == QLatin1String("refs")) return data.refs;
    if (key == QLatin1String("drawing")) return data.drawing;
    if (key == QLatin1String("shots")) return data.shots;
    if (key == QLatin1String("action")) return data.action;
    if (key == QLatin1String("result")) return data.result;
    if (key == QLatin1String("seconds")) return data.seconds;
    if (key == QLatin1String("size")) return data.size;
    if (key == QLatin1String("materials")) return data.materials;
    if (key == QLatin1String("storyboard")) return data.storyboard;
    QString motion = QString::fromLatin1("t x y action");
    for (int i = 0; i < data.motion.size(); ++i) {
        const MotionKey &keyframe = data.motion.at(i);
        motion += QLatin1Char('\n');
        motion += keyframe.t;
        motion += QLatin1Char(' ');
        motion += keyframe.x;
        motion += QLatin1Char(' ');
        motion += keyframe.y;
        motion += QLatin1Char(' ');
        motion += keyframe.action;
    }
    return motion;
}

QString markerBlock(const char *id, const QString &body)
{
    QString block = QString::fromLatin1("<!-- brief:%1 -->\n").arg(QLatin1String(id));
    block += body;
    if (!body.isEmpty())
        block += QLatin1Char('\n');
    block += QString::fromLatin1("<!-- /brief:%1 -->\n").arg(QLatin1String(id));
    return block;
}

bool patch(QString *doc, const char *id, const QString &body)
{
    const QString open = QString::fromLatin1("<!-- brief:%1 -->").arg(QLatin1String(id));
    const QString close = QString::fromLatin1("<!-- /brief:%1 -->").arg(QLatin1String(id));
    const int openAt = doc->indexOf(open);
    const int closeAt = doc->indexOf(close);
    if (openAt >= 0 && closeAt > openAt) {
        int start = openAt + open.size();
        if (start < doc->size() && doc->at(start) == QLatin1Char('\n'))
            ++start;
        int end = closeAt;
        if (end > start && doc->at(end - 1) == QLatin1Char('\n'))
            --end;
        doc->replace(start, end - start, body);
        return true;
    }
    if (openAt >= 0 || closeAt >= 0)
        return false;
    if (!doc->isEmpty() && !doc->endsWith(QLatin1Char('\n')))
        *doc += QLatin1Char('\n');
    if (QLatin1String(id) == QLatin1String("motion") && !doc->contains(QLatin1String("## Motion")))
        *doc += QLatin1String("\n## Motion\n\n");
    // 老 brief.md 里没有这两个块，补的时候连标题一起补，插在 Motion 前面
    static const char *kHeaded[][2] = {
        { "drawing", "## Drawing" },
        { "shots", "## Shot order" },
    };
    for (int i = 0; i < int(sizeof(kHeaded) / sizeof(kHeaded[0])); ++i) {
        if (QLatin1String(id) != QLatin1String(kHeaded[i][0]))
            continue;
        const QString block = QString::fromLatin1("\n%1\n\n%2")
                                  .arg(QLatin1String(kHeaded[i][1]), markerBlock(id, body));
        const int motionAt = doc->indexOf(QLatin1String("\n## Motion"));
        if (motionAt >= 0) {
            doc->insert(motionAt, block);
        } else {
            *doc += block;
        }
        return true;
    }
    *doc += markerBlock(id, body);
    return true;
}

void writeTitleHeading(QString *doc, const QString &title)
{
    QString heading = title;
    heading.replace(QLatin1Char('\n'), QLatin1Char(' '));
    heading = heading.trimmed();
    const QString line = QString::fromLatin1("# ") + heading;
    if (doc->startsWith(QLatin1String("# "))) {
        const int nl = doc->indexOf(QLatin1Char('\n'));
        if (nl < 0)
            *doc = line;
        else
            doc->replace(0, nl, line);
        return;
    }
    *doc = line + QString::fromLatin1("\n\n") + *doc;
}

bool readDoc(const QString &path, QString *doc)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    *doc = QString::fromUtf8(file.readAll());
    doc->replace(QLatin1String("\r\n"), QLatin1String("\n"));
    doc->replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return true;
}

bool takeInterior(const QString &doc, const char *id, QString *body)
{
    const QString open = QString::fromLatin1("<!-- brief:%1 -->").arg(QLatin1String(id));
    const QString close = QString::fromLatin1("<!-- /brief:%1 -->").arg(QLatin1String(id));
    const int openAt = doc.indexOf(open);
    const int closeAt = doc.indexOf(close);
    if (openAt < 0 || closeAt < openAt) {
        body->clear();
        return true;
    }
    int start = openAt + open.size();
    if (start < doc.size() && doc.at(start) == QLatin1Char('\n'))
        ++start;
    int end = closeAt;
    if (end > start && doc.at(end - 1) == QLatin1Char('\n'))
        --end;
    *body = doc.mid(start, end - start);
    return !body->contains(QLatin1String("<!-- /brief:"));
}

void readMotion(const QString &body, QVector<MotionKey> *motion)
{
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;
        const QStringList parts = line.split(QLatin1Char(' '));
        if (parts.size() >= 2 && parts.at(0) == QLatin1String("t") && parts.at(1) == QLatin1String("x"))
            continue;
        if (parts.size() < 3)
            continue;
        MotionKey key;
        key.t = parts.at(0);
        key.x = parts.at(1);
        key.y = parts.at(2);
        for (int p = 3; p < parts.size(); ++p) {
            if (!key.action.isEmpty())
                key.action += QLatin1Char(' ');
            key.action += parts.at(p);
        }
        motion->append(key);
    }
}

}

bool BriefMarkdown::load(const QString &path, BriefData *data)
{
    if (!data)
        return false;
    QString doc;
    if (!readDoc(path, &doc))
        return false;
    BriefData loaded;
    const char *ids[] = {
        "title", "style", "audience", "logline", "lastLine", "place", "change",
        "cast", "hero", "refs", "drawing", "shots", "action", "result", "seconds",
        "size", "materials", "storyboard", "motion"
    };
    QString bodies[sizeof(ids) / sizeof(ids[0])];
    for (int i = 0; i < int(sizeof(ids) / sizeof(ids[0])); ++i) {
        if (!takeInterior(doc, ids[i], &bodies[i]))
            return false;
    }
    loaded.title = bodies[0];
    loaded.style = bodies[1];
    loaded.audience = bodies[2];
    loaded.logline = bodies[3];
    loaded.lastLine = bodies[4];
    loaded.place = bodies[5];
    loaded.change = bodies[6];
    loaded.cast = bodies[7];
    loaded.hero = bodies[8];
    loaded.refs = bodies[9];
    loaded.drawing = bodies[10];
    loaded.shots = bodies[11];
    loaded.action = bodies[12];
    loaded.result = bodies[13];
    loaded.seconds = bodies[14];
    loaded.size = bodies[15];
    loaded.materials = bodies[16];
    loaded.storyboard = bodies[17];
    readMotion(bodies[18], &loaded.motion);
    *data = loaded;
    return true;
}

bool BriefMarkdown::save(const QString &path, const BriefData &data)
{
    QString bodies[sizeof(kIds) / sizeof(kIds[0])];
    for (int i = 0; i < int(sizeof(kIds) / sizeof(kIds[0])); ++i) {
        bodies[i] = bodyFor(kIds[i], data);
        if (bodies[i].contains(QLatin1String("<!-- /brief:")))
            return false;
    }

    QString doc;
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly))
            return false;
        doc = QString::fromUtf8(existing.readAll());
        existing.close();
        doc.replace(QLatin1String("\r\n"), QLatin1String("\n"));
        doc.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    }

    for (int i = 0; i < int(sizeof(bodies) / sizeof(bodies[0])); ++i) {
        if (!patch(&doc, kIds[i], bodies[i]))
            return false;
    }
    writeTitleHeading(&doc, data.title);
    if (!doc.endsWith(QLatin1Char('\n')))
        doc += QLatin1Char('\n');

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    if (out.write(doc.toUtf8()) < 0)
        return false;
    return out.commit();
}

// applyFilm is gone on purpose. The old flow injected the brief's fields into
// director/DIRECTOR.md and director/TECHNIQUE.md, because the agent only ever read
// those two guides. Now the agent's first step is films/<name>/brief.md itself, so
// that injection only left one film's story printed inside the shared template for
// the next film to read, and it overwrote the guides on every click.
// Saving a brief writes one file: brief.md.
