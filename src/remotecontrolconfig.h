#pragma once
#include <QJsonObject>
#include <QStringList>
namespace stel {
struct RemoteControlConfig {
    bool enabled=false;
    QString address="127.0.0.1";
    int port=8877;
    QStringList allowedPeers={"127.0.0.1/32","::1/128"};
    QString tokenHash,certificate,privateKey;
    QJsonObject toJson() const;
    static RemoteControlConfig fromJson(const QJsonObject &);
    QString validate() const;
    bool permits(const QString &) const;
    static QString newToken();
    static QString hashToken(const QString &);
};
}
