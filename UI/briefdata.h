#ifndef BRIEFDATA_H
#define BRIEFDATA_H

#include <QString>
#include <QVector>

struct MotionKey {
    QString t;
    QString x;
    QString y;
    QString action;
};

struct BriefData {
    QString title;
    QString style;
    QString audience;
    QString logline;
    QString lastLine;
    QString place;
    QString change;
    QString cast;
    QString hero;
    QString refs;
    QString drawing;
    QString shots;   // 推镜顺序：一行镜头 id，空格隔开
    QString action;
    QString result;
    QString seconds;
    QString size;
    QString materials;
    QString storyboard;
    QVector<MotionKey> motion;
};

#endif
