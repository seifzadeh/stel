#include "model.h"
#include "secretstore.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSet>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUuid>

namespace stel {
QJsonObject MessageInfo::toJson() const {
    return {{"id",id},{"accountId",accountId},{"peer",peer},{"peerName",peerName},{"text",text},
        {"state",state},{"error",error},{"timestamp",timestamp.toUTC().toString(Qt::ISODateWithMs)},
        {"incoming",incoming},{"unread",unread},{"sipStatus",sipStatus}};
}
MessageInfo MessageInfo::fromJson(const QJsonObject &o) {
    MessageInfo m;m.id=o["id"].toString();m.accountId=o["accountId"].toString();m.peer=o["peer"].toString();
    m.peerName=o["peerName"].toString();m.text=o["text"].toString();m.state=o["state"].toString();m.error=o["error"].toString();
    m.timestamp=QDateTime::fromString(o["timestamp"].toString(),Qt::ISODateWithMs);
    m.incoming=o["incoming"].toBool();m.unread=o["unread"].toBool();m.sipStatus=o["sipStatus"].toInt();return m;
}
QString messageStateLabel(const MessageInfo &m) {
    if(m.incoming)return "Received";
    if(m.state=="sending")return "Sending…";
    if(m.state=="sent")return "Sent";
    if(m.state=="delivered")return "Delivered";
    if(m.state=="read")return "Read";
    if(m.state=="interrupted")return "Send interrupted — delivery unknown";
    return "Not sent";
}
QString mediaSecurityLabel(const QString &mode){if(mode=="none")return "RTP";if(mode=="dtls")return "DTLS-SRTP";return mode.toUpper();}
QJsonObject Account::toJson() const {
    return {{"id",id},{"label",label},{"callerIdName",callerIdName},{"autoAnswer",autoAnswer},{"username",username},{"domain",domain},{"server",server},
        {"authUser",authUser},{"transport",transport},{"certificate",certificate},{"privateKey",privateKey},
        {"encryption",encryption},{"enabled",enabled},{"register",registerEnabled},{"ice",ice},{"rememberPassword",rememberPassword},
        {"webrtc",webrtc},{"stunServer",stunServer},{"turn",turn},{"turnUser",turnUser},
        {"forwardTo",forwardTo},{"forwardBusy",forwardBusy},{"expires",expires}};
}
Account Account::fromJson(const QJsonObject &o) {
    Account a;
    a.id=o["id"].toString(QUuid::createUuid().toString(QUuid::WithoutBraces));
    a.label=o["label"].toString(); a.username=o["username"].toString(); a.domain=o["domain"].toString();
    // Older versions used the local account label as their SIP display name.
    a.callerIdName=o["callerIdName"].toString(a.label);a.autoAnswer=o["autoAnswer"].toBool(false);
    a.server=o["server"].toString(); a.authUser=o["authUser"].toString();
    a.transport=o["transport"].toString("tls"); a.certificate=o["certificate"].toString();
    a.privateKey=o["privateKey"].toString(); a.encryption=o["encryption"].toString("none");
    a.enabled=o["enabled"].toBool(true); a.registerEnabled=o["register"].toBool(true);
    a.rememberPassword=o["rememberPassword"].toBool(true);
    a.ice=o["ice"].toBool(true); a.webrtc=o["webrtc"].toBool(); a.stunServer=o["stunServer"].toString();
    a.turn=o["turn"].toBool(); a.turnUser=o["turnUser"].toString();
    a.forwardTo=o["forwardTo"].toString(); a.forwardBusy=o["forwardBusy"].toBool();
    a.expires=o["expires"].toInt(600); return a;
}
QString Account::validate() const {
    if (id.isEmpty() || username.trimmed().isEmpty() || domain.trimmed().isEmpty())
        return "Account ID, username and domain are required.";
    if(!QRegularExpression("^[A-Za-z0-9_-]{1,80}$").match(id).hasMatch())return "Invalid account ID.";
    if(callerIdName.size()>256)return "Caller ID Name must be at most 256 characters.";
    for(const auto ch:callerIdName)if(ch.category()==QChar::Other_Control)return "Caller ID Name must not contain control characters.";
    if (username.contains('@') || username.contains(QChar('\n')) || domain.contains('/') || domain.contains('@'))
        return "Enter a username and a domain separately (no sip: prefix).";
    if (!QStringList{"udp","tcp","tls"}.contains(transport)) return "This engine supports SIP UDP/TCP/TLS only. SCTP/WSS need a separate backend or gateway.";
    if (!QStringList{"none","srtp","dtls","zrtp"}.contains(encryption)) return "Unknown media encryption.";
    if (certificate.isEmpty() != privateKey.isEmpty()) return "mTLS requires both a client certificate and a private key.";
    if (!certificate.isEmpty() && transport != "tls") return "Client certificates require TLS transport.";
    for (const auto &path : {certificate,privateKey})
        if (!path.isEmpty() && !QFile::exists(path)) return "Certificate/key file does not exist: " + path;
    if (webrtc && (encryption != "dtls" || !ice)) return "WebRTC media profile requires DTLS-SRTP and ICE.";
    if (turn && (stunServer.isEmpty() || turnUser.isEmpty() || turnPassword.isEmpty())) return "TURN requires server, username and a session password.";
    if (expires < 60 || expires > 86400) return "Registration expiry must be 60–86400 seconds.";
    return {};
}
QJsonObject Preferences::toJson() const {
    return {{"caFile",caFile},{"inputDevice",inputDevice},{"outputDevice",outputDevice},{"videoDevice",videoDevice},{"dnd",dnd},
        {"signallingAddress",signallingAddress},{"mediaAddress",mediaAddress},
        {"customAudioCodecs",customAudioCodecs},{"customVideoCodecs",customVideoCodecs},
        {"audioCodecs",QJsonArray::fromStringList(audioCodecs)},{"videoCodecs",QJsonArray::fromStringList(videoCodecs)},
        {"sipUdpPort",sipUdpPort},{"sipTcpPort",sipTcpPort},{"sipTlsPort",sipTlsPort},
        {"audioPortMin",audioPortMin},{"audioPortMax",audioPortMax},{"videoPortMin",videoPortMin},{"videoPortMax",videoPortMax}};
}
Preferences Preferences::fromJson(const QJsonObject &o) {
    Preferences p; p.caFile=o["caFile"].toString(); p.inputDevice=o["inputDevice"].toString();
    p.outputDevice=o["outputDevice"].toString(); p.videoDevice=o["videoDevice"].toString(); p.dnd=o["dnd"].toBool();
    p.signallingAddress=o["signallingAddress"].toString();p.mediaAddress=o["mediaAddress"].toString();
    p.customAudioCodecs=o["customAudioCodecs"].toBool();p.customVideoCodecs=o["customVideoCodecs"].toBool();
    for(const auto &id:o["audioCodecs"].toArray())p.audioCodecs<<id.toString();
    for(const auto &id:o["videoCodecs"].toArray())p.videoCodecs<<id.toString();
    p.sipUdpPort=o["sipUdpPort"].toInt(-1);p.sipTcpPort=o["sipTcpPort"].toInt(-1);p.sipTlsPort=o["sipTlsPort"].toInt(-1);
    p.audioPortMin=o["audioPortMin"].toInt(10000);p.audioPortMax=o["audioPortMax"].toInt(10198);
    p.videoPortMin=o["videoPortMin"].toInt(11000);p.videoPortMax=o["videoPortMax"].toInt(11198);return p;
}
QString Preferences::validate() const {
    for(const auto &ip:{signallingAddress,mediaAddress})
        if(!ip.isEmpty() && QHostAddress(ip).isNull())return "Choose a valid local IP address.";
    for(int port:{sipUdpPort,sipTcpPort,sipTlsPort})if(port < -1 || port>65535)return "SIP ports must be Auto (-1), disabled (0), or 1–65535.";
    if(sipTcpPort>0 && sipTcpPort==sipTlsPort)return "TCP and TLS cannot listen on the same IP and port.";
    if(audioPortMin<1024 || videoPortMin<1024 || audioPortMax>65534 || videoPortMax>65534 ||
       audioPortMax<audioPortMin || videoPortMax<videoPortMin ||
       audioPortMin%2 || audioPortMax%2 || videoPortMin%2 || videoPortMax%2)
        return "Media ranges must use even ports between 1024 and 65534; RTCP uses the next port.";
    if(audioPortMin<=videoPortMax+1 && videoPortMin<=audioPortMax+1)return "Audio and video port ranges must not overlap (including RTCP).";
    return {};
}
QVector<LocalAddress> localAddresses() {
    QVector<LocalAddress> result;
    for(const auto &iface:QNetworkInterface::allInterfaces()) {
        if(!iface.flags().testFlag(QNetworkInterface::IsUp))continue;
        for(const auto &entry:iface.addressEntries()) {
            auto ip=entry.ip();if(ip.isNull()||ip.isMulticast())continue;
            if(ip.protocol()==QAbstractSocket::IPv6Protocol && ip.isLinkLocal() && ip.scopeId().isEmpty())ip.setScopeId(iface.name());
            result.append({ip.toString(),iface.humanReadableName()});
        }
    }
    return result;
}
Store::Store(QString directory) : directory_(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) : directory) {
    QDir().mkpath(directory_);
    QFile::setPermissions(directory_,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
}
bool Store::load(QVector<Account> &accounts, Preferences &prefs, QString *error) const {
    QFile f(directory_+"/settings.json"); if (!f.exists()) return true;
    if (!f.open(QIODevice::ReadOnly)) { if(error)*error=f.errorString(); return false; }
    QJsonParseError parse; const auto doc=QJsonDocument::fromJson(f.readAll(),&parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || doc.object()["schema"].toInt()!=1) {
        if(error)*error="Invalid settings or unsupported schema; original file has been preserved.";
        return false;
    }
    const auto o=doc.object(); QVector<Account> loaded;
    for(const auto &v:o["accounts"].toArray()) loaded.append(Account::fromJson(v.toObject()));
    QByteArray secrets;bool needSecrets=false;for(const auto &a:loaded)needSecrets|=a.rememberPassword;
    credentialReadFailed_=needSecrets&&!SecretStore(directory_).read(secrets,error);
    if(credentialReadFailed_)secrets.clear();
    credentialSnapshot_=secrets;const auto credentials=QJsonDocument::fromJson(secrets).object();
    for(auto &a:loaded) {
        if(a.rememberPassword){const auto secret=credentials[a.id].toObject();a.password=secret["password"].toString();a.keyPassword=secret["keyPassword"].toString();a.turnPassword=secret["turnPassword"].toString();}
        a.credentialsRequired=a.registerEnabled&&a.password.isEmpty()&&a.certificate.isEmpty();
    }
    accounts=loaded; prefs=Preferences::fromJson(o["preferences"].toObject()); return true;
}
bool Store::save(const QVector<Account> &accounts, const Preferences &prefs, QString *error) const {
    // Never replace credentials that could not be read (for example a locked
    // keyring) with an incomplete account set.
    if(credentialReadFailed_) {
        if(!SecretStore(directory_).read(credentialSnapshot_,error))return false;
        credentialReadFailed_=false;
    }
    QJsonArray list; for(const auto &a:accounts) list.append(a.toJson());
    QSaveFile f(directory_+"/settings.json");
    if (!f.open(QIODevice::WriteOnly)) { if(error)*error=f.errorString(); return false; }
    f.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    QJsonObject credentials;const auto previous=QJsonDocument::fromJson(credentialSnapshot_).object();
    for(const auto &a:accounts)if(a.rememberPassword) {
        if(a.credentialsRequired&&a.password.isEmpty()&&a.keyPassword.isEmpty()&&a.turnPassword.isEmpty()){if(previous.contains(a.id))credentials[a.id]=previous[a.id];continue;}
        if(!a.password.isEmpty()||!a.keyPassword.isEmpty()||!a.turnPassword.isEmpty())credentials[a.id]=QJsonObject{{"password",a.password},{"keyPassword",a.keyPassword},{"turnPassword",a.turnPassword}};
    }
    const auto secrets=credentials.isEmpty()?QByteArray():QJsonDocument(credentials).toJson(QJsonDocument::Compact);
    const bool credentialsChanged=secrets!=credentialSnapshot_;
    if(credentialsChanged&&!SecretStore(directory_).write(secrets,error))return false;
    const auto data=QJsonDocument(QJsonObject{{"schema",1},{"accounts",list},{"preferences",prefs.toJson()}}).toJson();
    if (f.write(data)!=data.size() || !f.commit()) { if(error)*error=f.errorString();if(credentialsChanged)SecretStore(directory_).write(credentialSnapshot_);return false; }
    credentialSnapshot_=secrets;return true;
}
QVector<QJsonObject> Store::history() const {
    QFile f(directory_+"/history.json"); if(!f.open(QIODevice::ReadOnly))return {};
    QVector<QJsonObject> r; for(const auto &v:QJsonDocument::fromJson(f.readAll()).array())r.append(v.toObject()); return r;
}
void Store::appendHistory(const QJsonObject &entry) const {
    auto all=history(); all.prepend(entry); QJsonArray list;
    for(int i=0;i<all.size() && i<300;++i)list.append(all[i]);
    QSaveFile f(directory_+"/history.json"); if(f.open(QIODevice::WriteOnly)) {
        f.setPermissions(QFile::ReadOwner|QFile::WriteOwner); f.write(QJsonDocument(list).toJson()); f.commit();
    }
}
QVector<QJsonObject> Store::contacts() const {
    QFile f(directory_+"/contacts.json"); if(!f.open(QIODevice::ReadOnly))return {};
    QVector<QJsonObject> r; for(const auto &v:QJsonDocument::fromJson(f.readAll()).array())r.append(v.toObject()); return r;
}
bool Store::saveContacts(const QVector<QJsonObject> &contacts) const {
    QJsonArray list; for(const auto &v:contacts)list.append(v);
    QSaveFile f(directory_+"/contacts.json"); if(!f.open(QIODevice::WriteOnly))return false;
    f.setPermissions(QFile::ReadOwner|QFile::WriteOwner); f.write(QJsonDocument(list).toJson()); return f.commit();
}
bool Store::loadMessages(QVector<MessageInfo> &messages,QString *error) const {
    QFile file(directory_+"/messages.json");if(!file.exists()){messages.clear();return true;}
    if(!file.open(QIODevice::ReadOnly)){if(error)*error=file.errorString();return false;}
    QJsonParseError parse;const auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError||!doc.isObject()||doc["schema"].toInt()!=1||!doc["messages"].isArray()){
        if(error)*error="Message history is invalid; the original file has been preserved.";
        return false;
    }
    QVector<MessageInfo> loaded;QSet<QString> ids;
    for(const auto &value:doc["messages"].toArray()){
        const auto message=MessageInfo::fromJson(value.toObject());
        if(message.id.isEmpty()||message.accountId.isEmpty()||message.peer.isEmpty()||!message.timestamp.isValid()||ids.contains(message.id)){
            if(error)*error="Message history contains an invalid entry; the original file has been preserved.";
            return false;
        }
        ids.insert(message.id);loaded.append(message);
    }
    messages=loaded;return true;
}
bool Store::saveMessages(const QVector<MessageInfo> &messages,QString *error) const {
    QJsonArray list;for(const auto &message:messages)list.append(message.toJson());
    QSaveFile file(directory_+"/messages.json");if(!file.open(QIODevice::WriteOnly)){if(error)*error=file.errorString();return false;}
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);
    const auto data=QJsonDocument(QJsonObject{{"schema",1},{"messages",list}}).toJson();
    if(file.write(data)!=data.size()||!file.commit()){if(error)*error=file.errorString();return false;}return true;
}
QString normalizeTarget(const QString &target, const QString &domain) {
    QString s=target.trimmed();
    if(s.isEmpty() || s.contains('\r') || s.contains('\n'))return {};
    if(s.startsWith("sip:",Qt::CaseInsensitive)||s.startsWith("sips:",Qt::CaseInsensitive))return s;
    if(!s.contains('@'))s+='@'+domain;
    return "sip:"+s;
}
}
