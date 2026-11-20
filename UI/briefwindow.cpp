#include "briefwindow.h"
#include "boardscan.h"
#include "briefmarkdown.h"
#include "previewdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileInfoList>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QFormLayout>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStatusBar>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

const char *kBoardFolder = "storyboard";

QGroupBox *group(const QString &title, QLayout *layout)
{
    QGroupBox *box = new QGroupBox(title);
    box->setLayout(layout);
    return box;
}

bool isSkippedDir(const QString &name)
{
    return name == QLatin1String("node_modules")
        || name == QLatin1String("out")
        || name == QLatin1String("build")
        || name == QLatin1String("dist")
        || name == QLatin1String("__pycache__");
}

QString nativeRel(const QString &root, const QString &path)
{
    return QDir::fromNativeSeparators(QDir(root).relativeFilePath(path));
}

bool copyTree(const QString &srcRoot, const QString &destRoot, const QString &rel, int *count, QString *failed)
{
    QDir src(QDir(srcRoot).filePath(rel));
    const QFileInfoList entries = src.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (int i = 0; i < entries.size(); ++i) {
        const QFileInfo info = entries.at(i);
        const QString name = info.fileName();
        if (info.isDir()) {
            if (isSkippedDir(name))
                continue;
            if (!copyTree(srcRoot, destRoot, QDir(rel).filePath(name), count, failed))
                return false;
            continue;
        }
        if (name.startsWith(QLatin1Char('.')))
            continue;
        const QString to = QDir(destRoot).filePath(QDir(rel).filePath(name));
        QDir().mkpath(QFileInfo(to).absolutePath());
        if (QFileInfo(to).exists() && !QFile::remove(to)) {
            *failed = to;
            return false;
        }
        if (!QFile::copy(info.absoluteFilePath(), to)) {
            *failed = to;
            return false;
        }
        ++(*count);
    }
    return true;
}

// Parent folder shared by every picked file, so sibling imports survive the copy.
QString commonAncestor(const QStringList &files)
{
    QStringList dirs;
    for (int i = 0; i < files.size(); ++i)
        dirs << QFileInfo(files.at(i)).absolutePath();
    if (dirs.isEmpty())
        return QString();
    QString base = dirs.at(0);
    for (int i = 1; i < dirs.size(); ++i) {
        while (!dirs.at(i).startsWith(base + QLatin1Char('/')) && base.contains(QLatin1Char('/')))
            base = QFileInfo(base).absolutePath();
    }
    return base;
}

bool sameText(const QString &a, const QString &b)
{
    return a.trimmed() == b.trimmed();
}

// 当前表单和刚读回来的那份比一比，用来判断有没有改过。关键帧按行比，
// style 不比（没有控件，永远交回空串）。
bool sameBrief(const BriefData &a, const BriefData &b)
{
    if (!sameText(a.title, b.title) || !sameText(a.audience, b.audience)
        || !sameText(a.logline, b.logline) || !sameText(a.lastLine, b.lastLine)
        || !sameText(a.place, b.place) || !sameText(a.change, b.change)
        || !sameText(a.cast, b.cast) || !sameText(a.hero, b.hero)
        || !sameText(a.refs, b.refs) || !sameText(a.drawing, b.drawing)
        || !sameText(a.shots, b.shots) || !sameText(a.action, b.action)
        || !sameText(a.result, b.result) || !sameText(a.seconds, b.seconds)
        || !sameText(a.size, b.size) || !sameText(a.materials, b.materials)
        || !sameText(a.storyboard, b.storyboard))
        return false;
    if (a.motion.size() != b.motion.size())
        return false;
    for (int i = 0; i < a.motion.size(); ++i) {
        if (!sameText(a.motion.at(i).t, b.motion.at(i).t)
            || !sameText(a.motion.at(i).x, b.motion.at(i).x)
            || !sameText(a.motion.at(i).y, b.motion.at(i).y)
            || !sameText(a.motion.at(i).action, b.motion.at(i).action))
            return false;
    }
    return true;
}

}

BriefWindow::BriefWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString::fromUtf8("做片输入"));

    m_title = new QLineEdit;
    m_audience = new QLineEdit;
    m_logline = new QPlainTextEdit;
    m_lastLine = new QLineEdit;
    m_entry = new QComboBox;
    m_entry->setEditable(false);
    m_films = new QComboBox;
    m_films->setEditable(false);
    m_films->setMinimumWidth(300);
    m_films->setToolTip(QString::fromUtf8("films/ 下每一目录一行，选中就切过去；还没有 brief.md 的会标出来，切过去就是开新一支片子"));
    m_boardLabel = new QLabel(QString::fromUtf8("未选择"));
    m_boardLabel->setWordWrap(true);
    m_scanInfo = new QLabel(QString::fromUtf8("未读取"));
    m_scanInfo->setWordWrap(true);
    m_seconds = new QSpinBox;
    m_size = new QLineEdit;
    m_size->setPlaceholderText(QString::fromUtf8("例：1080x1440；留空 = 用 JS 自己的画布"));
    m_size->setToolTip(QString::fromUtf8("一格一个模块的 JS 是按手机屏幕画的，成片画幅得在这儿说；「读取 JS」读到画幅会回填到这里"));
    m_shots = new QLineEdit;
    m_shots->setPlaceholderText(QString::fromUtf8("例：earth japan coast fukushima —— 一行镜头 id，就是推镜的顺序"));
    m_shots->setToolTip(QString::fromUtf8("JS 里没有顺序。「读取 JS」会按取景范围从大到小猜一版，觉得不对在这里改"));
    m_storyboard = new QCheckBox(QString::fromUtf8("先看分镜"));
    m_motion = new QTableWidget(0, 4);
    m_materials = new QPlainTextEdit;
    m_place = new QPlainTextEdit;
    m_change = new QPlainTextEdit;
    m_cast = new QPlainTextEdit;
    m_hero = new QLineEdit;
    m_refPreview = new QLabel(QString::fromUtf8("未选择"));
    m_action = new QPlainTextEdit;
    m_result = new QPlainTextEdit;

    m_seconds->setRange(1, 600);
    m_seconds->setValue(24);
    m_motion->setHorizontalHeaderLabels(
        QStringList() << QString::fromUtf8("时间") << QString::fromUtf8("横位置")
                      << QString::fromUtf8("纵位置") << QString::fromUtf8("动作"));
    m_motion->horizontalHeaderItem(0)->setToolTip(QString::fromUtf8("这三列写 t x y，动作写备注：主角在每个样帧里的位置"));

    // ── 分镜 JS ───────────────────────────────────────────────
    QPushButton *pickFiles = new QPushButton(QString::fromUtf8("选择 JS 文件…"));
    QPushButton *pickFolder = new QPushButton(QString::fromUtf8("选择整个目录…"));
    QPushButton *readBoard = new QPushButton(QString::fromUtf8("读取 JS"));
    pickFiles->setToolTip(QString::fromUtf8("可多选。会按原来的相对位置拷进 films/<片名>/storyboard/，import 的兄弟文件不会断"));
    pickFolder->setToolTip(QString::fromUtf8("拷整个目录，跳过 node_modules、out、build、dist"));
    readBoard->setToolTip(QString::fromUtf8("扫主 JS 和同目录的兄弟模块：推定 A/B/C/D 模式、镜头表、推镜顺序、画幅、帧率、缺哪些依赖；结果写进下面的素材框"));

    QHBoxLayout *boardButtons = new QHBoxLayout;
    boardButtons->addWidget(pickFiles);
    boardButtons->addWidget(pickFolder);
    boardButtons->addWidget(readBoard);
    boardButtons->addStretch();
    QFormLayout *board = new QFormLayout;
    board->addRow(QString::fromUtf8("存放位置"), m_boardLabel);
    board->addRow(QString::fromUtf8("主 JS"), m_entry);
    board->addRow(QString::fromUtf8("推镜顺序"), m_shots);
    board->addRow(QString::fromUtf8("读出的结果"), m_scanInfo);
    board->addRow(QString::fromUtf8("分镜说明与依赖\n（读 JS 生成）"), m_materials);
    board->addRow(QString(), boardButtons);

    // ── 故事 ─────────────────────────────────────────────────
    QFormLayout *story = new QFormLayout;
    story->addRow(QString::fromUtf8("片名"), m_title);
    story->addRow(QString::fromUtf8("讲给谁"), m_audience);
    story->addRow(QString::fromUtf8("一句话"), m_logline);
    story->addRow(QString::fromUtf8("结尾停住那句"), m_lastLine);
    story->addRow(QString::fromUtf8("片长（秒）"), m_seconds);
    story->addRow(QString::fromUtf8("画幅"), m_size);
    story->addRow(QString(), m_storyboard);

    // ── 主体接续 ─────────────────────────────────────────────
    QPushButton *addMotion = new QPushButton(QString::fromUtf8("添加一行"));
    QPushButton *removeMotion = new QPushButton(QString::fromUtf8("删除一行"));
    QHBoxLayout *motionButtons = new QHBoxLayout;
    motionButtons->addWidget(addMotion);
    motionButtons->addWidget(removeMotion);
    motionButtons->addStretch();
    QVBoxLayout *motion = new QVBoxLayout;
    motion->addWidget(m_motion);
    motion->addLayout(motionButtons);

    // ── 可选：画面说明（旧流程的字段，留着帮代理读懂分镜）────
    QPushButton *pickRef = new QPushButton(QString::fromUtf8("选择图片"));
    QHBoxLayout *refRow = new QHBoxLayout;
    refRow->addWidget(m_refPreview);
    refRow->addWidget(pickRef);
    refRow->addStretch();
    QWidget *refWrap = new QWidget;
    refWrap->setLayout(refRow);
    m_refPreview->setFixedSize(160, 120);
    m_refPreview->setAlignment(Qt::AlignCenter);
    m_refPreview->setFrameShape(QFrame::StyledPanel);

    QFormLayout *extra = new QFormLayout;
    extra->addRow(QString::fromUtf8("地点"), m_place);
    extra->addRow(QString::fromUtf8("变化"), m_change);
    extra->addRow(QString::fromUtf8("登场物"), m_cast);
    extra->addRow(QString::fromUtf8("主角"), m_hero);
    extra->addRow(QString::fromUtf8("动作"), m_action);
    extra->addRow(QString::fromUtf8("结果"), m_result);
    extra->addRow(QString::fromUtf8("参考图"), refWrap);

    QWidget *extraHolder = new QWidget;
    QVBoxLayout *extraOuter = new QVBoxLayout;
    extraOuter->setContentsMargins(0, 0, 0, 0);
    extraOuter->addLayout(extra);
    extraHolder->setLayout(extraOuter);
    QGroupBox *optional = new QGroupBox(QString::fromUtf8("可选：分镜说明（画面由 JS 定，这几项留空也能做片）"));
    optional->setCheckable(true);
    optional->setChecked(false);
    m_optional = optional;
    QVBoxLayout *optionalOuter = new QVBoxLayout;
    optionalOuter->setContentsMargins(8, 8, 8, 8);
    optionalOuter->addWidget(extraHolder);
    optional->setLayout(optionalOuter);
    optional->setToolTip(QString::fromUtf8("打开旧的 brief.md 时，这里写过内容会自动展开"));

    QVBoxLayout *page = new QVBoxLayout;
    page->addWidget(group(QString::fromUtf8("分镜 JS —— 画面由它画"), board));
    page->addWidget(group(QString::fromUtf8("故事 —— 只这几项要说清"), story));
    page->addWidget(group(QString::fromUtf8("主体接续（主角在每个样帧里的位置）"), motion));
    page->addWidget(optional);
    page->addStretch();

    QWidget *inner = new QWidget;
    inner->setLayout(page);
    QScrollArea *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);

    QPushButton *fresh = new QPushButton(QString::fromUtf8("新建片…"));
    QPushButton *open = new QPushButton(QString::fromUtf8("打开"));
    QPushButton *save = new QPushButton(QString::fromUtf8("保存"));
    QPushButton *preview = new QPushButton(QString::fromUtf8("预览"));
    QPushButton *clear = new QPushButton(QString::fromUtf8("清空"));
    fresh->setToolTip(QString::fromUtf8("在 films/ 下开一个片子目录，后面选 JS 和保存都写进那里"));
    save->setToolTip(QString::fromUtf8("写 films/<片名>/brief.md。做片的 AI 第一步就读这个文件，不用再改导演说明"));
    preview->setToolTip(QString::fromUtf8("开个窗口：storyboard/ 里每个 JS 一个按钮，点下去看那个 JS 画出来的一张静帧。"
                                           "图由 node + 无头浏览器跑真正的模块渲，落在 out/preview/"));

    QLabel *filmLabel = new QLabel(QString::fromUtf8("片子"));
    QHBoxLayout *buttons = new QHBoxLayout;
    buttons->addWidget(filmLabel);
    buttons->addWidget(m_films);
    buttons->addWidget(fresh);
    buttons->addWidget(open);
    buttons->addWidget(save);
    buttons->addWidget(preview);
    buttons->addWidget(clear);
    buttons->addStretch();

    QVBoxLayout *root = new QVBoxLayout;
    root->addWidget(scroll);
    root->addLayout(buttons);
    QWidget *central = new QWidget;
    central->setLayout(root);
    setCentralWidget(central);
    statusBar();

    connect(fresh, SIGNAL(clicked()), this, SLOT(onNewFilm()));
    connect(m_films, SIGNAL(currentIndexChanged(int)), this, SLOT(onFilmChanged()));
    connect(open, SIGNAL(clicked()), this, SLOT(onOpen()));
    connect(save, SIGNAL(clicked()), this, SLOT(onSave()));
    connect(preview, SIGNAL(clicked()), this, SLOT(onOpenPreview()));
    connect(clear, SIGNAL(clicked()), this, SLOT(onClear()));
    connect(pickRef, SIGNAL(clicked()), this, SLOT(onPickRef()));
    connect(pickFiles, SIGNAL(clicked()), this, SLOT(onPickBoardFiles()));
    connect(pickFolder, SIGNAL(clicked()), this, SLOT(onPickBoardFolder()));
    connect(readBoard, SIGNAL(clicked()), this, SLOT(onReadBoard()));
    connect(addMotion, SIGNAL(clicked()), this, SLOT(onAddMotion()));
    connect(removeMotion, SIGNAL(clicked()), this, SLOT(onRemoveMotion()));
    connect(optional, SIGNAL(toggled(bool)), extraHolder, SLOT(setVisible(bool)));

    extraHolder->setVisible(false);

    m_path = locateBrief();
    rebuildEntryList();
    if (QFileInfo(m_path).isFile()) {
        BriefData data;
        if (BriefMarkdown::load(m_path, &data))
            showData(data);
    }
    setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
    rebuildFilmList();
    m_loaded = collect();
}

void BriefWindow::addMotionRow(const QString &time, const QString &x, const QString &y, const QString &action)
{
    const int row = m_motion->rowCount();
    m_motion->insertRow(row);
    m_motion->setItem(row, 0, new QTableWidgetItem(time));
    m_motion->setItem(row, 1, new QTableWidgetItem(x));
    m_motion->setItem(row, 2, new QTableWidgetItem(y));
    m_motion->setItem(row, 3, new QTableWidgetItem(action));
}

QString BriefWindow::locateBrief() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo(dir.filePath(QString::fromUtf8("director/AI_GUIDE.md"))).isFile())
            return QDir::cleanPath(dir.filePath(QString::fromUtf8("films/earth-to-fukushima/brief.md")));
        if (!dir.cdUp())
            break;
    }
    return QString::fromUtf8("films/earth-to-fukushima/brief.md");
}

QString BriefWindow::repoRoot() const
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 8; ++i) {
        if (QFileInfo(dir.filePath(QString::fromUtf8("director/AI_GUIDE.md"))).isFile())
            return dir.absolutePath();
        if (!dir.cdUp())
            break;
    }
    QDir fromBrief(QFileInfo(m_path).absolutePath());
    fromBrief.cdUp();
    fromBrief.cdUp();
    return fromBrief.absolutePath();
}

QString BriefWindow::filmDir() const
{
    const QFileInfo info(m_path);
    if (info.isFile() || !info.absolutePath().isEmpty())
        return info.absolutePath();
    return QDir(repoRoot()).filePath(QString::fromUtf8("films/earth-to-fukushima"));
}

void BriefWindow::setBoardInfo(const QString &text)
{
    m_boardLabel->setText(text.isEmpty() ? QString::fromUtf8("未选择") : text);
}

void BriefWindow::rebuildEntryList()
{
    const QString previous = m_entry->currentData().toString();
    const QString board = QDir(filmDir()).filePath(QLatin1String(kBoardFolder));
    const QStringList files = jsFilesUnder(board);
    const QString root = repoRoot();
    m_entry->blockSignals(true);
    m_entry->clear();
    for (int i = 0; i < files.size(); ++i)
        m_entry->addItem(nativeRel(root, files.at(i)), files.at(i));
    int keep = previous.isEmpty() ? -1 : m_entry->findData(previous);
    if (keep < 0 && !files.isEmpty())
        keep = m_entry->findData(guessEntry(files, board));
    if (keep < 0)
        keep = 0;
    if (m_entry->count() > 0)
        m_entry->setCurrentIndex(keep);
    m_entry->blockSignals(false);

    if (files.isEmpty()) {
        setBoardInfo(QString::fromUtf8("未选择 —— 还没往 %1 放过 JS").arg(nativeRel(root, board)));
        m_scanInfo->setText(QString::fromUtf8("未读取"));
    } else {
        setBoardInfo(QString::fromUtf8("%1（%2 个 JS）").arg(nativeRel(root, board)).arg(files.size()));
    }
}

void BriefWindow::setEntry(const QString &path)
{
    if (path.isEmpty())
        return;
    const QString wanted = QDir::cleanPath(path);
    for (int i = 0; i < m_entry->count(); ++i) {
        const QString item = m_entry->itemText(i);
        if (item.compare(wanted, Qt::CaseInsensitive) == 0
            || item.endsWith(QLatin1Char('/') + wanted, Qt::CaseInsensitive)
            || wanted.endsWith(QLatin1Char('/') + item, Qt::CaseInsensitive)) {
            m_entry->setCurrentIndex(i);
            return;
        }
    }
}

// films/ 下的每一目录都是一支片子，全部列出来直接切，不用走文件对话框。
// 里面还没有 brief.md 的（手工建的文件夹、或旧流程留下的片子）也列，标一句，不让人以为下拉只认得已经存过的。
void BriefWindow::rebuildFilmList()
{
    const QString current = QDir::cleanPath(QFileInfo(m_path).absoluteFilePath());
    QDir films(QDir(repoRoot()).filePath(QLatin1String("films")));
    const QStringList dirs = films.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    m_films->blockSignals(true);
    m_films->clear();
    for (int i = 0; i < dirs.size(); ++i) {
        const QString brief = QDir::cleanPath(films.filePath(dirs.at(i) + QLatin1String("/brief.md")));
        QString shown = dirs.at(i);
        if (!QFileInfo(brief).isFile()) {
            shown += QString::fromUtf8(" —— 还没有 brief");
        } else {
            BriefData data;
            if (BriefMarkdown::load(brief, &data) && !data.title.trimmed().isEmpty())
                shown = QString::fromUtf8("%1 —— %2").arg(dirs.at(i), data.title.trimmed());
        }
        m_films->addItem(shown, brief);
    }
    int at = m_films->findData(current);
    if (at < 0 && !m_path.isEmpty()) {
        // 当前这份在 films/ 以外（「打开」选了别的地点的文件）：照样列一行，不然看着像丢了
        const QString shown = QFileInfo(current).dir().dirName()
            + (QFileInfo(current).isFile() ? QString() : QString::fromUtf8(" —— 还没保存"));
        m_films->insertItem(0, shown, current);
        at = 0;
    }
    m_films->setCurrentIndex(at);
    m_films->blockSignals(false);
}

// 切片子之前问一句：表单里的字还没落盘，别让下拉一划就没了。
bool BriefWindow::confirmLeave()
{
    if (sameBrief(m_loaded, collect()))
        return true;
    const int answer = QMessageBox::question(this, QString::fromUtf8("当前片子还没保存"),
        QString::fromUtf8("切走会把没写盘的输入丢掉。先把这一份存进\n%1？").arg(m_path),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::Discard)
        return true;
    return saveCurrent();
}

void BriefWindow::onFilmChanged()
{
    const QString target = m_films->currentData().toString();
    if (target.isEmpty() || target == QDir::cleanPath(QFileInfo(m_path).absoluteFilePath()))
        return;
    if (!confirmLeave()) {
        rebuildFilmList();
        return;
    }
    const bool haveBrief = QFileInfo(target).isFile();
    BriefData data;
    if (haveBrief && !BriefMarkdown::load(target, &data)) {
        statusBar()->showMessage(QString::fromUtf8("读不出来：%1").arg(target));
        rebuildFilmList();
        return;
    }
    m_path = target;
    if (haveBrief) {
        showData(data);
        rebuildFilmList();
        setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
        statusBar()->showMessage(m_path);
        return;
    }
    // 目录里还没有 brief.md：按新片摊开（表单清空、片名用目录名），存一次就生成那份 brief.md
    onClear();
    m_title->setText(QFileInfo(target).dir().dirName());
    rebuildEntryList();
    rebuildFilmList();
    m_loaded = collect();
    setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
    statusBar()->showMessage(QString::fromUtf8("这个片子目录里还没有 brief.md，已经空着摊开；存一次就生成 %1").arg(m_path));
}

void BriefWindow::onNewFilm()
{
    if (!confirmLeave())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QString::fromUtf8("新建片"),
        QString::fromUtf8("片子目录名（小写字母、数字、连字符）："), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QRegularExpression legal(QString::fromLatin1("^[a-z0-9][a-z0-9-]{0,39}$"));
    if (!legal.match(name).hasMatch()) {
        statusBar()->showMessage(QString::fromUtf8("名字不合法：只能小写字母、数字、连字符"));
        return;
    }
    QDir root(repoRoot());
    const QString folder = root.filePath(QLatin1String("films/") + name);
    if (!QDir().mkpath(folder)) {
        statusBar()->showMessage(QString::fromUtf8("建目录失败：") + folder);
        return;
    }
    onClear();
    m_path = QDir(folder).filePath(QLatin1String("brief.md"));
    m_title->setText(name);
    rebuildEntryList();
    rebuildFilmList();
    m_loaded = collect();
    setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
    statusBar()->showMessage(m_path);
}

void BriefWindow::onPickRef()
{
    const QString file = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("选择参考图"), QString(),
        QString::fromUtf8("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (file.isEmpty())
        return;
    if (m_path.isEmpty())
        m_path = locateBrief();
    QDir dir(filmDir());
    if (!dir.mkpath(QString::fromLatin1("assets"))) {
        statusBar()->showMessage(QString::fromUtf8("图片复制失败"));
        return;
    }
    const QString dest = dir.filePath(QString::fromLatin1("assets/") + QFileInfo(file).fileName());
    if (QFileInfo(dest).absoluteFilePath() != QFileInfo(file).absoluteFilePath()) {
        QFile::remove(dest);
        if (!QFile::copy(file, dest)) {
            statusBar()->showMessage(QString::fromUtf8("图片复制失败"));
            return;
        }
    }
    setRefImage(nativeRel(repoRoot(), dest));
}

void BriefWindow::onPickBoardFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QString::fromUtf8("选择分镜 JS（可多选）"), QString(),
        QString::fromUtf8("JavaScript (*.js *.mjs);;所有文件 (*)"));
    if (files.isEmpty())
        return;
    QDir dir(filmDir());
    if (!dir.mkpath(QLatin1String(kBoardFolder))) {
        statusBar()->showMessage(QString::fromUtf8("建 storyboard 目录失败"));
        return;
    }
    const QString base = commonAncestor(files);
    const QString board = dir.filePath(QLatin1String(kBoardFolder));
    int copied = 0;
    QString first;
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo info(files.at(i));
        const QString rel = QDir(base).relativeFilePath(info.absoluteFilePath());
        const QString dest = QDir(board).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        if (QFileInfo(dest).exists())
            QFile::remove(dest);
        if (!QFile::copy(info.absoluteFilePath(), dest)) {
            statusBar()->showMessage(QString::fromUtf8("JS 复制失败：") + dest);
            return;
        }
        ++copied;
        if (first.isEmpty())
            first = dest;
    }
    rebuildEntryList();
    setEntry(first);
    statusBar()->showMessage(QString::fromUtf8("已拷入 %1 个文件，正在读取").arg(copied));
    onReadBoard();
}

void BriefWindow::onPickBoardFolder()
{
    const QString chosen = QFileDialog::getExistingDirectory(
        this, QString::fromUtf8("选择分镜 JS 所在目录（连同依赖一起拷）"));
    if (chosen.isEmpty())
        return;
    QDir dir(filmDir());
    if (!dir.mkpath(QLatin1String(kBoardFolder))) {
        statusBar()->showMessage(QString::fromUtf8("建 storyboard 目录失败"));
        return;
    }
    const QString board = dir.filePath(QLatin1String(kBoardFolder));
    int copied = 0;
    QString failed;
    if (!copyTree(chosen, board, QString(), &copied, &failed)) {
        statusBar()->showMessage(QString::fromUtf8("拷贝失败：") + failed);
        return;
    }
    rebuildEntryList();
    setEntry(guessEntry(jsFilesUnder(board), board));
    statusBar()->showMessage(QString::fromUtf8("已拷入 %1 个文件，正在读取").arg(copied));
    onReadBoard();
}

void BriefWindow::onReadBoard()
{
    const QString entry = m_entry->currentData().toString();
    if (entry.isEmpty() || !QFileInfo(entry).isFile()) {
        m_scanInfo->setText(QString::fromUtf8("还没有主 JS：先用上面两个按钮选文件"));
        statusBar()->showMessage(QString::fromUtf8("没有可读取的 JS"));
        return;
    }
    const QString board = QDir(filmDir()).filePath(QLatin1String(kBoardFolder));
    const BoardScan scan = scanBoard(entry, board);
    const QString rel = nativeRel(repoRoot(), entry);
    m_scanInfo->setText(QString::fromUtf8("模式 %1 · 镜头 %2 个 · 画幅 %3 · 缺 %4 项")
                               .arg(scan.mode)
                               .arg(scan.shotIds.size())
                               .arg(scan.sizeText.isEmpty() ? QString::fromUtf8("JS 里没有，用上面填的") : scan.sizeText)
                               .arg(scan.missing.size()));
    if (!scan.sizeText.isEmpty() && m_size->text().trimmed().isEmpty())
        m_size->setText(scan.sizeText);
    if (m_shots->text().trimmed().isEmpty() && !scan.shotIds.isEmpty())
        m_shots->setText(scan.shotIds.join(QLatin1Char(' ')));
    m_materials->setPlainText(describe(scan, rel, QFileInfo(entry).size()));
    statusBar()->showMessage(QString::fromUtf8("读完记得点保存，结果写进 brief.md 的 materials"));
}

// 预览是一个独立对话框（previewdialog.h）：左边一个 JS 一个按钮，右边看渲好的静帧。
// 不把页面嵌进来：Qt 5.7 内嵌的 WebEngine 是 Chromium 45，认不了 ES module，
// 也认不了 storyboard/earth.js 里的 `**`（TECHNIQUE.md §2.1），所以只能看图，
// 要看动的就把它给浏览器（对话框里那个「在浏览器打开这一格」）。
void BriefWindow::onOpenPreview()
{
    PreviewDialog dlg(repoRoot(), filmDir(), this);
    dlg.exec();
}

void BriefWindow::showData(const BriefData &data)
{
    m_title->setText(data.title);
    m_audience->setText(data.audience);
    m_logline->setPlainText(data.logline);
    m_lastLine->setText(data.lastLine);
    m_place->setPlainText(data.place);
    m_change->setPlainText(data.change);
    m_cast->setPlainText(data.cast);
    m_hero->setText(data.hero);
    setRefImage(data.refs);
    m_action->setPlainText(data.action);
    m_result->setPlainText(data.result);
    m_seconds->setValue(data.seconds.toInt() > 0 ? data.seconds.toInt() : 24);
    m_size->setText(data.size.trimmed());
    m_shots->setText(data.shots.trimmed());
    m_materials->setPlainText(data.materials);
    m_storyboard->setChecked(data.storyboard == QLatin1String("yes"));
    m_scanInfo->setText(QString::fromUtf8("未读取"));
    rebuildEntryList();
    setEntry(data.drawing.trimmed());
    m_motion->setRowCount(0);
    for (int i = 0; i < data.motion.size(); ++i) {
        const MotionKey &key = data.motion.at(i);
        addMotionRow(key.t, key.x, key.y, key.action);
    }
    const bool hasExtra = !data.place.isEmpty() || !data.change.isEmpty() || !data.cast.isEmpty()
        || !data.hero.isEmpty() || !data.action.isEmpty() || !data.result.isEmpty() || !data.refs.isEmpty();
    m_optional->setChecked(hasExtra);
    m_loaded = collect();
}

bool BriefWindow::saveCurrent()
{
    if (m_path.isEmpty())
        m_path = locateBrief();
    if (!BriefMarkdown::save(m_path, collect()))
        return false;
    m_loaded = collect();
    return true;
}

BriefData BriefWindow::collect() const
{
    BriefData data;
    data.title = m_title->text().trimmed();
    data.style = QString();
    data.audience = m_audience->text().trimmed();
    data.logline = m_logline->toPlainText().trimmed();
    data.lastLine = m_lastLine->text().trimmed();
    data.place = m_place->toPlainText().trimmed();
    data.change = m_change->toPlainText().trimmed();
    data.cast = m_cast->toPlainText().trimmed();
    data.hero = m_hero->text().trimmed();
    data.refs = m_refPath.trimmed();
    data.drawing = m_entry->currentText().trimmed();
    data.shots = m_shots->text().trimmed();
    data.action = m_action->toPlainText().trimmed();
    data.result = m_result->toPlainText().trimmed();
    data.seconds = QString::number(m_seconds->value());
    data.size = m_size->text().trimmed();
    data.materials = m_materials->toPlainText().trimmed();
    data.storyboard = m_storyboard->isChecked() ? QString::fromLatin1("yes") : QString::fromLatin1("no");
    for (int row = 0; row < m_motion->rowCount(); ++row) {
        MotionKey key;
        QTableWidgetItem *cells[4];
        bool any = false;
        for (int col = 0; col < 4; ++col) {
            cells[col] = m_motion->item(row, col);
            if (cells[col] && !cells[col]->text().trimmed().isEmpty())
                any = true;
        }
        if (!any)
            continue;
        key.t = cells[0] ? cells[0]->text().trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')) : QString();
        key.x = cells[1] ? cells[1]->text().trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')) : QString();
        key.y = cells[2] ? cells[2]->text().trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')) : QString();
        key.action = cells[3] ? cells[3]->text().trimmed().replace(QLatin1Char('\n'), QLatin1Char(' ')) : QString();
        data.motion.append(key);
    }
    return data;
}

void BriefWindow::setRefImage(const QString &path)
{
    m_refPath = path.trimmed();
    m_refPreview->setPixmap(QPixmap());
    if (m_refPath.isEmpty()) {
        m_refPreview->setText(QString::fromUtf8("未选择"));
        return;
    }
    QString full = m_refPath;
    if (QFileInfo(full).isRelative())
        full = QDir(repoRoot()).filePath(m_refPath);
    const QPixmap picture(full);
    if (picture.isNull()) {
        m_refPreview->setText(QString::fromUtf8("无法显示"));
        return;
    }
    m_refPreview->setText(QString());
    m_refPreview->setPixmap(picture.scaled(m_refPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void BriefWindow::onOpen()
{
    if (!confirmLeave())
        return;
    const QString file = QFileDialog::getOpenFileName(
        this, QString::fromUtf8("打开 brief.md"), QDir(repoRoot()).filePath(QLatin1String("films")),
        QString::fromUtf8("Markdown (*.md)"));
    if (file.isEmpty())
        return;
    BriefData data;
    if (!BriefMarkdown::load(file, &data)) {
        statusBar()->showMessage(QString::fromUtf8("打开失败"));
        return;
    }
    m_path = file;
    showData(data);
    rebuildFilmList();
    setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
    statusBar()->showMessage(m_path);
}

void BriefWindow::onSave()
{
    if (!saveCurrent()) {
        statusBar()->showMessage(QString::fromUtf8("保存失败"));
        return;
    }
    rebuildFilmList();   // 「还没有 brief」那一行存完就该换成片名
    if (collect().drawing.isEmpty())
        statusBar()->showMessage(QString::fromUtf8("已保存，但还没有选分镜 JS：%1").arg(m_path));
    else
        statusBar()->showMessage(QString::fromUtf8("已保存 %1 —— 做片的 AI 读这个文件就够，不用再写导演说明").arg(m_path));
}

void BriefWindow::onSaveAs()
{
    const QString file = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("另存为 brief.md"),
        m_path.isEmpty() ? locateBrief() : m_path,
        QString::fromUtf8("Markdown (*.md)"));
    if (file.isEmpty())
        return;
    const QString previous = m_path;
    m_path = file;
    if (!saveCurrent()) {
        m_path = previous;
        statusBar()->showMessage(QString::fromUtf8("保存失败"));
        return;
    }
    rebuildEntryList();
    rebuildFilmList();
    setBoardInfo(QString::fromUtf8("当前 brief：%1").arg(m_path));
    statusBar()->showMessage(m_path);
}

void BriefWindow::onClear()
{
    m_title->clear();
    m_audience->clear();
    m_logline->clear();
    m_lastLine->clear();
    m_place->clear();
    m_change->clear();
    m_cast->clear();
    m_hero->clear();
    setRefImage(QString());
    m_action->clear();
    m_result->clear();
    m_seconds->setValue(24);
    m_materials->clear();
    m_storyboard->setChecked(false);
    m_motion->setRowCount(0);
    m_size->clear();
    m_shots->clear();
    m_scanInfo->setText(QString::fromUtf8("未读取"));
    if (m_optional)
        m_optional->setChecked(false);
    m_loaded = collect();
    statusBar()->showMessage(QString::fromUtf8("已清空"));
}

void BriefWindow::onAddMotion()
{
    m_motion->insertRow(m_motion->rowCount());
}

void BriefWindow::onRemoveMotion()
{
    int row = m_motion->currentRow();
    if (row >= 0)
        m_motion->removeRow(row);
}
