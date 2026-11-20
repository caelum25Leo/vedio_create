#ifndef STYLECATALOG_H
#define STYLECATALOG_H

struct StyleEntry {
    const char *folder;
    const char *english;
    const char *chinese;
};

int styleCatalogCount();
const StyleEntry *styleCatalog();

#endif
