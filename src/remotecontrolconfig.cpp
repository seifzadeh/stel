#include "remotecontrolconfig.h"
#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QRegularExpression>
namespace stel {
QJsonObject RemoteControlConfig::toJson() const {
    return {{"enabled",enabled},{"address",address},{"port",port},{"allowedPeers",QJsonArray::fromStringList(allowedPeers)},
        {"tokenHash",tokenHash},{"certificate",certificate},{"privateKey",privateKey}};
}
RemoteControlConfig RemoteControlConfig::fromJson(const QJsonObject &o){
    RemoteControlConfig c;c.enabled=o["enabled"].toBool();c.address=o["address"].toString("127.0.0.1");c.port=o["port"].toInt(8877);
    if(o.contains("allowedPeers")){c.allowedPeers.clear();for(const auto &v:o["allowedPeers"].toArray())c.allowedPeers<<v.toString();}
    c.tokenHash=o["tokenHash"].toString();c.certificate=o["certificate"].toString();c.privateKey=o["privateKey"].toString();return c;
}
QString RemoteControlConfig::validate() const {
    if(!enabled)return {};
    if(QHostAddress(address).isNull())return "Remote API: choose a local IP address.";
    if(port<1||port>65535)return "Remote API port must be 1–65535.";
    if(!QRegularExpression("^[0-9a-f]{64}$").match(tokenHash).hasMatch())return "Generate an API token before enabling remote control.";
    if(allowedPeers.isEmpty())return "Add at least one allowed client IP or CIDR subnet.";
    for(const auto &peer:allowedPeers){const auto subnet=QHostAddress::parseSubnet(peer);if(subnet.first.isNull()||subnet.second<0)return "Invalid allowed IP/CIDR: "+peer;}
    if(certificate.isEmpty()!=privateKey.isEmpty())return "HTTPS needs both a certificate and private key.";
    return {};
}
bool RemoteControlConfig::permits(const QString &peer) const {
    QHostAddress ip(peer);if(ip.isNull())return false;
    bool ipv4=false;const auto v4=ip.toIPv4Address(&ipv4);if(ipv4)ip=QHostAddress(v4);
    for(const auto &value:allowedPeers){const auto subnet=QHostAddress::parseSubnet(value);if(ip.isInSubnet(subnet))return true;}
    return false;
}
QString RemoteControlConfig::newToken(){QByteArray bytes;for(int n=0;n<8;++n){const auto value=QRandomGenerator::system()->generate();bytes.append(reinterpret_cast<const char*>(&value),sizeof(value));}return QString::fromLatin1(bytes.toHex());}
QString RemoteControlConfig::hashToken(const QString &token){return QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(),QCryptographicHash::Sha256).toHex());}
}
