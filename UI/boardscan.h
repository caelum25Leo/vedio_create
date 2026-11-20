#ifndef BOARDSCAN_H
#define BOARDSCAN_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

// 一格分镜模块：JS 里 `export default { id, name, draw }` 的那个东西。
struct ShotModule {
    QString id;
    QString name;
    QString file;    // 相对 storyboard/ 的路径
    QString anchor;  // 模块自己写的取景中心（ANCHOR_LAT / ANCHOR_LON），没有就是空
    bool framing;    // 有 MARKS + frameFor() → 这一镜的取景能算成三个数
    bool projection; // 用了 toView / paintProjected → 和别的镜共用同一套投影
    bool progress;   // draw(…, u / t / time) → 这一镜自己会动
    bool needsWx;    // 用了 wx.createCanvas / GameGlobal，node 里要先打桩
    int spriteCap;   // 模块内部位图的边长上限（px），0 = 没写
    double span;     // MARKS 的经纬跨度，用来排推镜顺序，-1 = 没有 MARKS
    ShotModule()
        : framing(false), projection(false), progress(false), needsWx(false), spriteCap(0), span(-1)
    {
    }
};

struct BoardScan {
    QString mode;
    QString evidence;
    QString sizeText;
    QString fps;
    QString duration;
    QStringList shotIds;   // 镜头 id，D 模式下按取景范围从大到小排
    QVector<ShotModule> modules;
    QStringList dataFiles; // 只有数据、不画画的 JS（地名掩模一类）
    QStringList imports;
    QStringList packages;
    QStringList assets;
    QStringList notes;     // 推镜怎么接、环境要什么，写给做片的 AI 看
    QStringList missing;
    bool orderGuessed;
    bool truncated;
    BoardScan() : orderGuessed(false), truncated(false)
    {
    }
};

// 一个目录下的全部 JS / MJS，绝对路径，按名字排序
QStringList jsFilesUnder(const QString &dir);
// 在这些文件里挑一个当主 JS
QString guessEntry(const QStringList &absolute, const QString &boardRoot);
// 扫主 JS，顺带把同目录的兄弟模块一起认一遍
BoardScan scanBoard(const QString &file, const QString &boardRoot);
// 生成写进 brief:materials 的那段报告
QString describe(const BoardScan &scan, const QString &entryRel, qint64 bytes);

#endif
