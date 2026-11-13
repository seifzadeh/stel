#pragma once
#include <QJsonObject>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

namespace stel {
struct Account {
    QString id, label, callerIdName, username, domain, server, authUser, password;
    QString transport = "tls";
    QString certificate, privateKey, keyPassword;
    QString encryption = "none";
    bool enabled = true, registerEnabled = true, ice = true, webrtc = false;
    bool rememberPassword = true;
    bool autoAnswer = false;
    bool credentialsRequired = false; // Runtime state; never serialized.
    QString stunServer, turnUser, turnPassword, forwardTo;
    bool turn = false, forwardBusy = false;
    int expires = 600;
    QJsonObject toJson() const;
    static Account fromJson(const QJsonObject &);
    QString validate() const;
};
struct Preferences {
    QString caFile, inputDevice, outputDevice, videoDevice;
    QString signallingAddress, mediaAddress;
    int sipUdpPort = -1, sipTcpPort = -1, sipTlsPort = -1;
    int audioPortMin = 10000, audioPortMax = 10198;
    int videoPortMin = 11000, videoPortMax = 11198;
    bool dnd = false;
    bool customAudioCodecs = false, customVideoCodecs = false;
    QStringList audioCodecs, videoCodecs; // Enabled codec IDs, in offer preference order.
    QJsonObject toJson() const;
    static Preferences fromJson(const QJsonObject &);
    QString validate() const;
};
struct LocalAddress { QString address, interfaceName; };
QVector<LocalAddress> localAddresses();
struct MessageInfo {
    QString id, accountId, peer, peerName, text;
    QString state, error;
    QDateTime timestamp;
    bool incoming=false, unread=false;
    int sipStatus=0;
    QJsonObject toJson() const;
    static MessageInfo fromJson(const QJsonObject &);
};
QString messageStateLabel(const MessageInfo &);
class Store {
public:
    explicit Store(QString directory = {});
    QString directory() const { return directory_; }
    bool load(QVector<Account> &, Preferences &, QString *error = nullptr) const;
    bool save(const QVector<Account> &, const Preferences &, QString *error = nullptr) const;
    void appendHistory(const QJsonObject &) const;
    QVector<QJsonObject> history() const;
    QVector<QJsonObject> contacts() const;
    bool saveContacts(const QVector<QJsonObject> &) const;
    bool loadMessages(QVector<MessageInfo> &, QString *error=nullptr) const;
    bool saveMessages(const QVector<MessageInfo> &, QString *error=nullptr) const;
private:
    QString directory_;
    mutable QByteArray credentialSnapshot_;
    mutable bool credentialReadFailed_ = false;
};
QString normalizeTarget(const QString &target, const QString &domain);
QString mediaSecurityLabel(const QString &mode);
}
