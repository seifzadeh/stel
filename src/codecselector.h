#pragma once
#include "sipengine.h"
#include <QGroupBox>
class QListWidget;
namespace stel {
class CodecSelector : public QGroupBox {
public:
    CodecSelector(const QVector<CodecInfo> &, bool video, bool custom, const QStringList &selected, QWidget *parent=nullptr);
    QStringList selectedIds() const;
    bool custom() const { return custom_; }
private:
    QVector<CodecInfo> inventory_;
    QListWidget *list_;
    bool custom_;
    void populate(bool defaults, const QStringList &selected = {});
};
}
