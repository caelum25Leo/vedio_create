#ifndef PREVIEWDIALOG_H
#define PREVIEWDIALOG_H

#include <QDialog>
#include <QPixmap>
#include <QProcess>
#include <QStringList>
// QProcess 必须真包含、不能只前向声明：下面的槽签名带了 QProcess::ExitStatus，
// AUTOMOC 生成的 moc_previewdialog.cpp 要 reinterpret_cast 这个枚举，光看名字不够。

class QLabel;
class QPushButton;
class QVBoxLayout;

// 「预览」对话框：把 films/<片名>/out/preview/ 下每一个 png（= 一个能画出图的 storyboard JS）
// 列成一个按钮，点下去看那张静帧，外加刷新和关闭。
//
// 为什么是图、不是网页：Qt 5.7 自带的 WebEngine 是 Chromium 45，认不了 ES module，
// 也认不了用户 JS 里的 **（见 director/TECHNIQUE.md §2.1），嵌进来连原始 JS 都解析不了。
// 图由 `node tools/preview/build-preview.mjs <片子目录> --stills` 在无头 msedge 里跑
// 真正的垫片和真正的模块渲出来，落在 out/preview/<文件名>.png；旁边同名 .txt 写尺寸和
// 像素统计。画不出来的 JS 不生成 png、也不会出现在按钮列表里。
class PreviewDialog : public QDialog {
    Q_OBJECT
public:
    PreviewDialog(const QString &repoRoot, const QString &filmDir, QWidget *parent = 0);
    // 渲图渲到一半就关窗口：不留一个还在后台跑的 node（Qt 拆对象时也会抱怨一句）。
    // 被杀掉时可能只写了一半图，下次进来 showStill 会报“图读不出来”，不假装成功
    ~PreviewDialog();

private slots:
    void onPickShot();
    void onRefresh();
    void onStillsFinished(int exitCode, QProcess::ExitStatus status);

private:
    QStringList boardFiles() const;                 // storyboard/ 下的 .js，按文件名排序
    QString shortName(const QString &file) const;   // storyboard/hud.js → hud
    QString stillPath(const QString &file) const;
    QString notePath(const QString &file) const;
    QString readNote(const QString &file) const;
    void buildButtons();
    // buildIfMissing = false 用于“刚渲完还是没图”那一次：不能再顺手起 node，不然就死循环了
    void showStill(const QString &file, bool buildIfMissing = true);
    void rescale();
    // thenShow 是渲完要接着显示的 JS；onlyName 非空时给打包器传 --only=<name>，
    // 只渲那一格 —— 点按钮按需出图用，比全量渲快得多。
    void runStills(const QString &thenShow, const QString &onlyName = QString());
    QString findScript() const;
    QString findNode() const;
    void resizeEvent(QResizeEvent *event);

    QString m_repoRoot;
    QString m_filmDir;
    QString m_current;      // 现在看的是哪个 JS 的绝对路径
    QString m_pending;      // 渲完要接着显示的那个
    QPixmap m_pix;          // 原图；缩放只在显示时做，免得反复丢细节
    QWidget *m_buttonHost;
    QVBoxLayout *m_buttonBox;
    QLabel *m_image;
    QLabel *m_note;
    QProcess *m_proc;
};

#endif
