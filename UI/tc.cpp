#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include "boardscan.h"
#include "briefmarkdown.h"
#include "briefwindow.h"

// 不带参数就是平时的输入界面。
//   testCode --scan <storyboard 目录 或 里面某个 JS>   打「读取 JS」的报告
//   testCode --brief <brief.md>                    打这份 brief 读回来的关键字段
// 给做片的 AI 用：不开窗口也知道自己写的文件能不能读进去。
// 控制台是 GBK 的话先 chcp 65001，不然中文会花。
namespace {

int runScan(const QString &target)
{
    QTextStream out(stdout);
    out.setCodec("UTF-8");
    const QFileInfo info(target);
    if (!info.exists()) {
        out << QString::fromUtf8("找不到：%1\n").arg(target);
        return 2;
    }
    const QString board = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QString entry = info.isFile() ? info.absoluteFilePath() : QString();
    if (entry.isEmpty()) {
        const QStringList files = jsFilesUnder(board);
        if (files.isEmpty()) {
            out << QString::fromUtf8("这个目录里没有 JS：%1\n").arg(board);
            return 2;
        }
        entry = guessEntry(files, board);
    }
    const BoardScan scan = scanBoard(entry, board);
    out << describe(scan, QDir::fromNativeSeparators(entry), QFileInfo(entry).size());
    out << QString::fromUtf8("推镜顺序：%1\n").arg(scan.shotIds.join(QLatin1Char(' ')));
    return 0;
}

}

// 把 brief.md 读回来打一遍关键字段，确认人手改的文件能被读进去。
int runBrief(const QString &path)
{
    QTextStream out(stdout);
    out.setCodec("UTF-8");
    BriefData data;
    if (!BriefMarkdown::load(path, &data)) {
        out << QString::fromUtf8("读不出来：%1\n").arg(path);
        return 2;
    }
    out << QString::fromUtf8("片名：%1\n").arg(data.title);
    out << QString::fromUtf8("主 JS：%1\n").arg(data.drawing);
    out << QString::fromUtf8("推镜顺序：%1\n").arg(data.shots);
    out << QString::fromUtf8("画幅：%1    片长：%2 秒    先看分镜：%3\n")
               .arg(data.size, data.seconds, data.storyboard);
    out << QString::fromUtf8("关键帧：%1 行    素材报告：%2 字\n").arg(data.motion.size()).arg(data.materials.length());
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) != QLatin1String("--scan") && args.at(i) != QLatin1String("-s"))
            continue;
        if (i + 1 >= args.size()) {
            QTextStream(stdout) << QString::fromUtf8("用法：testCode --scan <storyboard 目录或里面的 JS>\n");
            return 2;
        }
        return runScan(args.at(i + 1));
    }
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) != QLatin1String("--brief") && args.at(i) != QLatin1String("-b"))
            continue;
        if (i + 1 >= args.size()) {
            QTextStream(stdout) << QString::fromUtf8("用法：testCode --brief <brief.md>\n");
            return 2;
        }
        return runBrief(args.at(i + 1));
    }
    BriefWindow window;
    window.resize(720, 800);
    window.show();
    return app.exec();
}
