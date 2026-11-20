#ifndef BRIEFWINDOW_H
#define BRIEFWINDOW_H

#include "briefdata.h"

#include <QMainWindow>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QComboBox;
class QGroupBox;
class QSpinBox;
class QCheckBox;
class QTableWidget;

class BriefWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit BriefWindow(QWidget *parent = 0);

private slots:
    void onNewFilm();
    void onFilmChanged();
    void onOpen();
    void onSave();
    void onSaveAs();   // 底部那个位置给了「预览」，按钮没了；槽留着，想接回去一行 connect 就行
    void onClear();
    void onPickRef();
    void onPickBoardFiles();
    void onPickBoardFolder();
    void onReadBoard();
    void onOpenPreview();   // 开 PreviewDialog，不再生成也不弹浏览器
    void onAddMotion();
    void onRemoveMotion();

private:
    void addMotionRow(const QString &time, const QString &x, const QString &y, const QString &action);
    QString locateBrief() const;
    QString repoRoot() const;
    QString filmDir() const;
    void rebuildEntryList();
    void rebuildFilmList();
    bool confirmLeave();
    void setBoardInfo(const QString &text);
    BriefData collect() const;
    bool saveCurrent();
    void showData(const BriefData &data);
    void setRefImage(const QString &path);
    void setEntry(const QString &path);

    QString m_path;
    QString m_refPath;
    BriefData m_loaded;   // 上次读回／清空／存盘后的快照，用来判断有没有改过

    QLineEdit *m_title;
    QLineEdit *m_audience;
    QPlainTextEdit *m_logline;
    QLineEdit *m_lastLine;
    QComboBox *m_entry;
    QComboBox *m_films;
    QLabel *m_boardLabel;
    QLabel *m_scanInfo;
    QSpinBox *m_seconds;
    QLineEdit *m_size;
    QLineEdit *m_shots;
    QCheckBox *m_storyboard;
    QTableWidget *m_motion;

    QPlainTextEdit *m_materials;
    QGroupBox *m_optional;
    QPlainTextEdit *m_place;
    QPlainTextEdit *m_change;
    QPlainTextEdit *m_cast;
    QLineEdit *m_hero;
    QLabel *m_refPreview;
    QPlainTextEdit *m_action;
    QPlainTextEdit *m_result;
};

#endif
