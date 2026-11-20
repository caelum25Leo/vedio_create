#include "boardscan.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace {

bool readText(const QString &file, QString *out)
{
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly))
        return false;
    const QByteArray raw = handle.readAll();
    *out = QString::fromUtf8(raw);
    return true;
}

void addUnique(QStringList *list, const QString &value)
{
    if (!value.isEmpty() && !list->contains(value))
        list->append(value);
}

bool firstNumber(const QString &text, const QString &pattern, QString *out)
{
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return false;
    *out = match.captured(1);
    return true;
}

bool firstPair(const QString &text, const QString &pattern, QString *out)
{
    QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return false;
    const int w = match.captured(1).toInt();
    const int h = match.captured(2).toInt();
    if (w < 100 || h < 100 || w > 8192 || h > 8192)
        return false;
    *out = QString::number(w) + QLatin1Char('x') + QString::number(h);
    return true;
}

void collectRelative(const QString &source, const QRegularExpression &re, QStringList *out)
{
    QRegularExpressionMatchIterator it = re.globalMatch(source);
    while (it.hasNext())
        addUnique(out, it.next().captured(1));
}

// './earth.js'、'../js/earth.js' → 'earth.js'。分镜 JS 习惯写成 '兄弟模块.js'，
// 只剔 '../' 不剔 './' 会把文件名削掉一个字符，依赖会被误报成缺文件。
QString stripDots(const QString &spec)
{
    QString clean = spec;
    while (clean.startsWith(QLatin1String("../")))
        clean = clean.mid(3);
    while (clean.startsWith(QLatin1String("./")))
        clean = clean.mid(2);
    return clean;
}

// Resolve a referenced path against the storyboard folder; anything found there
// came along with the copy, anything else the agent has to be told about.
bool resolves(const QString &reference, const QString &boardRoot)
{
    const QString clean = stripDots(reference);
    if (clean.isEmpty())
        return false;
    if (QFileInfo(QDir(boardRoot).filePath(clean)).exists())
        return true;
    QDirIterator it(boardRoot, QStringList() << QFileInfo(clean).fileName(),
                    QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

// MARKS 的经纬跨度：跨度大的那一镜更远。认不出成对数字就返回 -1。
double marksSpan(const QString &source)
{
    QRegularExpression decl(QString::fromLatin1("MARKS\\s*=\\s*\\[([\\s\\S]*?)\\]\\s*;"));
    QRegularExpressionMatch dm = decl.match(source);
    if (!dm.hasMatch())
        return -1;
    QRegularExpression pair(QString::fromLatin1("\\[\\s*(-?\\d+(?:\\.\\d+)?)\\s*,\\s*(-?\\d+(?:\\.\\d+)?)\\s*\\]"));
    double minLat = 0, maxLat = 0, minLon = 0, maxLon = 0;
    int seen = 0;
    QRegularExpressionMatchIterator it = pair.globalMatch(dm.captured(1));
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const double a = m.captured(1).toDouble();
        const double b = m.captured(2).toDouble();
        if (!seen) {
            minLat = maxLat = a;
            minLon = maxLon = b;
        } else {
            minLat = qMin(minLat, a);
            maxLat = qMax(maxLat, a);
            minLon = qMin(minLon, b);
            maxLon = qMax(maxLon, b);
        }
        ++seen;
    }
    if (seen < 2)
        return -1;
    return std::sqrt((maxLat - minLat) * (maxLat - minLat) + (maxLon - minLon) * (maxLon - minLon));
}

// draw(ctx, stage, time) 的第三个参数是这一镜的时间；只有 (ctx, stage) 就是静态取景。
bool drawHasProgress(const QString &source)
{
    QRegularExpression re(QString::fromLatin1("\\bdraw\\s*\\(([^)]*)\\)"));
    QRegularExpressionMatchIterator it = re.globalMatch(source);
    while (it.hasNext()) {
        const QStringList params = it.next().captured(1)
            .split(QLatin1Char(','), QString::SkipEmptyParts);
        for (int i = 0; i < params.size(); ++i) {
            const QString p = params.at(i).trimmed().toLower();
            if (p == QLatin1String("u") || p == QLatin1String("t")
                || p == QLatin1String("time") || p == QLatin1String("progress")
                || p == QLatin1String("second") || p == QLatin1String("seconds")
                || p == QLatin1String("sec"))
                return true;
        }
    }
    return false;
}

ShotModule scanModule(const QString &absolute, const QString &boardRoot, bool *isModule)
{
    ShotModule shot;
    *isModule = false;
    QString source;
    if (!readText(absolute, &source))
        return shot;
    shot.file = QDir::fromNativeSeparators(QDir(boardRoot).relativeFilePath(absolute));

    const int at = source.indexOf(QLatin1String("export default"));
    if (at < 0)
        return shot;
    const QString head = source.mid(at, 400);
    QRegularExpression idRe(QString::fromLatin1("\\bid\\s*[:=]\\s*[\"']([A-Za-z][\\w-]{0,24})[\"']"));
    QRegularExpression nameRe(QString::fromLatin1("\\bname\\s*[:=]\\s*[\"']([^\"'\\n]{1,24})[\"']"));
    QRegularExpressionMatch im = idRe.match(head);
    if (im.hasMatch())
        shot.id = im.captured(1);
    QRegularExpressionMatch nm = nameRe.match(head);
    if (nm.hasMatch())
        shot.name = nm.captured(1);
    if (shot.id.isEmpty() || !source.contains(QRegularExpression(QString::fromLatin1("\\bdraw\\s*\\("))))
        return shot;
    *isModule = true;

    shot.progress = drawHasProgress(source);
    shot.framing = source.contains(QLatin1String("MARKS"))
        && source.contains(QRegularExpression(QString::fromLatin1("function\\s+frameFor|\\bframeFor\\s*=")));
    shot.projection = source.contains(QRegularExpression(QString::fromLatin1("\\btoView\\b")))
        && source.contains(QRegularExpression(QString::fromLatin1("\\bpaintProjected\\b")));
    shot.needsWx = source.contains(QRegularExpression(QString::fromLatin1("wx\\s*\\.\\s*createCanvas|GameGlobal")));
    shot.span = marksSpan(source);
    QRegularExpression latRe(QString::fromLatin1("ANCHOR_LAT\\s*=\\s*([0-9.]+)"));
    QRegularExpression lonRe(QString::fromLatin1("ANCHOR_LON\\s*=\\s*([0-9.]+)"));
    const QRegularExpressionMatch lam = latRe.match(source);
    const QRegularExpressionMatch lom = lonRe.match(source);
    if (lam.hasMatch() && lom.hasMatch())
        shot.anchor = lam.captured(1) + QLatin1String(", ") + lom.captured(1);
    QString cap;
    if (!firstNumber(source, QString::fromLatin1("maxSide\\s*=\\s*(\\d{3,4})"), &cap))
        firstNumber(source, QString::fromLatin1("Math\\s*\\.\\s*min\\s*\\(\\s*(\\d{3,4})\\s*,"), &cap);
    shot.spriteCap = cap.toInt();
    return shot;
}

// 推镜顺序：没有登记表（index.js 一类）时，取景跨度大的排前面；
// 自己不框 MARKS 但被别人 import 的那模块（投影底本）当第一镜。
void orderShots(BoardScan *scan)
{
    QVector<ShotModule> shots = scan->modules;
    if (shots.isEmpty())
        return;
    std::stable_sort(shots.begin(), shots.end(), [](const ShotModule &a, const ShotModule &b) {
        const double sa = a.span < 0 ? 1e9 : a.span;
        const double sb = b.span < 0 ? 1e9 : b.span;
        return sa > sb;
    });
    scan->shotIds.clear();
    for (int i = 0; i < shots.size(); ++i)
        scan->shotIds << shots.at(i).id;
    scan->modules = shots;
    scan->orderGuessed = true;
}

}

QStringList jsFilesUnder(const QString &dir)
{
    QStringList found;
    QDirIterator it(dir, QStringList() << QLatin1String("*.js") << QLatin1String("*.mjs"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        found << QFileInfo(it.next()).absoluteFilePath();
    found.sort();
    return found;
}

// 挑主 JS。一格一个模块的分镜表里没有哪个文件写着「从我开始」，
// 但被其余模块 import 的那个就是底本（投影、共用算法都在那里），
// 所以「被 import 最多」比文件名更靠谱；再不行才看名字。
QString guessEntry(const QStringList &absolute, const QString &boardRoot)
{
    QHash<QString, QSet<QString> > importers;
    QRegularExpression importRe(QString::fromLatin1("(?:import[^;\"']*from|require\\()\\s*[\"']([^\"']+)[\"']"));
    for (int k = 0; k < absolute.size(); ++k) {
        QString source;
        if (!readText(absolute.at(k), &source))
            continue;
        QRegularExpressionMatchIterator it = importRe.globalMatch(source);
        while (it.hasNext()) {
            const QString base = QFileInfo(stripDots(it.next().captured(1))).completeBaseName();
            if (base.isEmpty())
                continue;
            importers[base].insert(absolute.at(k));
        }
    }
    const QString root = QDir(boardRoot).absolutePath();
    int bestRefs = 0;
    QString best;
    for (int i = 0; i < absolute.size(); ++i) {
        if (QFileInfo(absolute.at(i)).absolutePath() != root)
            continue;
        const int refs = importers.value(QFileInfo(absolute.at(i)).completeBaseName()).size();
        if (refs > bestRefs) {
            bestRefs = refs;
            best = absolute.at(i);
        }
    }
    if (!best.isEmpty())
        return best;

    const char *hints[] = { "storyboard", "story", "shots", "main", "index", "film", "board" };
    for (int h = 0; h < int(sizeof(hints) / sizeof(hints[0])); ++h) {
        for (int i = 0; i < absolute.size(); ++i) {
            if (QFileInfo(absolute.at(i)).absolutePath() != root)
                continue;
            if (QFileInfo(absolute.at(i)).completeBaseName().compare(QLatin1String(hints[h]), Qt::CaseInsensitive) == 0)
                return absolute.at(i);
        }
    }
    for (int i = 0; i < absolute.size(); ++i) {
        if (QFileInfo(absolute.at(i)).absolutePath() == root)
            return absolute.at(i);
    }
    return absolute.isEmpty() ? QString() : absolute.at(0);
}

BoardScan scanBoard(const QString &file, const QString &boardRoot)
{
    BoardScan scan;
    scan.truncated = false;
    QString source;
    if (!readText(file, &source))
        return scan;
    if (source.size() > 1200000) {
        source = source.left(1200000);
        scan.truncated = true;
    }

    const QStringList siblings = jsFilesUnder(boardRoot);
    for (int i = 0; i < siblings.size(); ++i) {
        bool isModule = false;
        const ShotModule shot = scanModule(siblings.at(i), boardRoot, &isModule);
        if (isModule)
            scan.modules.append(shot);
        else if (siblings.at(i) != file)
            scan.dataFiles << QFileInfo(siblings.at(i)).fileName();
    }

    int framed = 0;
    int moving = 0;
    for (int i = 0; i < scan.modules.size(); ++i) {
        if (scan.modules.at(i).framing)
            ++framed;
        if (scan.modules.at(i).progress)
            ++moving;
    }

    const bool hasDraw = source.contains(QRegularExpression(QString::fromLatin1("\\b(?:draw|paint|renderShot|shot)\\w*\\s*\\([^)]*\\bu\\b")));
    const bool hasStory = source.contains(QRegularExpression(QString::fromLatin1("window\\s*\\.\\s*STORY|\\bshots\\b\\s*[:=]")));
    const bool hasWholeTime = source.contains(QRegularExpression(QString::fromLatin1("window\\s*\\.\\s*renderAt|\\b(?:render|draw|paint)\\s*\\([^)]*\\bt\\b")));
    const bool hasStills = source.contains(QRegularExpression(QString::fromLatin1("\\.(?:png|jpg|jpeg|webp)[\"']")));

    if (scan.modules.size() >= 2 && framed >= 1 && moving == 0) {
        // 一格一个模块，模块只管把自己那一格画满，推进是模块之间的事
        scan.mode = QString::fromLatin1("D");
        scan.evidence = QString::fromUtf8("%1 个模块各画一格静态取景，draw() 里没有进度参数")
                            .arg(scan.modules.size());
        orderShots(&scan);
        bool shared = true;
        for (int i = 0; i < scan.modules.size(); ++i) {
            if (!scan.modules.at(i).projection && !scan.modules.at(i).framing)
                shared = false;
        }
        if (shared) {
            scan.notes << QString::fromUtf8("推镜怎么接：这些框在同一套投影里，一镜的取景就是 radius、originX、originY 三个数（各模块 frameFor 算出来的）。"
                                            "镜与镜之间对 radius 取对数插值，再由锚点反解 origin，就是推镜；插值式在两端还原成两镜自己的取景，接得上不会跳。");
        }
        QStringList caps;
        QStringList wxFiles;
        QStringList anchors;
        for (int i = 0; i < scan.modules.size(); ++i) {
            const ShotModule &shot = scan.modules.at(i);
            if (shot.spriteCap > 0)
                addUnique(&caps, QString::number(shot.spriteCap));
            if (shot.needsWx)
                wxFiles << shot.file;
            if (!shot.anchor.isEmpty())
                addUnique(&anchors, QString::fromUtf8("%1（%2）").arg(shot.anchor).arg(shot.file));
        }
        if (!anchors.isEmpty()) {
            scan.notes << QString::fromUtf8("取景锚点：%1 —— 全程盯住同一个地方，推镜才不像跳镜。").arg(anchors.join(QString::fromUtf8("、")));
        }
        if (!wxFiles.isEmpty()) {
            scan.notes << QString::fromUtf8("这些模块跑在小游戏环境里，node 直接 import 会报 wx is not defined，出样帧前要先打桩（wx.createCanvas → getContext('2d') → createImageData / putImageData）：%1")
                            .arg(wxFiles.join(QString::fromUtf8("、")));
        }
        if (!caps.isEmpty()) {
            std::sort(caps.begin(), caps.end());
            scan.notes << QString::fromUtf8("模块内部位图边长上限 %1 px。出片画幅比它大就别放大那张精灵，"
                                            "照搬它的 frameFor 公式按成片画幅复算取景，再逐像素画（paintProjected 一类的入口吃 width/height）。")
                            .arg(caps.join(QString::fromLatin1(" / ")));
        }
    } else if (scan.modules.size() >= 2 && moving >= 1) {
        scan.mode = QString::fromLatin1("A");
        scan.evidence = QString::fromUtf8("%1 个模块，draw() 里带进度 / 时间参数，逐镜自己会动").arg(scan.modules.size());
        orderShots(&scan);
    } else if (hasDraw || hasStory) {
        scan.mode = QString::fromLatin1("A");
        scan.evidence = QString::fromUtf8("逐镜函数带归一化进度 u，或导出了 shots 表");
    } else if (hasWholeTime) {
        scan.mode = QString::fromLatin1("B");
        scan.evidence = QString::fromUtf8("只有一个按整片时间 t 画的入口");
    } else if (hasStills) {
        scan.mode = QString::fromLatin1("C");
        scan.evidence = QString::fromUtf8("只列出成品图，没有时间入口");
    } else {
        scan.mode = QString::fromLatin1("?");
        scan.evidence = QString::fromUtf8("没认出画分镜的入口，需要人工看一遍");
    }

    if (!firstPair(source, QString::fromLatin1("SIZE\\s*=\\s*\\[\\s*(\\d+)[,\\s]+(\\d+)"), &scan.sizeText))
        firstPair(source, QString::fromLatin1("canvas\\s*\\.\\s*width\\s*=\\s*(\\d+)[^0-9]{0,40}canvas\\s*\\.\\s*height\\s*=\\s*(\\d+)"), &scan.sizeText);
    if (scan.sizeText.isEmpty())
        firstPair(source, QString::fromLatin1("\\bw(?:idth)?\\s*[:=]\\s*(\\d+)\\s*,\\s*h(?:eight)?\\s*[:=]\\s*(\\d+)"), &scan.sizeText);
    if (scan.sizeText.isEmpty())
        firstPair(source, QString::fromUtf8("(\\d{3,4})\\s*[x×]\\s*(\\d{3,4})"), &scan.sizeText);
    if (scan.sizeText.isEmpty() && scan.mode == QLatin1String("D"))
        scan.notes << QString::fromUtf8("画幅：这些模块按屏幕尺寸画，JS 里没有成片画幅 —— 在上面的「画幅」栏填。");

    firstNumber(source, QString::fromLatin1("FPS\\s*=\\s*(\\d{1,3})"), &scan.fps);
    if (scan.fps.isEmpty())
        firstNumber(source, QString::fromLatin1("\\bfps\\s*[:=]\\s*(\\d{1,3})"), &scan.fps);
    firstNumber(source, QString::fromLatin1("(?:DURATION|SECS|TOTAL)\\s*=\\s*([0-9]{1,4}(?:\\.[0-9]{1,3})?)"), &scan.duration);
    if (scan.duration.isEmpty())
        firstNumber(source, QString::fromLatin1("\\bdur(?:ation)?\\s*[:=]\\s*([0-9]{1,4}(?:\\.[0-9]{1,3})?)"), &scan.duration);

    if (scan.shotIds.isEmpty()) {
        QRegularExpression idRe(QString::fromLatin1("\\b(?:id|shot|name)\\s*[:=]\\s*[\"']([A-Za-z][\\w-]{1,24})[\"']"));
        collectRelative(source, idRe, &scan.shotIds);
    }

    QRegularExpression importRe(QString::fromLatin1("(?:import[^;\"']*from|require\\()\\s*[\"']([^\"']+)[\"']"));
    QStringList specs;
    collectRelative(source, importRe, &specs);
    for (int i = 0; i < specs.size(); ++i) {
        const QString one = specs.at(i);
        if (one.startsWith(QLatin1Char('.')))
            scan.imports << one;
        else if (!one.startsWith(QLatin1Char('/')))
            addUnique(&scan.packages, one);
    }
    // 兄弟模块也要各自把 import 交代一遍，不然依赖只报了主 JS 那一层
    for (int i = 0; i < scan.modules.size(); ++i) {
        QString oneSource;
        if (!readText(QDir(boardRoot).filePath(scan.modules.at(i).file), &oneSource))
            continue;
        QStringList more;
        collectRelative(oneSource, importRe, &more);
        for (int k = 0; k < more.size(); ++k) {
            const QString one = more.at(k);
            if (one.startsWith(QLatin1Char('.'))) {
                if (!scan.imports.contains(one))
                    scan.imports << one;
            } else if (!one.startsWith(QLatin1Char('/'))) {
                addUnique(&scan.packages, one);
            }
        }
    }
    QRegularExpression assetRe(QString::fromLatin1("[\"']([^\"'\\n]{1,160}?\\.(?:png|jpg|jpeg|webp|gif|svg|mp3|wav|ogg|json|woff2?|ttf|otf))[\"']"));
    collectRelative(source, assetRe, &scan.assets);

    for (int i = 0; i < scan.imports.size(); ++i) {
        const QString one = scan.imports.at(i);
        if (resolves(one, boardRoot) || resolves(one + QLatin1String(".js"), boardRoot)
            || resolves(one + QLatin1String(".mjs"), boardRoot))
            continue;
        scan.missing << one;
    }
    for (int i = 0; i < scan.assets.size(); ++i) {
        const QString one = scan.assets.at(i);
        if (one.startsWith(QLatin1String("http")) || one.startsWith(QLatin1String("data:")))
            continue;
        if (resolves(one, boardRoot))
            continue;
        scan.missing << one;
    }
    return scan;
}

QString describe(const BoardScan &scan, const QString &entryRel, qint64 bytes)
{
    QString text;
    QTextStream out(&text);
    out << QString::fromUtf8("主 JS：%1（%2 字节）\n").arg(entryRel).arg(bytes);
    out << QString::fromUtf8("分镜模式（推定）：%1 —— %2\n").arg(scan.mode).arg(scan.evidence);

    if (!scan.modules.isEmpty()) {
        out << QString::fromUtf8("镜头模块 %1 个%2：\n").arg(scan.modules.size())
                   .arg(scan.orderGuessed ? QString::fromUtf8("（下面这个顺序是按取景范围从大到小猜的，要在「推镜顺序」栏确认）") : QString());
        for (int i = 0; i < scan.modules.size(); ++i) {
            const ShotModule &shot = scan.modules.at(i);
            QStringList tag;
            if (shot.framing)
                tag << QString::fromUtf8("自算取景");
            if (shot.projection)
                tag << QString::fromUtf8("共用投影");
            if (shot.progress)
                tag << QString::fromUtf8("自带进度参数");
            if (shot.span > 0)
                tag << QString::fromUtf8("跨度 %1°").arg(shot.span, 0, 'f', 2);
            out << QString::fromUtf8("  %1. %2").arg(i + 1).arg(shot.id);
            if (!shot.name.isEmpty())
                out << QString::fromUtf8(" %1").arg(shot.name);
            out << QString::fromUtf8("  —— %1").arg(shot.file);
            if (!tag.isEmpty())
                out << QString::fromUtf8("  [%1]").arg(tag.join(QString::fromLatin1(" ")));
            out << QLatin1Char('\n');
        }
    }
    if (!scan.dataFiles.isEmpty())
        out << QString::fromUtf8("只放数据不画画的 JS：%1\n").arg(scan.dataFiles.join(QString::fromUtf8("、")));

    out << QString::fromUtf8("画幅：%1    帧率：%2    时长：%3\n")
               .arg(scan.sizeText.isEmpty()
                        ? (scan.mode == QLatin1String("D")
                               ? QString::fromUtf8("JS 里没有")
                               : QString::fromUtf8("未识别，取 JS 画布"))
                        : scan.sizeText,
                    scan.fps.isEmpty() ? QString::fromUtf8("未识别，按 24") : scan.fps + QLatin1String(" fps"),
                    scan.duration.isEmpty() ? QString::fromUtf8("未识别") : scan.duration + QLatin1String(" s"));
    if (scan.shotIds.isEmpty()) {
        out << QString::fromUtf8("镜头：没认出 id 表\n");
    } else if (scan.modules.isEmpty()) {
        QStringList shown = scan.shotIds;
        if (shown.size() > 24)
            shown = shown.mid(0, 24) << QString::fromUtf8("…");
        out << QString::fromUtf8("镜头 %1 个：%2\n").arg(scan.shotIds.size()).arg(shown.join(QString::fromLatin1(" / ")));
    }
    for (int i = 0; i < scan.notes.size(); ++i)
        out << scan.notes.at(i) << QLatin1Char('\n');
    out << QString::fromUtf8("import 的相对模块 %1 个    要装的包 %2 个    引用的素材 %3 个\n")
               .arg(scan.imports.size()).arg(scan.packages.size()).arg(scan.assets.size());
    if (!scan.packages.isEmpty())
        out << QString::fromUtf8("这些得在片子目录里 npm install：") << scan.packages.join(QLatin1String(", ")) << QLatin1Char('\n');
    if (scan.missing.isEmpty()) {
        out << QString::fromUtf8("相对引用全部能在 storyboard/ 里找到。\n");
    } else {
        out << QString::fromUtf8("缺这些（把文件补进 storyboard/，或告诉用户缺什么）：\n");
        for (int i = 0; i < scan.missing.size(); ++i)
            out << QLatin1String("  - ") << scan.missing.at(i) << QLatin1Char('\n');
    }
    if (scan.truncated)
        out << QString::fromUtf8("注意：文件很大，只扫了前 1.2 MB。\n");
    out << QString::fromUtf8("（这段是「读取 JS」生成的，重读会覆盖；要补的话写在下面。）\n");
    return text;
}
