#include "remotecontrol.h"
#include "chatwidget.h"
#include "secretstore.h"
#include "faxdocument.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <bctoolbox/crypto.h>
namespace stel {
// Reuse the SDK TLS backend on both platforms, including Qt 5.15.2/Windows 7.
// No separate Qt/OpenSSL runtime is needed by the portable application.
struct ApiTlsConfig {
    bctbx_ssl_config_t *config=bctbx_ssl_config_new();
    bctbx_x509_certificate_t *certificate=bctbx_x509_certificate_new();
    bctbx_signing_key_t *key=bctbx_signing_key_new();
    bctbx_rng_context_t *random=bctbx_rng_context_new();
    ~ApiTlsConfig(){bctbx_ssl_config_free(config);bctbx_x509_certificate_free(certificate);bctbx_signing_key_free(key);bctbx_rng_context_free(random);}
    bool load(const RemoteControlConfig &c){
        QFile certificateFile(c.certificate),keyFile(c.privateKey);
        if(!certificateFile.open(QIODevice::ReadOnly)||!keyFile.open(QIODevice::ReadOnly))return false;
        auto certificateBytes=certificateFile.readAll(),keyBytes=keyFile.readAll();certificateBytes.append('\0');keyBytes.append('\0');
        // AEAD suites for TLS 1.2/1.3; older protocol versions cannot negotiate these.
        static const int suites[]={0x1301,0x1302,0x1303,0xc02f,0xc030,0xc02b,0xc02c,0};
        return bctbx_x509_certificate_parse(certificate,certificateBytes.constData(),size_t(certificateBytes.size()))==0&&
            bctbx_signing_key_parse(key,keyBytes.constData(),size_t(keyBytes.size()),nullptr,0)==0&&
            bctbx_ssl_config_defaults(config,BCTBX_SSL_IS_SERVER,BCTBX_SSL_TRANSPORT_STREAM)==0&&
            bctbx_ssl_config_set_authmode(config,BCTBX_SSL_VERIFY_NONE)==0&&
            bctbx_ssl_config_set_rng(config,[](void *ctx,unsigned char *bytes,size_t length){return int(bctbx_rng_get(static_cast<bctbx_rng_context_t*>(ctx),bytes,length));},random)==0&&
            bctbx_ssl_config_set_ciphersuites(config,suites)==0&&
            bctbx_ssl_config_set_own_cert(config,certificate,key)==0;
    }
};
struct ApiTlsSession {
    std::shared_ptr<ApiTlsConfig> owner;
    bctbx_ssl_context_t *context=bctbx_ssl_context_new();
    bool connected=false;
    explicit ApiTlsSession(std::shared_ptr<ApiTlsConfig> config):owner(std::move(config)){}
    ~ApiTlsSession(){bctbx_ssl_context_free(context);}
    bool setup(QTcpSocket *socket){
        if(bctbx_ssl_context_setup(context,owner->config)!=0)return false;
        bctbx_ssl_set_io_callbacks(context,socket,
            [](void *data,const unsigned char *bytes,size_t length){const auto n=static_cast<QTcpSocket*>(data)->write(reinterpret_cast<const char*>(bytes),qint64(length));return n<0?BCTBX_ERROR_NET_CONN_RESET:int(n);},
            [](void *data,unsigned char *bytes,size_t length){auto *socket=static_cast<QTcpSocket*>(data);if(!socket->bytesAvailable())return BCTBX_ERROR_NET_WANT_READ;const auto n=socket->read(reinterpret_cast<char*>(bytes),qint64(length));return n<0?BCTBX_ERROR_NET_CONN_RESET:int(n);});
        return true;
    }
};

namespace {
constexpr int MaxHeaders=16384,MaxBody=16*1024*1024,MaxClients=16,MaxEvents=500;
QString uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces);}
QJsonArray objects(const QVector<QJsonObject> &values){QJsonArray result;for(const auto &v:values)result.append(v);return result;}
QJsonObject success(){return {{"ok",true}};}
ApiReply accepted(const QJsonObject &data){ApiReply r;r.status=202;r.data=data;return r;}
ApiReply engineResult(bool ok,const SipEngine &engine){return ok?ApiReply{200,success(),{},{"application/json; charset=utf-8"}}:ApiReply::error(409,"operation_failed",engine.lastError());}
bool incoming(const CallInfo &c){return c.incoming&&(c.rawState==LinphoneCallIncomingReceived||c.rawState==LinphoneCallIncomingEarlyMedia);}
bool validBase64(const QByteArray &text){
    if(text.isEmpty()||text.size()%4)return false;
    int end=text.size();while(end>0&&text[end-1]=='=')--end;
    if(text.size()-end>2)return false;
    for(int n=0;n<end;++n){const auto c=text[n];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))return false;}
    return true;
}
bool validText(const QString &value,int max){return !value.trimmed().isEmpty()&&value.size()<=max&&!value.contains('\r')&&!value.contains('\n');}
}
ApiReply ApiReply::error(int status,const QString &code,const QString &message){ApiReply r;r.status=status;r.data={{"ok",false},{"error",QJsonObject{{"code",code},{"message",message}}}};return r;}
RemoteControlServer::RemoteControlServer(SipEngine &engine,Store &store,QVector<Account> &accounts,Preferences &prefs,ChatWidget &chat,QObject *parent)
    :QTcpServer(parent),engine_(engine),store_(store),accounts_(accounts),prefs_(prefs),chat_(chat){
    setObjectName("remoteControlServer");uptime_.start();
    const QDir root(store_.directory()+"/api-documents");
    for(const auto &entry:root.entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(documents_.size()>=128)break;
        if(!QRegularExpression("^[0-9a-f-]{36}$").match(entry).hasMatch())continue;
        QFile metadata(root.filePath(entry+"/document.json"));if(!metadata.open(QIODevice::ReadOnly))continue;
        const auto info=QJsonDocument::fromJson(metadata.readAll()).object();if(info["id"]!=entry)continue;
        Document d;d.info=info;d.output=root.filePath(entry+"/a4.tif");d.control=std::make_shared<FaxConversionControl>();documents_[entry]=d;
    }

    connect(&engine_,&SipEngine::errorOccurred,this,[this](const QString &message){event("engine.error",{{"message",message}});});
    connect(&engine_,&SipEngine::notification,this,[this](const QString &message){event("engine.notification",{{"message",message}});});
    connect(&engine_,&SipEngine::callChanged,this,[this](const CallInfo &call){event("call.changed",callJson(call));QTimer::singleShot(0,this,[this,call]{advanceTransfers(call);});});
    connect(&engine_,&SipEngine::callFinished,this,[this](const CallInfo &call){event("call.finished",callJson(call));QTimer::singleShot(0,this,[this,call]{advanceTransfers(call,true);});});
    connect(&engine_,&SipEngine::accountState,this,[this](const QString &id,const QString &state,const QString &detail){event("account.changed",{{"id",id},{"state",state},{"detail",detail}});});
    connect(&engine_,&SipEngine::messageChanged,this,[this](const MessageInfo &message){event("message.changed",message.toJson());});
    connect(&engine_,&SipEngine::faxChanged,this,[this](const FaxInfo &fax){event("fax.changed",faxJson(fax));});
    connect(&engine_,&SipEngine::blfChanged,this,[this](const BlfInfo &info){event("blf.changed",{{"key",info.key+1},{"state",blfStateLabel(info.state)},{"detail",info.detail}});});
}
RemoteControlServer::~RemoteControlServer(){stop();for(auto &d:documents_)d.control->cancelled=true;}
void RemoteControlServer::setSelectionProviders(std::function<QString()> call,std::function<QString()> account){selectedCall_=std::move(call);selectedAccount_=std::move(account);}
void RemoteControlServer::stop(){close();const auto sockets=clients_.keys();for(auto *socket:sockets){disconnect(socket,nullptr,this,nullptr);socket->abort();socket->deleteLater();}clients_.clear();emit stateChanged();}
bool RemoteControlServer::configure(const RemoteControlConfig &next,QString *error){
    const auto invalid=next.validate();if(!invalid.isEmpty()){if(error)*error=invalid;return false;}
    if(!next.enabled){stop();config_=next;return true;}
    std::shared_ptr<ApiTlsConfig> tls;
    if(!next.certificate.isEmpty()){tls=std::make_shared<ApiTlsConfig>();if(!tls->load(next)){if(error)*error="Cannot load the HTTPS PEM certificate chain/private key using the SIP SDK TLS backend.";return false;}}
    const auto old=config_;const bool wasListening=isListening();
    // Changing credentials/allow-list revokes already accepted requests.
    stop();
    config_=next;
    if(!listen(QHostAddress(next.address),quint16(next.port))){const auto reason=errorString();config_=old;if(wasListening)listen(QHostAddress(old.address),quint16(old.port));if(error)*error=reason;emit stateChanged();return false;}
    tls_=tls;emit stateChanged();return true;
}
QString RemoteControlServer::endpoint() const {if(!isListening())return {};const QString host=config_.address.contains(':')?"["+config_.address+"]":config_.address;return (config_.certificate.isEmpty()?"http://":"https://")+host+":"+QString::number(serverPort());}
void RemoteControlServer::incomingConnection(qintptr descriptor){
    auto *socket=new QTcpSocket(this);
    if(!socket->setSocketDescriptor(descriptor)){socket->deleteLater();return;}
    if(clients_.size()>=MaxClients||!config_.permits(socket->peerAddress().toString())){socket->abort();socket->deleteLater();return;}
    socket->setReadBufferSize(MaxBody+MaxHeaders+1);clients_.insert(socket,{});
    connect(socket,&QTcpSocket::readyRead,this,[this,socket]{read(socket);});
    connect(socket,&QTcpSocket::disconnected,this,[this,socket]{clients_.remove(socket);socket->deleteLater();});
    auto *timeout=new QTimer(socket);timeout->setSingleShot(true);connect(timeout,&QTimer::timeout,socket,[socket]{socket->abort();});timeout->start(15000);clients_[socket].timeout=timeout;
    if(tls_){auto session=std::make_shared<ApiTlsSession>(tls_);if(!session->setup(socket)){socket->abort();return;}clients_[socket].tls=session;}

}
bool RemoteControlServer::authenticate(const QByteArray &header) const {
    QString token;
    if(header.startsWith("Bearer "))token=QString::fromLatin1(header.mid(7));
    else if(header.startsWith("Basic ")){const auto decoded=QByteArray::fromBase64(header.mid(6));if(decoded.startsWith("stel:"))token=QString::fromLatin1(decoded.mid(5));}
    const auto hash=RemoteControlConfig::hashToken(token).toLatin1(),expected=config_.tokenHash.toLatin1();unsigned difference=unsigned(hash.size()^expected.size());
    for(int n=0;n<qMin(hash.size(),expected.size());++n)difference|=unsigned(uchar(hash[n])^uchar(expected[n]));
    return !token.isEmpty()&&difference==0;
}
void RemoteControlServer::read(QTcpSocket *socket){
    if(!clients_.contains(socket)||clients_[socket].answered)return;
    auto &client=clients_[socket];
    if(client.tls){auto &tls=*client.tls;
        if(!tls.connected){const auto result=bctbx_ssl_handshake(tls.context);if(result==BCTBX_ERROR_NET_WANT_READ||result==BCTBX_ERROR_NET_WANT_WRITE)return;if(result!=0){socket->abort();return;}tls.connected=true;}
        unsigned char bytes[16384];
        for(;;){const auto count=bctbx_ssl_read(tls.context,bytes,sizeof(bytes));if(count>0){client.buffer.append(reinterpret_cast<const char*>(bytes),count);if(client.buffer.size()>MaxBody+MaxHeaders){send(socket,ApiReply::error(413,"body_too_large","Request exceeds 16 MiB."));return;}}
            else if(count==BCTBX_ERROR_NET_WANT_READ||count==BCTBX_ERROR_NET_WANT_WRITE)break;else if(count==0){socket->disconnectFromHost();return;}else{socket->abort();return;}}
    }else client.buffer+=socket->readAll();
    const auto reject=[&](int status,const QString &code,const QString &text){send(socket,ApiReply::error(status,code,text));};
    if(!client.headerSize){
        const int end=client.buffer.indexOf("\r\n\r\n");
        if(end<0){if(client.buffer.size()>MaxHeaders)reject(431,"headers_too_large","HTTP headers exceed 16 KiB.");return;}
        if(end>MaxHeaders){reject(431,"headers_too_large","HTTP headers exceed 16 KiB.");return;}
        const auto lines=client.buffer.left(end).split('\n');const auto start=lines.value(0).trimmed().split(' ');
        if(start.size()!=3||!(start[2]=="HTTP/1.1"||start[2]=="HTTP/1.0")||!start[1].startsWith('/')||start[1].startsWith("//")){reject(400,"bad_request","Use an origin-form HTTP/1.0 or HTTP/1.1 request.");return;}
        QHash<QByteArray,QByteArray> headers;
        for(int n=1;n<lines.size();++n){const auto line=lines[n].trimmed();const int colon=line.indexOf(':');if(colon<1||lines[n].startsWith(' ')||lines[n].startsWith('\t')){reject(400,"bad_headers","Invalid HTTP header.");return;}
            const auto name=line.left(colon).toLower();if(headers.contains(name)){reject(400,"duplicate_header","Duplicate HTTP headers are not supported.");return;}headers[name]=line.mid(colon+1).trimmed();}
        if(headers.contains("origin")||headers.value("sec-fetch-site")=="cross-site"){reject(403,"browser_origin","Cross-origin browser requests are disabled.");return;}
        if(headers.contains("transfer-encoding")||headers.contains("expect")){reject(400,"unsupported_framing","Use Content-Length; chunked requests and Expect are not supported.");return;}
        if(!authenticate(headers.value("authorization"))){reject(401,"unauthorized","Use Authorization: Bearer <token>, or Basic with username stel and the token as password.");return;}
        client.authenticated=true;client.headerSize=end+4;
        const auto length=headers.value("content-length","0");bool valid=false;client.length=length.toLongLong(&valid);
        if(!valid||!QRegularExpression("^[0-9]+$").match(QString::fromLatin1(length)).hasMatch()||client.length<0||client.length>MaxBody){reject(413,"body_too_large","Content-Length must be between 0 and 16 MiB.");return;}
        if(client.length&&headers.value("content-type").split(';').first().trimmed()!="application/json"){reject(415,"content_type","Send application/json encoded as UTF-8.");return;}
    }
    if(client.buffer.size()>client.headerSize+client.length){reject(400,"pipelining_disabled","One request is allowed per connection.");return;}
    if(client.buffer.size()<client.headerSize+client.length)return;
    const auto start=client.buffer.left(client.buffer.indexOf("\r\n")).split(' ');const auto method=start[0];const auto url=QUrl::fromEncoded(start[1],QUrl::StrictMode);
    if(!url.isValid()||!url.fragment().isEmpty()){reject(400,"invalid_url","Invalid request URL.");return;}
    QJsonObject body;
    if(client.length){QJsonParseError parse;const auto doc=QJsonDocument::fromJson(client.buffer.mid(client.headerSize),&parse);if(parse.error!=QJsonParseError::NoError||!doc.isObject()){reject(400,"invalid_json","Body must be a JSON object.");return;}body=doc.object();}
    if(dispatching_||credentialOperationActive()){reject(503,"busy","A configuration operation is in progress; retry later.");return;}
    const QScopedValueRollback<bool> guard(dispatching_,true);send(socket,dispatch(method,url,body));
}
void RemoteControlServer::send(QTcpSocket *socket,const ApiReply &reply){
    if(!clients_.contains(socket)||clients_[socket].answered)return;
    clients_[socket].answered=true;clients_[socket].timeout->start(60000);
    const auto body=reply.body.isNull()?QJsonDocument(reply.data).toJson(QJsonDocument::Compact):reply.body;
    const QMap<int,QByteArray> reasons={{200,"OK"},{201,"Created"},{202,"Accepted"},{400,"Bad Request"},{401,"Unauthorized"},{403,"Forbidden"},{404,"Not Found"},{405,"Method Not Allowed"},{409,"Conflict"},{410,"Gone"},{413,"Payload Too Large"},{415,"Unsupported Media Type"},{422,"Unprocessable Entity"},{429,"Too Many Requests"},{431,"Request Header Fields Too Large"},{500,"Internal Server Error"},{503,"Service Unavailable"}};
    QByteArray header="HTTP/1.1 "+QByteArray::number(reply.status)+" "+reasons.value(reply.status,"Error")+"\r\nContent-Type: "+reply.contentType+"\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n";
    if(reply.status==401)header+="WWW-Authenticate: Basic realm=\"Stel Remote Control\"\r\n";
    const auto response=header+"\r\n"+body;
    if(auto tls=clients_[socket].tls){int offset=0;while(offset<response.size()){const auto count=bctbx_ssl_write(tls->context,reinterpret_cast<const unsigned char*>(response.constData()+offset),size_t(qMin(16384,response.size()-offset)));if(count<=0){socket->abort();return;}offset+=count;}bctbx_ssl_close_notify(tls->context);}
    else socket->write(response);
    socket->disconnectFromHost();
}
void RemoteControlServer::event(const QString &type,const QJsonObject &data){events_.append(QJsonObject{{"id",QString::number(++eventId_)},{"type",type},{"timestamp",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},{"data",data}});while(events_.size()>MaxEvents)events_.removeFirst();}
QJsonObject RemoteControlServer::callJson(const CallInfo &c){return {{"id",c.id},{"accountId",c.accountId},{"remote",c.remote},{"remoteName",c.remoteName},{"remoteUsername",c.remoteUsername},{"accountName",c.accountName},{"state",c.state},{"stateCode",int(c.rawState)},{"message",c.message},{"incoming",c.incoming},{"video",c.video},{"muted",c.muted},{"recording",c.recording},{"inConference",c.inConference},{"conferenceParticipants",c.conferenceParticipants},{"duration",c.duration},{"encryption",c.encryption},{"audioCodec",c.audioCodec},{"videoCodec",c.videoCodec},{"audioDownKbps",c.audioDownKbps},{"videoDownKbps",c.videoDownKbps},{"paging",c.paging},{"pagingAutoAnswered",c.pagingAutoAnswered},{"sipStatus",c.sipStatus},{"error",c.error}};}
QJsonObject RemoteControlServer::faxJson(const FaxInfo &f){auto o=f.toJson();o.remove("path");o["downloadUrl"]="/api/v1/faxes/"+f.id+"/file";return o;}
QJsonArray RemoteControlServer::accountsJson() const {QJsonArray result;for(const auto &a:accounts_){auto o=a.toJson();o.remove("privateKey");o.remove("certificate");o["registrationState"]=engine_.registrationState(a.id);o["credentialsRequired"]=a.credentialsRequired;result.append(o);}return result;}
QJsonArray RemoteControlServer::blfJson() const {QJsonArray result;for(int n=0;n<ExpansionKeyCount;++n){auto o=prefs_.expansionKeys[n].toJson();const auto info=engine_.blfInfo(n);o["key"]=n+1;o["state"]=blfStateLabel(info.state);o["stateCode"]=int(info.state);o["detail"]=info.detail;o["colour"]=info.state==BlfState::Idle?"green":info.state==BlfState::Busy||info.state==BlfState::Ringing?"red":"grey";result.append(o);}return result;}
QJsonObject RemoteControlServer::status() const {QJsonArray transfers;for(const auto &t:transfers_)transfers.append(QJsonObject{{"id",t.id},{"callId",t.original},{"otherCallId",t.consultation},{"state",t.state},{"error",t.error}});QJsonArray calls,faxes;for(const auto &c:engine_.calls())calls.append(callJson(c));for(const auto &f:engine_.faxes())faxes.append(faxJson(f));return {{"version",STEL_VERSION},{"apiVersion",1},{"liblinphone",QString::fromUtf8(linphone_core_get_version())},{"uptimeSeconds",double(uptime_.elapsed()/1000)},{"dnd",prefs_.dnd},{"accounts",accountsJson()},{"calls",calls},{"blf",blfJson()},{"faxes",faxes},{"transfers",transfers},{"unreadMessages",chat_.unreadCount()},{"lastEventId",QString::number(eventId_)},{"eventSession",QString::number(QCoreApplication::applicationPid())+"-"+QString::number(uptime_.msecsSinceReference())}};}
QString RemoteControlServer::accountId(const QString &requested) const {if(!requested.isEmpty()){for(const auto &a:accounts_)if(a.id==requested||a.username+'@'+a.domain==requested||"sip:"+a.username+'@'+a.domain==requested)return a.id;return {};}
    if(selectedAccount_){const auto id=selectedAccount_();for(const auto &a:accounts_)if(a.id==id&&a.enabled)return id;}QString found;for(const auto &a:accounts_)if(a.enabled){if(!found.isEmpty())return {};found=a.id;}return found;}
QString RemoteControlServer::callId(const QString &requested,bool ringing) const {const auto calls=engine_.calls();if(!requested.isEmpty()){for(const auto &c:calls)if(c.id==requested)return c.id;return {};}
    if(selectedCall_){const auto selected=selectedCall_();for(const auto &c:calls)if(c.id==selected&&(!ringing||incoming(c)))return c.id;}QString found;for(const auto &c:calls)if(!ringing||incoming(c)){if(!found.isEmpty())return {};found=c.id;}return found;}
bool RemoteControlServer::configurationAllowed(QString &error) const {if(canConfigure_&&!canConfigure_()){error="Close Settings before changing configuration through the API.";return false;}return true;}
ApiReply RemoteControlServer::dial(const QJsonObject &body){const auto account=accountId(body["accountId"].toString());const auto target=body["target"].toString();if(account.isEmpty()||!validText(target,1024))return ApiReply::error(422,"invalid_destination","Choose an existing accountId and a destination.");
    QSet<QString> before;for(const auto &c:engine_.calls())before.insert(c.id);if(!engine_.dial(account,target,body["video"].toBool()))return engineResult(false,engine_);
    for(const auto &c:engine_.calls())if(!before.contains(c.id)&&!c.incoming)return accepted({{"ok",true},{"call",callJson(c)}});
    return accepted(success());}
ApiReply RemoteControlServer::callAction(const QString &id,const QString &action,const QJsonObject &body){
    CallInfo call;for(const auto &c:engine_.calls())if(c.id==id)call=c;if(call.id.isEmpty())return ApiReply::error(404,"call_not_found","The call no longer exists.");
    if(action=="answer"){if(!incoming(call))return ApiReply::error(409,"not_ringing","Only a ringing incoming call can be answered.");return engineResult(engine_.answer(id,body["video"].toBool()),engine_);}
    if(action=="reject"){if(!incoming(call))return ApiReply::error(409,"not_ringing","Reject applies to a ringing incoming call.");return engineResult(engine_.hangup(id),engine_);}
    if(action=="hangup")return engineResult(engine_.hangup(id),engine_);
    if(action=="hold"||action=="resume")return engineResult(engine_.hold(id,action=="hold"),engine_);
    if(action=="mute"||action=="unmute")return engineResult(engine_.muteCall(id,action=="mute"),engine_);
    if(action=="record"||action=="stop-recording")return engineResult(engine_.record(id,action=="record"),engine_);
    if(action=="transfer"||action=="forward"){const auto target=body["target"].toString();if(!validText(target,1024))return ApiReply::error(422,"invalid_destination","Enter a destination.");return engineResult(action=="forward"?engine_.redirect(id,target):engine_.transfer(id,target),engine_);}
    if(action=="attended-transfer"||action=="conference"){const auto other=body["otherCallId"].toString();if(other==id||callId(other).isEmpty()||other.isEmpty())return ApiReply::error(422,"invalid_peer","Choose a different existing otherCallId.");return engineResult(action=="conference"?engine_.conference(id,other):engine_.attendedTransfer(id,other),engine_);}
    if(action=="dtmf"){const auto digit=body["digit"].toString();if(digit.size()!=1||!QString("0123456789*#ABCD").contains(digit))return ApiReply::error(422,"invalid_digit","Send one DTMF digit: 0–9, *, # or A–D.");return engineResult(engine_.dtmf(id,digit[0].toLatin1()),engine_);}
    if(action=="fax"){if(!call.incoming)return ApiReply::error(409,"not_incoming","Receive fax applies to an incoming call.");return engineResult(engine_.receiveFax(id,body["protocol"].toString("t38")),engine_);}
    return ApiReply::error(404,"unknown_action","Unknown call action.");
}
ApiReply RemoteControlServer::preferences(const QJsonObject &body){
    QString error;if(!configurationAllowed(error))return ApiReply::error(409,"settings_open",error);
    if(!engine_.calls().isEmpty())return ApiReply::error(409,"active_calls","Change media/network configuration when no calls are active.");
    auto json=prefs_.toJson();for(auto i=body.begin();i!=body.end();++i){
        if(i.key()=="remoteControl"||!json.contains(i.key()))return ApiReply::error(422,"unknown_setting","Unknown or locally managed setting: "+i.key());
        if(i.value().type()!=json[i.key()].type())return ApiReply::error(422,"invalid_type","Incorrect type for "+i.key());
        if(i.value().isDouble()&&i.value().toDouble()!=double(i.value().toInt(-2147483647)))return ApiReply::error(422,"invalid_integer","Use a whole number for "+i.key());
        json[i.key()]=i.value();
    }
    auto next=Preferences::fromJson(json);error=next.validate();if(!error.isEmpty())return ApiReply::error(422,"invalid_settings",error);
    for(const auto &key:next.expansionKeys)if(key.type!="disabled"&&accountId(key.accountId).isEmpty())return ApiReply::error(422,"unknown_account","Expansion keys need an existing accountId.");
    const auto old=prefs_;
    const auto apply=[this](const Preferences &p){return engine_.applyNetwork(p)&&engine_.applyDevices(p)&&engine_.applyCodecSelection(false,p.audioCodecs,p.customAudioCodecs)&&engine_.applyCodecSelection(true,p.videoCodecs,p.customVideoCodecs);};
    if(!apply(next)){error=engine_.lastError();apply(old);return ApiReply::error(409,"apply_failed",error);}
    if(!store_.save(accounts_,next,&error)){apply(old);return ApiReply::error(500,"save_failed",error);}
    prefs_=next;engine_.setDnd(next.dnd);engine_.setExpansionKeys(next.expansionKeys);emit resourceChanged("configuration");event("configuration.changed",{{"resource","preferences"}});return {200,success(),{},{"application/json; charset=utf-8"}};
}
ApiReply RemoteControlServer::accountWrite(const QString &id,const QJsonObject &body,bool remove){
    QString error;if(!configurationAllowed(error))return ApiReply::error(409,"settings_open",error);
    int index=-1;for(int n=0;n<accounts_.size();++n)if(accounts_[n].id==id)index=n;
    if(!id.isEmpty()&&index<0)return ApiReply::error(404,"account_not_found","Account does not exist.");
    for(const auto &c:engine_.calls())if(c.accountId==id)return ApiReply::error(409,"account_in_use","End this account's calls before editing or removing it.");
    const auto before=accounts_;auto next=accounts_;Account account;
    if(remove)next.removeAt(index);
    else {
        if(index>=0)account=accounts_[index];
        auto json=account.toJson();
        for(auto i=body.begin();i!=body.end();++i){if(i.key()=="password"||i.key()=="keyPassword"||i.key()=="turnPassword"){if(!i.value().isString()||i.value().toString().size()>4096)return ApiReply::error(422,"invalid_credentials","Credentials must be strings of at most 4096 characters.");continue;}
            if(!json.contains(i.key())||i.value().type()!=json[i.key()].type())return ApiReply::error(422,"invalid_account_field","Unknown field or incorrect type: "+i.key());
            if(i.value().isDouble()&&i.value().toDouble()!=double(i.value().toInt(-2147483647)))return ApiReply::error(422,"invalid_integer","Use a whole number for "+i.key());
            json[i.key()]=i.value();}
        const auto old=account;account=Account::fromJson(json);if(index<0&&account.id.isEmpty())account.id=uuid();
        if(index>=0&&account.id!=id)return ApiReply::error(422,"immutable_id","Account IDs cannot be changed.");
        account.password=body["password"].toString(old.password);account.keyPassword=body["keyPassword"].toString(old.keyPassword);account.turnPassword=body["turnPassword"].toString(old.turnPassword);
        account.credentialsRequired=false;error=account.validate();if(!error.isEmpty())return ApiReply::error(422,"invalid_account",error);
        for(int n=0;n<next.size();++n)if(n!=index&&next[n].id==account.id)return ApiReply::error(409,"duplicate_id","An account with this ID already exists.");
        if(index<0)next.append(account);else next[index]=account;
    }
    if(!store_.save(next,prefs_,&error))return ApiReply::error(500,"save_failed",error);
    const bool removed=index<0||engine_.removeAccount(id);
    const bool applied=removed&&(remove||engine_.addAccount(account));
    if(!applied){error=engine_.lastError();store_.save(before,prefs_);if(index>=0)engine_.addAccount(before[index]);return ApiReply::error(409,"account_failed",error);}
    accounts_=next;emit resourceChanged("configuration");event("configuration.changed",{{"resource","accounts"}});
    ApiReply result;result.status=index<0?201:200;result.data={{"ok",true},{"accountId",remove?id:account.id}};return result;
}
ApiReply RemoteControlServer::uploadDocument(const QJsonObject &body){
    int active=0;for(const auto &d:documents_)active+=d.info["state"]=="converting";
    if(active>=2||documents_.size()>=128)return ApiReply::error(429,"document_limit","At most two conversions and 128 API documents are allowed. Delete unused documents.");
    const bool text=body.contains("text");if(text==body.contains("base64"))return ApiReply::error(422,"source_required","Send either text, or filename plus base64.");
    const auto content=body["text"].toString();const auto name=body["filename"].toString();const auto extension=QFileInfo(name).suffix().toLower();
    if(text&&(content.trimmed().isEmpty()||content.size()>2*1024*1024))return ApiReply::error(422,"invalid_text","Text must be nonempty and at most 2 Mi characters.");
    if(!text&&(!QStringList{"pdf","txt","tif","tiff","png","jpg","jpeg"}.contains(extension)||name.size()>256))return ApiReply::error(422,"invalid_file","Use a PDF, TXT, TIFF, PNG or JPEG filename.");
    QByteArray bytes;
    if(!text){const auto encoded=body["base64"].toString().toLatin1();if(!validBase64(encoded))return ApiReply::error(422,"invalid_base64","Use standard padded base64 without whitespace.");bytes=QByteArray::fromBase64(encoded);}
    const auto id=uuid(),directory=store_.directory()+"/api-documents/"+id;QDir().mkpath(directory);QFile::setPermissions(directory,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    Document d;d.output=directory+"/a4.tif";d.source=directory+"/input."+extension;d.control=std::make_shared<FaxConversionControl>();
    d.info={{"id",id},{"state","converting"},{"pages",0},{"filename",text?QString("text.txt"):QFileInfo(name).fileName()},{"pageSize","A4"},{"width",FaxPageWidth},{"height",FaxPageHeight}};
    if(!text){QSaveFile file(d.source);if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit()){QDir(directory).removeRecursively();return ApiReply::error(500,"write_failed","Could not save the uploaded document.");}QFile::setPermissions(d.source,QFile::ReadOwner|QFile::WriteOwner);}
    documents_[id]=d;initializeFaxDocumentFonts();auto *watcher=new QFutureWatcher<FaxDocumentResult>(this);
    connect(watcher,&QFutureWatcher<FaxDocumentResult>::finished,this,[this,watcher,id,d]{const auto converted=watcher->result();watcher->deleteLater();if(!documents_.contains(id))return;
        auto &doc=documents_[id];doc.info["state"]=converted.ok?"ready":converted.cancelled?"cancelled":"failed";doc.info["pages"]=converted.pages;doc.info["error"]=converted.error;
        QFile::remove(d.source);if(converted.ok)QFile::setPermissions(d.output,QFile::ReadOwner|QFile::WriteOwner);
        QSaveFile metadata(QFileInfo(d.output).absolutePath()+"/document.json");if(metadata.open(QIODevice::WriteOnly)){metadata.setPermissions(QFile::ReadOwner|QFile::WriteOwner);metadata.write(QJsonDocument(doc.info).toJson());metadata.commit();}
        event("document.changed",doc.info);
    });
    watcher->setFuture(QtConcurrent::run([d,text,content]{return text?convertFaxText(content,d.output,d.control):convertFaxDocument(d.source,d.output,d.control);}));return accepted({{"ok",true},{"document",d.info}});
}
ApiReply RemoteControlServer::dispatch(const QByteArray &method,const QUrl &url,const QJsonObject &body){
    if(method!="GET"&&method!="POST"&&method!="PUT"&&method!="PATCH"&&method!="DELETE")return ApiReply::error(405,"method_not_allowed","Use GET, POST, PUT, PATCH or DELETE.");
    if(method=="GET"&&!body.isEmpty())return ApiReply::error(400,"unexpected_body","GET requests must not contain a body.");
    for(const auto &flag:{"video"})if(body.contains(flag)&&!body[flag].isBool())return ApiReply::error(422,"invalid_boolean",QString(flag)+" must be true or false.");
    const auto path=url.path();const QUrlQuery query(url);QSet<QString> parameters;for(const auto &item:query.queryItems()){if(parameters.contains(item.first))return ApiReply::error(400,"duplicate_parameter","Duplicate query parameters are not supported.");parameters.insert(item.first);}
    if(path=="/servlet"||path=="/cgi-bin/ConfigManApp.com"||path=="/cgi-bin/cgiServer.exx"){if(method!="GET")return ApiReply::error(405,"method_not_allowed","Yealink Action URI uses GET.");return yealink(url);}
    if(method=="GET"&&(path=="/screencapture"||path=="/screencapture/download"||path=="/api/v1/screen")){if(!screenshot_)return ApiReply::error(503,"screen_unavailable","No main display is attached.");ApiReply r;r.body=screenshot_();r.contentType="image/jpeg";return r;}
    if(!path.startsWith("/api/v1"))return ApiReply::error(404,"not_found","Use /api/v1 or /servlet.");
    const auto parts=path.split('/',Qt::SkipEmptyParts);if(parts.size()<2||parts[0]!="api"||parts[1]!="v1")return ApiReply::error(404,"not_found","Unknown API version.");
    const auto collection=parts.value(2),id=parts.value(3),action=parts.value(4);
    if(parts.size()>5)return ApiReply::error(404,"not_found","Unknown endpoint.");
    if(method=="GET"&&parts.size()==2)return {200,{{"apiVersion",1},{"resources",QJsonArray{"status","calls","accounts","blf","expansion","messages","faxes","documents","contacts","history","devices","codecs","preferences","events"}},{"documentation","docs/API.md"},{"yealinkActionUri","/servlet?key=number=1002"}},{},{"application/json; charset=utf-8"}};
    if(collection=="status"&&parts.size()==3&&method=="GET")return {200,status(),{},{"application/json; charset=utf-8"}};
    if(collection=="events"&&parts.size()==3&&method=="GET"){
        bool valid=true;const auto since=query.hasQueryItem("since")?query.queryItemValue("since",QUrl::FullyDecoded).toULongLong(&valid):0;
        if(!valid)return ApiReply::error(422,"invalid_cursor","since must be a nonnegative event ID.");
        QJsonArray result;for(const auto &v:events_)if(v.toObject()["id"].toString().toULongLong()>since)result.append(v);
        return {200,{{"events",result},{"lastEventId",QString::number(eventId_)},{"eventSession",QString::number(QCoreApplication::applicationPid())+"-"+QString::number(uptime_.msecsSinceReference())},{"oldestEventId",events_.isEmpty()?QString::number(eventId_+1):events_.first().toObject()["id"].toString()},{"resetRequired",since>eventId_||(!events_.isEmpty()&&since>0&&since+1<events_.first().toObject()["id"].toString().toULongLong())}},{},{"application/json; charset=utf-8"}};
    }
    if(collection=="calls"){
        if(method=="GET"&&parts.size()<=4){if(id.isEmpty()){QJsonArray result;for(const auto &c:engine_.calls())result.append(callJson(c));return {200,{{"calls",result}},{},{"application/json; charset=utf-8"}};}for(const auto &c:engine_.calls())if(c.id==id)return {200,{{"call",callJson(c)}},{},{"application/json; charset=utf-8"}};return ApiReply::error(404,"call_not_found","Call no longer exists.");}
        if(method=="POST"&&parts.size()==3)return dial(body);
        if(method=="POST"&&parts.size()==5)return callAction(id,action,body);
        if(method=="DELETE"&&parts.size()==4)return callAction(id,"hangup",{});
    }
    if(collection=="accounts"){
        if(method=="GET"&&parts.size()<=4){if(id.isEmpty())return {200,{{"accounts",accountsJson()}},{},{"application/json; charset=utf-8"}};for(const auto &a:accountsJson())if(a.toObject()["id"]==id)return {200,{{"account",a}},{},{"application/json; charset=utf-8"}};return ApiReply::error(404,"account_not_found","Unknown accountId.");}
        if(method=="POST"&&parts.size()==3)return accountWrite({},body);
        if((method=="PUT"||method=="PATCH")&&parts.size()==4)return accountWrite(id,body);
        if(method=="DELETE"&&parts.size()==4)return accountWrite(id,{},true);
        if(method=="POST"&&parts.size()==5&&(action=="register"||action=="unregister")){
            QString error;if(!configurationAllowed(error))return ApiReply::error(409,"settings_open",error);auto next=accounts_;int index=-1;for(int n=0;n<next.size();++n)if(next[n].id==id)index=n;
            if(index<0)return ApiReply::error(404,"account_not_found","Unknown accountId.");
            const bool enabled=action=="register";next[index].registerEnabled=enabled;
            if(!store_.save(next,prefs_,&error))return ApiReply::error(500,"save_failed",error);
            if(!engine_.setRegistration(id,enabled)){store_.save(accounts_,prefs_);return engineResult(false,engine_);}accounts_=next;emit resourceChanged("configuration");return accepted(success());
        }
    }
    if((collection=="blf"||collection=="expansion")&&method=="GET"&&parts.size()==3)return {200,{{"keys",blfJson()}},{},{"application/json; charset=utf-8"}};
    if(collection=="expansion"&&parts.size()>=4){bool ok=false;const int index=id.toInt(&ok)-1;if(!ok||index<0||index>=ExpansionKeyCount)return ApiReply::error(404,"key_not_found","Expansion key numbers are 1–12.");
        if(method=="POST"&&action=="press"&&parts.size()==5){const auto &key=prefs_.expansionKeys[index];if(key.type=="disabled")return ApiReply::error(409,"disabled_key","This expansion key is disabled.");return dial({{"accountId",key.accountId},{"target",key.target}});}
        if(method=="PUT"&&parts.size()==4){QString error;if(!configurationAllowed(error))return ApiReply::error(409,"settings_open",error);auto next=prefs_;const auto key=ExpansionKey::fromJson(body);error=key.validate();if(!error.isEmpty()||(key.type!="disabled"&&accountId(key.accountId).isEmpty()))return ApiReply::error(422,"invalid_key",error.isEmpty()?"Choose an existing accountId.":error);next.expansionKeys[index]=key;if(!store_.save(accounts_,next,&error))return ApiReply::error(500,"save_failed",error);prefs_=next;engine_.setExpansionKeys(next.expansionKeys);emit resourceChanged("configuration");event("configuration.changed",{{"resource","expansion"},{"key",index+1}});return {200,success(),{},{"application/json; charset=utf-8"}};}
    }
    if(collection=="dnd"&&method=="POST"&&parts.size()==3){if(!body["enabled"].isBool())return ApiReply::error(422,"invalid_boolean","enabled must be true or false.");auto next=prefs_;next.dnd=body["enabled"].toBool();QString error;if(!store_.save(accounts_,next,&error))return ApiReply::error(500,"save_failed",error);prefs_=next;engine_.setDnd(next.dnd);emit resourceChanged("dnd");event("dnd.changed",{{"enabled",next.dnd}});return {200,success(),{},{"application/json; charset=utf-8"}};}
    if(collection=="preferences"&&parts.size()==3){if(method=="GET"){auto o=prefs_.toJson();o.remove("remoteControl");return {200,{{"preferences",o}},{},{"application/json; charset=utf-8"}};}if(method=="PATCH")return preferences(body);}
    if(collection=="devices"&&method=="GET"&&parts.size()==3)return {200,{{"input",QJsonArray::fromStringList(engine_.soundDevices(true,false))},{"output",QJsonArray::fromStringList(engine_.soundDevices(false,true))},{"video",QJsonArray::fromStringList(engine_.videoDevices())}},{},{"application/json; charset=utf-8"}};
    if(collection=="codecs"&&method=="GET"&&parts.size()==3){QJsonObject o;for(const auto video:{false,true}){QJsonArray codecs;for(const auto &c:engine_.availableCodecs(video))codecs.append(QJsonObject{{"id",c.id},{"mime",c.mime},{"clockRate",c.clockRate},{"channels",c.channels},{"enabled",c.enabled},{"available",c.available},{"fmtp",c.fmtp}});o[video?"video":"audio"]=codecs;}return {200,o,{},{"application/json; charset=utf-8"}};}
    if(collection=="contacts"&&parts.size()==3){if(method=="GET")return {200,{{"contacts",objects(store_.contacts())}},{},{"application/json; charset=utf-8"}};
        if(method=="PUT"){if(!body["contacts"].isArray()||body["contacts"].toArray().size()>1000)return ApiReply::error(422,"invalid_contacts","Send a contacts array with at most 1000 entries.");QVector<QJsonObject> contacts;for(const auto &v:body["contacts"].toArray()){const auto o=v.toObject();if(!validText(o["name"].toString(),256)||!validText(o["uri"].toString(),1024))return ApiReply::error(422,"invalid_contact","Each contact needs name and uri.");contacts.append({{"name",o["name"]},{"uri",o["uri"]}});}if(!store_.saveContacts(contacts))return ApiReply::error(500,"save_failed","Could not save contacts.");emit resourceChanged("contacts");event("contacts.changed",{{"count",contacts.size()}});return {200,success(),{},{"application/json; charset=utf-8"}};}}
    if(collection=="history"&&method=="GET"&&parts.size()==3)return {200,{{"history",objects(store_.history())}},{},{"application/json; charset=utf-8"}};
    if(collection=="messages"){
        if(method=="GET"&&parts.size()==3){QJsonArray messages;for(const auto &m:chat_.messages())if((!query.hasQueryItem("accountId")||m.accountId==query.queryItemValue("accountId",QUrl::FullyDecoded))&&(!query.hasQueryItem("peer")||m.peer==query.queryItemValue("peer",QUrl::FullyDecoded)))messages.append(m.toJson());return {200,{{"messages",messages}},{},{"application/json; charset=utf-8"}};}
        if(method=="POST"&&parts.size()==3){const auto account=accountId(body["accountId"].toString());if(account.isEmpty())return ApiReply::error(422,"invalid_account","Choose an existing accountId.");const auto message=engine_.sendMessage(account,body["target"].toString(),body["text"].toString());if(message.isEmpty())return engineResult(false,engine_);return accepted({{"ok",true},{"messageId",message}});}
        if(method=="POST"&&parts.size()==5&&action=="read"){QString error;if(!chat_.markMessageRead(id,&error))return ApiReply::error(409,"mark_read_failed",error);event("message.read",{{"id",id}});return {200,success(),{},{"application/json; charset=utf-8"}};}
    }
    if(collection=="documents"){
        if(method=="POST"&&parts.size()==3)return uploadDocument(body);
        if(method=="GET"&&parts.size()==3){QJsonArray docs;for(const auto &d:documents_)docs.append(d.info);return {200,{{"documents",docs}},{},{"application/json; charset=utf-8"}};}
        if(!QRegularExpression("^[0-9a-f-]{36}$").match(id).hasMatch())return ApiReply::error(404,"document_not_found","Unknown document ID.");
        if(!documents_.contains(id)){const auto directory=store_.directory()+"/api-documents/"+id;QFile metadata(directory+"/document.json");if(metadata.open(QIODevice::ReadOnly)){const auto info=QJsonDocument::fromJson(metadata.readAll()).object();if(info["id"]==id){Document d;d.info=info;d.output=directory+"/a4.tif";d.control=std::make_shared<FaxConversionControl>();documents_[id]=d;}}}
        if(!documents_.contains(id))return ApiReply::error(404,"document_not_found","Unknown document ID.");
        const auto d=documents_[id];
        if(method=="GET"&&parts.size()==4)return {200,{{"document",d.info}},{},{"application/json; charset=utf-8"}};
        if(method=="DELETE"&&parts.size()==4){if(d.info["state"]=="converting")return ApiReply::error(409,"converting","Wait for conversion to finish before deleting this document.");QDir(QFileInfo(d.output).absolutePath()).removeRecursively();documents_.remove(id);return {200,success(),{},{"application/json; charset=utf-8"}};}
        if(method=="GET"&&parts.size()==5&&action=="file"){if(d.info["state"]!="ready")return ApiReply::error(409,"not_ready","Document conversion is not complete.");QFile file(d.output);if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024)return ApiReply::error(404,"file_unavailable","Document is missing or exceeds 64 MiB.");ApiReply r;r.body=file.readAll();r.contentType="image/tiff";return r;}
    }
    if(collection=="faxes"){
        if(method=="GET"&&parts.size()==3){QJsonArray faxes;for(const auto &f:engine_.faxes())faxes.append(faxJson(f));return {200,{{"faxes",faxes}},{},{"application/json; charset=utf-8"}};}
        if(method=="POST"&&parts.size()==3){const auto document=body["documentId"].toString();if(!documents_.contains(document)||documents_[document].info["state"]!="ready")return ApiReply::error(422,"document_not_ready","Upload and prepare an A4 document first.");const auto account=accountId(body["accountId"].toString());if(account.isEmpty()||!validText(body["target"].toString(),1024))return ApiReply::error(422,"invalid_destination","Choose accountId and target.");QSet<QString> before;for(const auto &fax:engine_.faxes())before.insert(fax.id);if(!engine_.sendFax(account,body["target"].toString(),documents_[document].output,body["protocol"].toString("t38")))return engineResult(false,engine_);for(const auto &fax:engine_.faxes())if(!before.contains(fax.id)&&!fax.incoming)return accepted({{"ok",true},{"fax",faxJson(fax)}});return accepted(success());}
        FaxInfo found;for(const auto &f:engine_.faxes())if(f.id==id)found=f;if(found.id.isEmpty())return ApiReply::error(404,"fax_not_found","Unknown fax ID.");
        if(method=="GET"&&parts.size()==4)return {200,{{"fax",faxJson(found)}},{},{"application/json; charset=utf-8"}};
        if(method=="DELETE"&&parts.size()==4){if(found.callId.isEmpty()||callId(found.callId).isEmpty())return ApiReply::error(409,"fax_not_active","This fax has no active call.");return engineResult(engine_.hangup(found.callId),engine_);}
        if(method=="GET"&&parts.size()==5&&action=="file"){if(found.state!="completed")return ApiReply::error(409,"fax_incomplete","Only completed fax files can be downloaded.");QFile file(found.path);if(!file.open(QIODevice::ReadOnly)||file.size()>64*1024*1024)return ApiReply::error(404,"file_unavailable","Fax file is missing or exceeds 64 MiB.");ApiReply r;r.body=file.readAll();r.contentType="image/tiff";return r;}
    }
    return ApiReply::error(404,"not_found","Unknown endpoint or unsupported method. See docs/API.md.");
}
ApiReply RemoteControlServer::startAttended(const QString &id,const QString &target){
    CallInfo original;for(const auto &call:engine_.calls())if(call.id==id)original=call;
    if(original.id.isEmpty()||!validText(target,1024))return ApiReply::error(422,"invalid_transfer","Choose an active call and destination.");
    if(original.rawState!=LinphoneCallStreamsRunning&&original.rawState!=LinphoneCallPaused)return ApiReply::error(409,"not_connected","The original call must be connected or held.");
    for(const auto &t:transfers_)if(t.original==id&&(t.state=="holding"||t.state=="consulting"))return ApiReply::error(409,"transfer_pending","This call already has a pending attended transfer.");
    if(transfers_.size()>=64){for(auto it=transfers_.begin();it!=transfers_.end();)if(it->state!="holding"&&it->state!="consulting")it=transfers_.erase(it);else ++it;}
    if(transfers_.size()>=64)return ApiReply::error(429,"transfer_limit","Too many pending attended transfers.");
    Transfer transfer{uuid(),id,target,{},"holding",{}};transfers_[transfer.id]=transfer;
    if(original.rawState==LinphoneCallPaused)QTimer::singleShot(0,this,[this,original]{advanceTransfers(original);});
    else if(!engine_.hold(id,true)){transfers_.remove(transfer.id);return engineResult(false,engine_);}
    QTimer::singleShot(45000,this,[this,id=transfer.id]{if(!transfers_.contains(id))return;auto &t=transfers_[id];if(t.state!="holding"&&t.state!="consulting")return;t.state="failed";t.error="Consultation timed out.";if(!t.consultation.isEmpty())engine_.hangup(t.consultation);if(!callId(t.original).isEmpty())engine_.hold(t.original,false);event("transfer.changed",{{"id",id},{"state",t.state},{"error",t.error}});});
    return accepted({{"ok",true},{"transferId",transfer.id},{"state","holding"}});
}
void RemoteControlServer::advanceTransfers(const CallInfo &call,bool finished){
    const auto ids=transfers_.keys();for(const auto &id:ids){if(!transfers_.contains(id))continue;auto &t=transfers_[id];
        if(t.state!="holding"&&t.state!="consulting")continue;
        const auto publish=[&]{event("transfer.changed",{{"id",t.id},{"callId",t.original},{"otherCallId",t.consultation},{"state",t.state},{"error",t.error}});};
        if(finished&&(call.id==t.original||call.id==t.consultation)){
            t.state="failed";t.error="A transfer leg ended before the consultation connected.";publish();
            if(call.id==t.original&&!t.consultation.isEmpty())engine_.hangup(t.consultation);
            else if(!callId(t.original).isEmpty())engine_.hold(t.original,false);
            continue;
        }
        if(t.state=="holding"&&call.id==t.original&&call.rawState==LinphoneCallPaused){
            QSet<QString> before;for(const auto &c:engine_.calls())before.insert(c.id);
            if(!engine_.dial(call.accountId,t.target,false)){t.state="failed";t.error=engine_.lastError();publish();engine_.hold(t.original,false);continue;}
            for(const auto &c:engine_.calls())if(!before.contains(c.id)&&!c.incoming)t.consultation=c.id;
            if(t.consultation.isEmpty()){t.state="failed";t.error="Consultation call did not start.";engine_.hold(t.original,false);}else t.state="consulting";publish();
        }else if(t.state=="consulting"&&call.id==t.consultation&&call.rawState==LinphoneCallStreamsRunning){
            // REFER completion is asynchronous; do not report delivery as complete.
            t.state="transfer-requested";if(!engine_.attendedTransfer(t.original,t.consultation)){t.state="failed";t.error=engine_.lastError();engine_.hold(t.original,false);}publish();
        }
    }
}
ApiReply RemoteControlServer::yealink(const QUrl &url){
    const QUrlQuery query(url);
    if(query.queryItemValue("phonecfg",QUrl::FullyDecoded)=="get")return {200,status(),{},{"application/json; charset=utf-8"}};
    const auto value=query.queryItemValue("key",QUrl::FullyDecoded);
    if(value.isEmpty())return ApiReply::error(422,"key_required","Use /servlet?key=number=1002 or key=ANSWER.");
    const bool withTarget=value.startsWith("number=",Qt::CaseInsensitive)||value.startsWith("BTrans=",Qt::CaseInsensitive)||value.startsWith("ATrans=",Qt::CaseInsensitive);
    const auto commands=withTarget?QStringList{value}:value.split(';');if(commands.size()>16)return ApiReply::error(422,"too_many_commands","At most 16 Action URI commands are allowed.");
    QJsonArray results;
    for(const auto &raw:commands){
        const auto upper=raw.toUpper();QString requested=query.queryItemValue("callId",QUrl::FullyDecoded);if(requested.isEmpty())requested=query.queryItemValue("call_id",QUrl::FullyDecoded);
        QString command=upper;const int colon=raw.indexOf(':');if(colon>0&&QStringList{"ASW","ANSWER","CANCEL","CALLEND","HOLD","UNHOLD"}.contains(upper.left(colon))){command=upper.left(colon);requested=raw.mid(colon+1);}
        ApiReply result;
        if(upper.startsWith("NUMBER="))result=dial({{"accountId",query.queryItemValue("outgoing_uri",QUrl::FullyDecoded)},{"target",raw.mid(7)},{"video",query.queryItemValue("video",QUrl::FullyDecoded)=="1"}});
        else if(upper.startsWith("BTRANS="))result=callAction(callId(requested),"transfer",{{"target",raw.mid(7)}});
        else if(upper.startsWith("ATRANS=")){
            const auto argument=raw.mid(7);const int at=argument.indexOf('@');const auto first=at>0?argument.left(at):QString(),second=at>0?argument.mid(at+1):QString();
            if(!first.isEmpty()&&!callId(first).isEmpty()&&!callId(second).isEmpty())result=callAction(first,"attended-transfer",{{"otherCallId",second}});
            else result=startAttended(callId(requested),argument);
        }else if(command=="ANSWER"||command=="ASW")result=callAction(callId(requested,true),"answer",{{"video",query.queryItemValue("video",QUrl::FullyDecoded)=="1"}});
        else if(command=="REJECT")result=callAction(callId(requested,true),"reject",{});
        else if(QStringList{"CALLEND","CANCEL","ONHOOK"}.contains(command))result=callAction(callId(requested),"hangup",{});
        else if(command=="HOLD"||command=="F_HOLD"||command=="UNHOLD"){
            const auto selected=callId(requested);bool held=false;for(const auto &c:engine_.calls())if(c.id==selected)held=c.rawState==LinphoneCallPaused;
            result=callAction(selected,command=="UNHOLD"||held?"resume":"hold",{});
        }else if(command=="MUTE"){
            const auto selected=callId(requested);bool muted=false;for(const auto &c:engine_.calls())if(c.id==selected)muted=c.muted;result=callAction(selected,muted?"unmute":"mute",{});
        }else if(command=="DNDON"||command=="DNDOFF")result=dispatch("POST",QUrl("/api/v1/dnd"),{{"enabled",command=="DNDON"}});
        else if(command=="F_CONFERENCE"){
            auto selected=callId(requested);auto other=query.queryItemValue("otherCallId",QUrl::FullyDecoded);if(other.isEmpty()){for(const auto &c:engine_.calls())if(c.id!=selected){if(!other.isEmpty()){other.clear();break;}other=c.id;}}
            result=callAction(selected,"conference",{{"otherCallId",other}});
        }else if(command=="F_TRANSFER"){
            const auto target=query.queryItemValue("target",QUrl::FullyDecoded);if(target.isEmpty())result=ApiReply::error(422,"target_required","F_TRANSFER needs target=<extension>; BTrans=<extension> also works.");else result=callAction(callId(requested),"transfer",{{"target",target}});
        }else if(command=="RD"){
            const auto history=store_.history();if(history.isEmpty())result=ApiReply::error(409,"empty_history","There is no recent number to redial.");else result=dial({{"accountId",query.queryItemValue("outgoing_uri",QUrl::FullyDecoded)},{"target",history.first()["remote"]}});
        }else if((command.size()==1&&QString("0123456789*#").contains(command))||command=="POUND")result=callAction(callId(requested),"dtmf",{{"digit",command=="POUND"?"#":command}});
        else if(command.startsWith("L")&&command.mid(1).toInt()>0){const int key=command.mid(1).toInt();result=dispatch("POST",QUrl("/api/v1/expansion/"+QString::number(key)+"/press"),{});}
        else result=ApiReply::error(422,"unsupported_key","Unsupported Action URI key. See the compatibility table in docs/API.md.");
        if(result.status>=400){result.data["command"]=raw;result.data["completedCommands"]=results;return result;}
        results.append(result.data);
    }
    return {200,{{"ok",true},{"results",results}},{},{"application/json; charset=utf-8"}};
}
}
