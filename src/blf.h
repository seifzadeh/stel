#pragma once
#include <QByteArray>
#include <QMap>
#include <QString>
#include <QMetaType>

namespace stel {
enum class BlfState { Unknown, Subscribing, Idle, Ringing, Busy, Error };
QString blfStateLabel(BlfState);
struct BlfInfo {
    int key = -1;
    BlfState state = BlfState::Unknown;
    QString detail;
};
// RFC 4235 full/partial documents. Track every dialog: ending one call must
// not turn the lamp green while another call is still active.
class DialogInfo {
public:
    bool update(const QByteArray &,QString *error=nullptr);
    BlfState state() const;
    QString entity() const { return entity_; }
private:
    qint64 version_ = -1;
    QString entity_;
    QMap<QString,QString> dialogs_;
};
}
Q_DECLARE_METATYPE(stel::BlfInfo)
