#ifndef BRIEFMARKDOWN_H
#define BRIEFMARKDOWN_H

#include "briefdata.h"

class BriefMarkdown {
public:
    static bool load(const QString &path, BriefData *data);
    static bool save(const QString &path, const BriefData &data);
};

#endif
