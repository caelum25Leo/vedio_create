#include "previewdialog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace {

const char *kBoardFolder = "storyboard";
const char *kStillFolder = "out/preview";
const char *kScript = "tools/preview/build-preview.mjs";

QString readText(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    // 打包器写的是 UTF-8；用 fromLocal8Bit 在中文 Windows 上会变乱码
    return QString::fromUtf8(f.readAll()).trimmed();
}

QString tailOf(const QByteArray &out, int lines)
{
    const QStringList all = QString::fromUtf8(out).split(QLatin1Char('\n'));
    QStringList kept;
    for (int i = 0; i < all.size(); ++i) {
        if (!all.at(i).trimmed().isEmpty())
            kept << all.at(i).trimmed();
    }
    QString tail;
    for (int i = qMax(0, kept.size() - lines); i < kept.size(); ++i)
        tail += (tail.isEmpty() ? QString() : QString::fromUtf8("\n")) + kept.at(i);
    return tail;
}

}

PreviewDialog::PreviewDialog(const QString &repoRoot, const QString &filmDir, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QString::fromUtf8("预览 storyboard/ —— 一个 JS 一个按钮"));
    resize(1000, 740);

    m_repoRoot = repoRoot;
    m_filmDir = filmDir;
    m_proc = 0;

    QLabel *listTitle = new QLabel(QString::fromUtf8("JS 文件"));
    m_buttonHost = new QWidget;
    m_buttonBox = new QVBoxLayout(m_buttonHost);
    m_buttonBox->setContentsMargins(0, 0, 6, 0);
    m_buttonBox->setSpacing(3);
    QScrollArea *listScroll = new QScrollArea;
    listScroll->setWidgetResizable(true);
    listScroll->setWidget(m_buttonHost);
    listScroll->setFrameShape(QFrame::NoFrame);
    listScroll->setMinimumWidth(196);
    listScroll->setMaximumWidth(250);
    QVBoxLayout *left = new QVBoxLayout;
    left->addWidget(listTitle);
    left->addWidget(listScroll, 1);

    m_image = new QLabel;
    m_image->setAlignment(Qt::AlignCenter);
    m_image->setMinimumSize(420, 480);
    m_image->setText(QString::fromUtf8("左边选一个 JS"));

    m_note = new QLabel;
    m_note->setWordWrap(true);
    m_note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_note->setMinimumHeight(56);
    m_note->setAlignment(Qt::AlignLeft | Qt::AlignTop);

    QVBoxLayout *right = new QVBoxLayout;
    right->addWidget(m_image, 1);
    right->addWidget(m_note);

    QPushButton *refresh = new QPushButton(QString::fromUtf8("刷新"));
    QPushButton *close = new QPushButton(QString::fromUtf8("关闭"));
    refresh->setToolTip(QString::fromUtf8("重扫 storyboard/（新拷进来的 JS 会多一个按钮），并重渲全部静帧：\n"
                                          "node tools/preview/build-preview.mjs <片子目录> --stills"));
    close->setToolTip(QString::fromUtf8("关掉这个窗口，主窗口不动"));

    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addWidget(refresh);
    bottom->addStretch();
    bottom->addWidget(close);

    QHBoxLayout *body = new QHBoxLayout;
    body->addLayout(left);
    body->addLayout(right, 1);

    QVBoxLayout *root = new QVBoxLayout;
    root->addLayout(body, 1);
    root->addLayout(bottom);
    setLayout(root);

    connect(refresh, SIGNAL(clicked()), this, SLOT(onRefresh()));
    connect(close, SIGNAL(clicked()), this, SLOT(close()));

    buildButtons();
    const QStringList files = boardFiles();
    if (!files.isEmpty()) {
        showStill(files.first());
    } else if (QDir(QDir(m_filmDir).filePath(QLatin1String(kBoardFolder))).entryList(
                   QStringList() << QLatin1String("*.js") << QLatin1String("*.mjs"), QDir::Files).isEmpty()) {
        // 连源 JS 都没有 —— 提示用户从主窗口拷进来
        m_note->setText(QString::fromUtf8("%1/ 里还没有 JS —— 先用主窗口的「选择 JS 文件…」或「选择整个目录…」拷进来")
                            .arg(QString::fromUtf8(kBoardFolder)));
    } else {
        // 有源 JS 但 out/preview/ 还是空：首次开预览窗口，自动跑一次全量渲。
        // 渲完时 onStillsFinished 拿到 __rescan__ 会重扫 out/preview/*.png 列按钮。
        m_note->setText(QString::fromUtf8("%1/ 有 JS，但 out/preview/ 还是空。\n自动跑一次全量渲图：node tools/preview/build-preview.mjs %2 --stills（几秒）")
                            .arg(QString::fromUtf8(kBoardFolder)).arg(QDir::toNativeSeparators(m_filmDir)));
        runStills(QString::fromUtf8("__rescan__"));   // 全量：onlyName 留空
    }
}

PreviewDialog::~PreviewDialog()
{
    // 关窗口时还渲着：杀掉，不丢一个在后台跑完还会抱着 msedge 的 node
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(2000);
    }
}

QStringList PreviewDialog::boardFiles() const
{
    // 按钮列表从 out/preview/*.png 拿。“哪个 JS 能被点”这个问题交给打包器去答：
    // 能画出 png 的 JS 会被列成按钮；只有 .txt、没 .png 的「数据型依赖」（earth-land.js
    // 那种 export const MASK、没 draw()）不会出现在这里，不干扰用户判断。
    QDir dir(QDir(m_filmDir).filePath(QLatin1String(kStillFolder)));
    const QStringList names = dir.entryList(QStringList() << QLatin1String("*.png"),
                                            QDir::Files, QDir::Name);
    QStringList full;
    for (int i = 0; i < names.size(); ++i)
        full << dir.absoluteFilePath(names.at(i));
    return full;
}

QString PreviewDialog::shortName(const QString &file) const
{
    // file 现在是 out/preview/<name>.png 的绝对路径。completeBaseName 去掉 .png。
    // 这个短名同时是 storyboard/<name>.js 的名字，打包器 --only 认这个。
    return QFileInfo(file).completeBaseName();
}

QString PreviewDialog::stillPath(const QString &file) const
{
    // file 本身就是 png 的绝对路径，原样返。
    return file;
}

QString PreviewDialog::notePath(const QString &file) const
{
    // 同目录同名 .txt：打包器在渲不出图时只留 txt。
    return QString(file).replace(QString::fromUtf8(".png"), QString::fromUtf8(".txt"));
}

QString PreviewDialog::readNote(const QString &file) const
{
    return readText(notePath(file));
}

void PreviewDialog::buildButtons()
{
    while (QLayoutItem *item = m_buttonBox->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    const QStringList files = boardFiles();
    for (int i = 0; i < files.size(); ++i) {
        // 按钮列表从 out/preview/*.png 拿，但按钮名字还是 <name>.js，跟用户原始需求一致；
        // 只出现「有 png」的那些 —— 数据型 JS（没 draw()）不会生成 png、也不会出现。
        const QString jsName = shortName(files.at(i)) + QLatin1String(".js");
        QPushButton *b = new QPushButton(jsName, m_buttonHost);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setProperty("jsFile", files.at(i));   // 内部仍然存 png 绝对路径，供 showStill 读图
        b->setToolTip(QString::fromUtf8("%1\n静帧：%2").arg(jsName).arg(files.at(i)));
        connect(b, SIGNAL(clicked()), this, SLOT(onPickShot()));
        m_buttonBox->addWidget(b);
    }
    m_buttonBox->addStretch();
}

void PreviewDialog::onPickShot()
{
    QPushButton *b = qobject_cast<QPushButton *>(sender());
    if (b)
        showStill(b->property("jsFile").toString());
}

void PreviewDialog::showStill(const QString &file, bool buildIfMissing)
{
    if (file.isEmpty())
        return;
    m_current = file;
    const QList<QPushButton *> bs = m_buttonHost->findChildren<QPushButton *>();
    for (int i = 0; i < bs.size(); ++i)
        bs.at(i)->setChecked(bs.at(i)->property("jsFile").toString() == file);

    const QString png = stillPath(file);
    if (QFileInfo(png).isFile()) {
        m_pending.clear();
        if (m_pix.load(png)) {
            rescale();
            const QString note = readNote(file);
            m_note->setText(QString::fromUtf8("%1　·　%2").arg(QFileInfo(file).fileName(),
                                 note.isEmpty() ? QString::fromUtf8("旁边没有 .txt，尺寸和像素统计没写上") : note));
        } else {
            m_pix = QPixmap();
            m_image->setText(QString::fromUtf8("图读不出来"));
            m_note->setText(QString::fromUtf8("%1 打不开（可能只写了一半），点「刷新」重渲").arg(QDir::toNativeSeparators(png)));
        }
        return;
    }

    m_pix = QPixmap();
    m_image->clear();
    if (!buildIfMissing) {
        // 这一支是刚跑完 node 还是没图：读 .txt 看打包器给的原因。
        // 不能顺手起 node —— 那就死循环了。
        if (QFileInfo(notePath(file)).isFile()) {
            m_image->setText(QString::fromUtf8("这个 JS 画不出静帧"));
            m_note->setText(QString::fromUtf8("%1\n（out/preview/%2.txt）")
                                .arg(readNote(file), shortName(file)));
            return;
        }
        m_image->setText(QString::fromUtf8("渲完了还是没有 %1").arg(shortName(file)));
        // kStillFolder 是个变量，不能拿字符串邻接拼（`kStillFolder "/x"` 只有字面量之间才成立）
        const QString status = readText(QDir(m_filmDir).filePath(
            QString::fromLatin1(kStillFolder) + QLatin1String("/_status.txt")));
        m_note->setText(status.isEmpty()
            ? QString::fromUtf8("打包器没写到这一格。命令行看全貌：node tools/preview/build-preview.mjs %1 --stills")
                  .arg(QDir::toNativeSeparators(m_filmDir))
            : QString::fromUtf8("打包器说：%1").arg(status));
        return;
    }
    // 走到这里：PNG 不在 + 允许补渲。不论之前是否有 .txt，一律先清掉旧的 png/txt，
    // 避免 --only 跑不动时读到上一版过期说明误判。删后马上走 node。
    QFile::remove(png);
    QFile::remove(notePath(file));
    m_image->setText(QString::fromUtf8("还没有 %1 的静帧，正在渲…").arg(shortName(file)));
    m_note->setText(QString::fromUtf8("渲一张图要在无头浏览器里跑真正的模块，几秒；之后要重看就点「刷新」"));
    // 按需出图：只让打包器渲当前这一格（--only=<shortName>），不动别的 JS。
    // 首次点按钮时这一格还不存在，走这里；之后 PNG 落盘了，上面 isFile() 那支就直接读图。
    runStills(file, shortName(file));
}

void PreviewDialog::rescale()
{
    if (m_pix.isNull())
        return;
    m_image->setPixmap(m_pix.scaled(m_image->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void PreviewDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    rescale();
}

QString PreviewDialog::findScript() const
{
    const QString script = QDir(m_repoRoot).filePath(QLatin1String(kScript));
    return QFileInfo(script).isFile() ? script : QString();
}

QString PreviewDialog::findNode() const
{
    const QString onPath = QStandardPaths::findExecutable(QLatin1String("node"));
    if (!onPath.isEmpty())
        return onPath;
    // PATH 里没有就试几个默认安装位置（开发机常见 D 盘）
    QStringList cand;
    cand << QString::fromUtf8("C:/Program Files/nodejs/node.exe")
         << QString::fromUtf8("D:/Program Files/nodejs/node.exe");
    for (int i = 0; i < cand.size(); ++i) {
        if (QFileInfo(cand.at(i)).isFile())
            return cand.at(i);
    }
    return QString();
}

void PreviewDialog::runStills(const QString &thenShow, const QString &onlyName)
{
    m_pending = thenShow;
    if (m_proc && m_proc->state() != QProcess::NotRunning)
        return;   // 这一轮还在渲，渲完会显示 m_pending
    const QString script = findScript();
    const QString node = findNode();
    if (script.isEmpty() || node.isEmpty()) {
        m_image->setText(QString::fromUtf8("渲不了图"));
        m_note->setText(script.isEmpty()
            ? QString::fromUtf8("没找到 %1。命令行也可以跑：node tools/preview/build-preview.mjs %2 --stills")
                  .arg(QLatin1String(kScript)).arg(QDir::toNativeSeparators(m_filmDir))
            : QString::fromUtf8("工具箱里没找到 node。命令行也可以跑：node tools/preview/build-preview.mjs %1 --stills")
                  .arg(QDir::toNativeSeparators(m_filmDir)));
        return;
    }
    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, SIGNAL(finished(int, QProcess::ExitStatus)),
                this, SLOT(onStillsFinished(int, QProcess::ExitStatus)));
    }
    m_proc->setWorkingDirectory(m_repoRoot);
    QStringList args;
    args << QDir::toNativeSeparators(script) << QDir::toNativeSeparators(m_filmDir) << QLatin1String("--stills");
    // onlyName 非空：只渲一格（点按钮按需出图）；空：全量渲（点「刷新」）。
    if (!onlyName.isEmpty())
        args << QString::fromUtf8("--only=%1").arg(onlyName);
    m_proc->start(node, args);
}

void PreviewDialog::onRefresh()
{
    // 「刷新」跑全量 node：扫 storyboard/ 下所有 .js、该出 png 的出 png。
    // 跑完 onStillsFinished 看到 m_pending = __rescan__，会重扫 out/preview/*.png 列按钮 + 显示第一格。
    m_note->setText(QString::fromUtf8("在重渲静帧…（node + 无头 msedge，几秒）"));
    runStills(QString::fromUtf8("__rescan__"));   // 全量渲：onlyName 留空
}

void PreviewDialog::onStillsFinished(int exitCode, QProcess::ExitStatus status)
{
    const QByteArray out = m_proc ? m_proc->readAll() : QByteArray();
    const QString show = m_pending;
    m_pending.clear();
    // 非零退出也可能已经画出了几格（比如某一格 draw() 抛了），所以先看图再报失败，
    // 不能一失败就什么都不给 —— 那正好把最该看的那一格藏起来。
    if (show == QLatin1String("__rescan__")) {
        // 全量渲图跑完（首次开窗 / 点「刷新」）：扫 out/preview/*.png 重建按钮列表。
        buildButtons();
        const QStringList files = boardFiles();
        if (!files.isEmpty())
            showStill(files.first(), false);   // 已经渲完一次，不要再触发 --only
    } else if (!show.isEmpty()) {
        showStill(show, false);
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        const QString tail = tailOf(out, 5);
        m_note->setText(QString::fromUtf8("渲图退出码 %1。%2").arg(exitCode).arg(
            tail.isEmpty()
                ? QString::fromUtf8("没输出，命令行看全貌：node tools/preview/build-preview.mjs %1 --stills")
                      .arg(QDir::toNativeSeparators(m_filmDir))
                : QString::fromUtf8("打包器最后几行：\n%1").arg(tail)));
    }
}
