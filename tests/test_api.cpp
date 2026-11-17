#include "remotecontrol.h"
#include "remotecontrolwidget.h"
#include "chatwidget.h"
#include "mainwindow.h"
#include "deskphone.h"
#include "calldialog.h"
#include "faxdocument.h"
#include "isolatedsipengine.h"
#include "minipbx.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPdfWriter>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QFileInfo>
#include <QPushButton>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QUdpSocket>
#include <QtTest>
using namespace stel;
namespace {
struct Http {int status=0;QJsonObject json;QByteArray bytes,type;};
Http request(const QString &base,const QString &path,const QByteArray &method="GET",const QJsonObject &body={},const QString &token={},bool basic=false,const QMap<QByteArray,QByteArray> &headers={},const QString &ca={}){
    QNetworkAccessManager manager;QNetworkRequest req{QUrl(base+path)};
    if(!token.isEmpty())req.setRawHeader("Authorization",basic?"Basic "+QByteArray("stel:"+token.toLatin1()).toBase64():"Bearer "+token.toLatin1());
    if(!ca.isEmpty()){QFile f(ca);if(f.open(QIODevice::ReadOnly)){auto ssl=req.sslConfiguration();ssl.setCaCertificates(QSslCertificate::fromData(f.readAll()));req.setSslConfiguration(ssl);}}
    for(auto i=headers.begin();i!=headers.end();++i)req.setRawHeader(i.key(),i.value());
    QByteArray bytes;if(method!="GET"){req.setHeader(QNetworkRequest::ContentTypeHeader,"application/json");bytes=QJsonDocument(body).toJson(QJsonDocument::Compact);}
    auto *reply=manager.sendCustomRequest(req,method,bytes);QEventLoop loop;QTimer timer;timer.setSingleShot(true);QObject::connect(reply,&QNetworkReply::finished,&loop,&QEventLoop::quit);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.start(12000);loop.exec();
    Http result;result.status=reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();result.bytes=reply->readAll();result.json=QJsonDocument::fromJson(result.bytes).object();result.type=reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();reply->abort();return result;
}
quint16 freePort(){QTcpServer probe;if(!probe.listen(QHostAddress::LocalHost,0))return 0;return probe.serverPort();}
Account direct(const QString &user){Account a;a.id=a.label=a.username=user;a.domain="localhost";a.transport="udp";a.registerEnabled=false;a.ice=false;return a;}
QString uri(SipEngine &engine,const QString &user){auto *t=linphone_core_get_transports_used(engine.native());const auto port=linphone_transports_get_udp_port(t);linphone_transports_unref(t);return "sip:"+user+"@127.0.0.1:"+QString::number(port);}
bool running(const SipEngine &engine){for(const auto &c:engine.calls())if(c.rawState==LinphoneCallStreamsRunning)return true;return false;}
struct Device {
    QTemporaryDir directory;Store store{directory.path()};Preferences prefs;QVector<Account> accounts;
    IsolatedSipEngine engine;ChatWidget chat{engine,store};RemoteControlServer api{engine,store,accounts,prefs,chat};
    QString token=RemoteControlConfig::newToken();
    bool start(bool silence=false){prefs.inputDevice=silence?dummyMicSilence():dummyMicTone();prefs.outputDevice=dummySpeaker();prefs.videoDevice=dummyCamera();prefs.mediaAddress="127.0.0.1";
        if(!engine.start(prefs,directory.path(),true))return false;
        prefs.remoteControl.enabled=true;prefs.remoteControl.port=freePort();prefs.remoteControl.tokenHash=RemoteControlConfig::hashToken(token);return api.configure(prefs.remoteControl);}
    bool add(Account a){accounts.append(a);chat.setAccounts(accounts);return engine.addAccount(a);}
    Http get(const QString &path){return request(api.endpoint(),path,"GET",{},token);}
    Http post(const QString &path,const QJsonObject &body={}){return request(api.endpoint(),path,"POST",body,token);}
    QString call(const QString &target,bool video=false){return post("/api/v1/calls",{{"accountId",accounts.first().id},{"target",target},{"video",video}}).json["call"].toObject()["id"].toString();}
};
}
class ApiTests : public QObject {
    Q_OBJECT
private slots:
    void init(){IsolatedSipEngine::resetPortPools();}
    void authenticationAndHttp(){
        Device d;QVERIFY(!d.api.isListening());QVERIFY(d.start());QCOMPARE(d.get("/api/v1/status").status,200);
        QCOMPARE(request(d.api.endpoint(),"/api/v1/status").status,401);QCOMPARE(request(d.api.endpoint(),"/api/v1/status","GET",{},"wrong").status,401);
        QCOMPARE(request(d.api.endpoint(),"/api/v1/status","GET",{},d.token,true).status,200);
        QCOMPARE(request(d.api.endpoint(),"/api/v1/status","GET",{},d.token,false,{{"Origin","https://example.invalid"}}).status,403);
        QCOMPARE(d.post("/servlet?key=ANSWER").status,405);QCOMPARE(d.get("/api/v1/not-real").status,404);
        QCOMPARE(d.get("/servlet?key=Reset").status,422);QCOMPARE(d.get("/servlet?key=ANSWER&key=CALLEND").status,400);
        const auto raw=[&](const QByteArray &headers){QTcpSocket socket;QEventLoop loop;QByteArray bytes;connect(&socket,&QTcpSocket::connected,&loop,[&]{socket.write("POST /api/v1/dnd HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer "+d.token.toLatin1()+"\r\n"+headers+"\r\n");});connect(&socket,&QTcpSocket::readyRead,&loop,[&]{bytes+=socket.readAll();});connect(&socket,&QTcpSocket::disconnected,&loop,&QEventLoop::quit);QTimer::singleShot(3000,&loop,&QEventLoop::quit);socket.connectToHost(QHostAddress::LocalHost,d.api.serverPort());loop.exec();return bytes.split(' ').value(1).toInt();};
        QCOMPARE(raw("Content-Length: 2\r\nContent-Length: 2\r\n"),400);QCOMPARE(raw("Content-Length: -1\r\n"),413);QCOMPARE(raw("Content-Length: 17000000\r\n"),413);QCOMPARE(raw("Transfer-Encoding: chunked\r\n"),400);
        QVERIFY(!d.prefs.dnd);const auto old=d.token;d.token=RemoteControlConfig::newToken();auto config=d.prefs.remoteControl;config.tokenHash=RemoteControlConfig::hashToken(d.token);QVERIFY(d.api.configure(config));QCOMPARE(request(d.api.endpoint(),"/api/v1/status","GET",{},old).status,401);QCOMPARE(d.get("/api/v1/status").status,200);
        QString error;config.tokenHash="bad";QVERIFY(!d.api.configure(config,&error));QVERIFY(d.api.isListening());
        config=d.prefs.remoteControl;config.enabled=false;QVERIFY(d.api.configure(config));QVERIFY(!d.api.isListening());
        RemoteControlConfig allow;QVERIFY(allow.permits("127.0.0.1"));QVERIFY(allow.permits("::1"));QVERIFY(allow.permits("::ffff:127.0.0.1"));QVERIFY(!allow.permits("192.168.17.12"));allow.allowedPeers=QStringList{"192.168.17.0/24"};QVERIFY(allow.permits("192.168.17.12"));QVERIFY(!allow.permits("192.168.18.12"));
    }
    void accountsAndConfiguration(){
        Device d;QVERIFY(d.start());auto account=direct("alice").toJson();account["password"]="api-test-password";const auto created=d.post("/api/v1/accounts",account);QCOMPARE(created.status,201);QCOMPARE(d.accounts.size(),1);
        const auto result=d.get("/api/v1/accounts");QCOMPARE(result.status,200);QVERIFY(!result.bytes.contains("api-test-password"));QVERIFY(!result.bytes.contains(d.prefs.remoteControl.tokenHash.toLatin1()));
        auto update=request(d.api.endpoint(),"/api/v1/accounts/alice","PATCH",{{"callerIdName",QString::fromUtf8("مهر داد")}},d.token);QVERIFY2(update.status==200,update.bytes.constData());QCOMPARE(d.accounts[0].password,QString("api-test-password"));
        auto duplicate=account;duplicate["id"]="duplicate";QCOMPARE(d.post("/api/v1/accounts",duplicate).status,409);QCOMPARE(d.accounts.size(),1);QVERIFY(d.get("/api/v1/events").bytes.contains("engine.error"));
        QCOMPARE(d.post("/api/v1/accounts/alice/unregister").status,202);QVERIFY(!d.accounts[0].registerEnabled);
        d.api.setConfigurationGuard([]{return false;});QCOMPARE(request(d.api.endpoint(),"/api/v1/preferences","PATCH",{{"dnd",true}},d.token).status,409);d.api.setConfigurationGuard([]{return true;});
        QCOMPARE(d.post("/api/v1/dnd",{{"enabled",true}}).status,200);QVERIFY(d.prefs.dnd);QCOMPARE(d.get("/servlet?key=DNDOff").status,200);QVERIFY(!d.prefs.dnd);
        QCOMPARE(request(d.api.endpoint(),"/api/v1/preferences","PATCH",{{"mediaAddress","127.0.0.1"}},d.token).status,200);
        QCOMPARE(request(d.api.endpoint(),"/api/v1/preferences","PATCH",{{"remoteControl",QJsonObject{}}},d.token).status,422);
        QCOMPARE(d.get("/api/v1/devices").status,200);QVERIFY(!d.get("/api/v1/codecs").json["audio"].toArray().isEmpty());
        QCOMPARE(request(d.api.endpoint(),"/api/v1/contacts","PUT",{{"contacts",QJsonArray{QJsonObject{{"name",QString::fromUtf8("پشتیبانی")},{"uri","1002"}}}}},d.token).status,200);QCOMPARE(d.store.contacts().size(),1);
        auto key=QJsonObject{{"type","speed-dial"},{"label","Support"},{"accountId","alice"},{"target","1002"}};QCOMPARE(request(d.api.endpoint(),"/api/v1/expansion/1","PUT",key,d.token).status,200);QCOMPARE(d.prefs.expansionKeys[0].label,QString("Support"));QCOMPARE(d.get("/api/v1/blf").json["keys"].toArray().size(),12);
        QVector<Account> accounts;Preferences saved;QVERIFY(d.store.load(accounts,saved));QCOMPARE(accounts[0].password,QString("api-test-password"));QCOMPARE(saved.remoteControl.tokenHash,d.prefs.remoteControl.tokenHash);
        {
            // Windows QFile opens without FILE_SHARE_DELETE. Release this read
            // handle before DELETE saves settings through QSaveFile's rename.
            QFile file(d.directory.path()+"/settings.json");QVERIFY(file.open(QIODevice::ReadOnly));const auto bytes=file.readAll();QVERIFY(!bytes.contains(d.token.toLatin1()));QVERIFY(!bytes.contains("api-test-password"));
        }
        const auto deleted=request(d.api.endpoint(),"/api/v1/accounts/alice","DELETE",{},d.token);
        QVERIFY2(deleted.status==200,qPrintable(QString("DELETE returned HTTP %1: %2").arg(deleted.status).arg(QString::fromUtf8(deleted.bytes))));QVERIFY(d.accounts.isEmpty());
        QVERIFY(d.store.load(accounts,saved));QVERIFY(accounts.isEmpty());
    }
    void callsAndYealink_data(){QTest::addColumn<bool>("video");QTest::newRow("voice")<<false;QTest::newRow("video")<<true;}
    void callsAndYealink(){
        QFETCH(bool,video);Device d;MiniPbx pbx;QVERIFY(d.start());for(const auto &name:{"1001","1002"}){auto a=direct(name);a.registerEnabled=true;a.password="api-loopback";a.server=pbx.server();QVERIFY(d.add(a));}
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("1001")&&pbx.registered("1002"),6000);
        const auto called=request(d.api.endpoint(),"/servlet?key=number=1002&outgoing_uri=1001%40localhost&video="+QString::number(video),"GET",{},d.token,true);QVERIFY2(called.status==200,called.bytes.constData());
        QTRY_COMPARE_WITH_TIMEOUT(d.engine.calls().size(),2,6000);QString in,out;for(const auto &c:d.engine.calls())(c.incoming?in:out)=c.id;
        auto answered=d.get("/servlet?key=ASW:"+in+"&video="+QString::number(video));QCOMPARE(answered.status,200);QTRY_VERIFY_WITH_TIMEOUT([&]{int count=0;for(const auto &call:d.engine.calls())count+=call.rawState==LinphoneCallStreamsRunning;return count==2;}(),10000);
        int digit=0;auto *cb=linphone_factory_create_core_cbs(linphone_factory_get());linphone_core_cbs_set_user_data(cb,&digit);linphone_core_cbs_set_dtmf_received(cb,[](LinphoneCore *lc,LinphoneCall *,int v){*static_cast<int*>(linphone_core_cbs_get_user_data(linphone_core_get_current_callbacks(lc)))=v;});linphone_core_add_callbacks(d.engine.native(),cb);
        QCOMPARE(d.post("/api/v1/calls/"+out+"/dtmf",{{"digit","5"}}).status,200);QTRY_COMPARE_WITH_TIMEOUT(digit,int('5'),5000);linphone_core_remove_callbacks(d.engine.native(),cb);linphone_core_cbs_unref(cb);
        QCOMPARE(d.post("/api/v1/calls/"+in+"/reject").status,409);QCOMPARE(d.get("/servlet?key=MUTE&callId="+in).status,200);QCOMPARE(d.post("/api/v1/calls/"+in+"/unmute").status,200);
        QCOMPARE(d.get("/servlet?key=HOLD:"+in).status,200);QTRY_VERIFY([&]{for(const auto &c:d.engine.calls())if(c.id==in)return c.rawState==LinphoneCallPaused;return false;}());QCOMPARE(d.get("/servlet?key=UNHOLD:"+in).status,200);QTRY_VERIFY([&]{int count=0;for(const auto &call:d.engine.calls())count+=call.rawState==LinphoneCallStreamsRunning;return count==2;}());
        QCOMPARE(d.post("/api/v1/calls/"+in+"/record").status,200);QTest::qWait(250);QCOMPARE(d.post("/api/v1/calls/"+in+"/stop-recording").status,200);
        const auto messages=d.post("/api/v1/messages",{{"accountId","1001"},{"target","1002"},{"text",QString::fromUtf8("سلام، تست API")}});QCOMPARE(messages.status,202);QTRY_COMPARE_WITH_TIMEOUT(d.chat.messages().size(),2,6000);QString received;for(const auto &m:d.chat.messages())if(m.incoming)received=m.id;QVERIFY(!received.isEmpty());QCOMPARE(d.post("/api/v1/messages/"+received+"/read").status,200);QCOMPARE(d.chat.unreadCount(),0);
        auto key=QJsonObject{{"type","blf"},{"label","Support"},{"accountId","1001"},{"target","1002"}};QCOMPARE(request(d.api.endpoint(),"/api/v1/expansion/1","PUT",key,d.token).status,200);QTRY_COMPARE_WITH_TIMEOUT(d.engine.blfInfo(0).state,BlfState::Idle,6000);
        pbx.notifyBlf("1002","<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\"1\" state=\"full\" entity=\"sip:1002@localhost\"><dialog id=\"busy\"><state>confirmed</state></dialog></dialog-info>");QTRY_COMPARE(d.engine.blfInfo(0).state,BlfState::Busy);QCOMPARE(d.get("/api/v1/blf").json["keys"].toArray()[0].toObject()["colour"].toString(),QString("red"));
        const auto events=d.get("/api/v1/events?since=0");QCOMPARE(events.status,200);QVERIFY(events.json["events"].toArray().size()>5);QCOMPARE(d.get("/api/v1/events?since="+events.json["lastEventId"].toString()).status,200);
        QCOMPARE(d.get("/servlet?key=CALLEND&callId="+out).status,200);QTRY_VERIFY_WITH_TIMEOUT(d.engine.calls().isEmpty(),6000);QCOMPARE(d.post("/api/v1/calls/"+in+"/hangup").status,404);
    }
    void transfersAndConference_data(){QTest::addColumn<QString>("mode");for(const auto &mode:{"reject","forward","blind","attended","consult-target","conference"})QTest::newRow(mode)<<QString(mode);}
    void transfersAndConference(){
        QFETCH(QString,mode);Device d;QVERIFY(d.start());QVERIFY(d.add(direct("bob")));IsolatedSipEngine a,c;QVERIFY(a.start(d.prefs,d.directory.path()+"/a",true));QVERIFY(c.start(d.prefs,d.directory.path()+"/c",true));QVERIFY(a.addAccount(direct("alice")));QVERIFY(c.addAccount(direct("carol")));
        connect(&c,&SipEngine::callChanged,&c,[&](const CallInfo &call){if(call.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&c,[&,id=call.id]{c.answer(id,false);});});
        QVERIFY(a.dial("alice",uri(d.engine,"bob")));QTRY_VERIFY(!d.engine.calls().isEmpty());const auto id=d.engine.calls().first().id;
        if(mode=="reject"){QCOMPARE(d.post("/api/v1/calls/"+id+"/reject").status,200);QTRY_VERIFY(d.engine.calls().isEmpty());return;}
        if(mode=="forward")QCOMPARE(d.post("/api/v1/calls/"+id+"/forward",{{"target",uri(c,"carol")}}).status,200);
        else {
            QCOMPARE(d.post("/api/v1/calls/"+id+"/answer").status,200);QTRY_VERIFY(running(a)&&running(d.engine));
            if(mode=="blind")QCOMPARE(d.get("/servlet?key=BTrans="+QString::fromLatin1(QUrl::toPercentEncoding(uri(c,"carol")))+"&callId="+id).status,200);
            else if(mode=="consult-target")QCOMPARE(d.get("/servlet?key=ATrans="+QString::fromLatin1(QUrl::toPercentEncoding(uri(c,"carol")))+"&callId="+id).status,200);
            else {
                QCOMPARE(d.post("/api/v1/calls/"+id+"/hold").status,200);QTRY_VERIFY([&]{for(const auto &call:d.engine.calls())if(call.id==id)return call.rawState==LinphoneCallPaused;return false;}());
                const auto consultation=d.call(uri(c,"carol"));QVERIFY(!consultation.isEmpty());QTRY_VERIFY(running(c)&&running(d.engine));
                if(mode=="attended")QCOMPARE(d.get("/servlet?key=ATrans="+id+"%40"+consultation).status,200);
                else {QCOMPARE(d.post("/api/v1/calls/"+id+"/conference",{{"otherCallId",consultation}}).status,200);QTRY_VERIFY([&]{int count=0;for(const auto &call:d.engine.calls())count+=call.inConference;return count==2;}());for(const auto &call:d.engine.calls())d.engine.hangup(call.id);QTRY_VERIFY(d.engine.calls().isEmpty());return;}
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(running(a)&&running(c)&&d.engine.calls().isEmpty(),12000);for(const auto &call:a.calls())a.hangup(call.id);QTRY_VERIFY(c.calls().isEmpty());
    }
    void documentsAndFax_data(){QTest::addColumn<QString>("protocol");QTest::newRow("t38")<<QString("t38");QTest::newRow("g711")<<QString("g711");}
    void documentsAndFax(){
        QFETCH(QString,protocol);Device d;MiniPbx pbx;QVERIFY(d.start(true));for(const auto &name:{"1001","1002"}){auto a=direct(name);a.registerEnabled=true;a.password="api-fax";a.server=pbx.server();QVERIFY(d.add(a));}QTRY_VERIFY(pbx.registered("1001")&&pbx.registered("1002"));
        const auto uploaded=d.post("/api/v1/documents",{{"text",QString::fromUtf8("سلام\nStel API fax")}});QCOMPARE(uploaded.status,202);const auto document=uploaded.json["document"].toObject()["id"].toString();QVERIFY(!document.isEmpty());QTRY_COMPARE_WITH_TIMEOUT(d.get("/api/v1/documents/"+document).json["document"].toObject()["state"].toString(),QString("ready"),15000);
        const auto data=d.get("/api/v1/documents/"+document+"/file");QCOMPARE(data.status,200);QCOMPARE(data.type,QByteArray("image/tiff"));
        const auto sent=d.post("/api/v1/faxes",{{"accountId","1001"},{"target","1002"},{"documentId",document},{"protocol",protocol}});QCOMPARE(sent.status,202);QTRY_VERIFY([&]{for(const auto &call:d.engine.calls())if(call.incoming)return true;return false;}());QString incomingId;for(const auto &call:d.engine.calls())if(call.incoming)incomingId=call.id;
        QCOMPARE(d.post("/api/v1/calls/"+incomingId+"/fax",{{"protocol",protocol}}).status,200);
        QTRY_VERIFY_WITH_TIMEOUT([&]{int done=0;for(const auto &f:d.engine.faxes())done+=f.state=="completed"&&f.pages==1;return done==2;}(),60000);
        QString received;for(const auto &f:d.engine.faxes())if(f.incoming)received=f.id;const auto downloaded=d.get("/api/v1/faxes/"+received+"/file");QCOMPARE(downloaded.status,200);QVERIFY(!downloaded.bytes.isEmpty());QVERIFY(!d.get("/api/v1/faxes").bytes.contains(d.directory.path().toUtf8()));QTRY_VERIFY(d.engine.calls().isEmpty());
        QCOMPARE(request(d.api.endpoint(),"/api/v1/documents/"+document,"DELETE",{},d.token).status,200);
    }
    void uploadedFiles_data(){QTest::addColumn<QString>("extension");QTest::newRow("txt")<<QString("txt");QTest::newRow("pdf")<<QString("pdf");}
    void uploadedFiles(){
        QFETCH(QString,extension);Device d;QVERIFY(d.start());const auto path=d.directory.path()+"/input."+extension;
        if(extension=="txt"){QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QString::fromUtf8("سلام، تست فایل متنی API").toUtf8());}
        else {initializeFaxDocumentFonts();QPdfWriter writer(path);writer.setPageSize(QPageSize(QPageSize::Letter));QPainter painter(&writer);painter.drawText(QRect(100,100,3000,800),QString::fromUtf8("Stel API — فایل PDF"));painter.end();}
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto uploaded=d.post("/api/v1/documents",{{"filename","input."+extension},{"base64",QString::fromLatin1(file.readAll().toBase64())}});QCOMPARE(uploaded.status,202);const auto id=uploaded.json["document"].toObject()["id"].toString();QVERIFY(!id.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(d.get("/api/v1/documents/"+id).json["document"].toObject()["state"].toString(),QString("ready"),15000);QCOMPARE(d.get("/api/v1/documents/"+id).json["document"].toObject()["pages"].toInt(),1);
        d.api.stop();RemoteControlServer restarted(d.engine,d.store,d.accounts,d.prefs,d.chat);QVERIFY(restarted.configure(d.prefs.remoteControl));QCOMPARE(request(restarted.endpoint(),"/api/v1/documents","GET",{},d.token).json["documents"].toArray().size(),1);QCOMPARE(request(restarted.endpoint(),"/api/v1/documents/"+id+"/file","GET",{},d.token).status,200);
        QCOMPARE(request(restarted.endpoint(),"/api/v1/documents","POST",{{"filename","bad.txt"},{"base64","!!!"}},d.token).status,422);
    }
    void https(){
        Device d;QVERIFY(d.start());const auto certificate=d.directory.path()+"/api.pem",key=d.directory.path()+"/api.key";
        QProcess openssl;openssl.start("openssl",{"req","-x509","-newkey","rsa:2048","-nodes","-days","1","-subj","/CN=localhost","-addext","subjectAltName=IP:127.0.0.1,DNS:localhost","-keyout",key,"-out",certificate});QVERIFY(openssl.waitForFinished(15000));QCOMPARE(openssl.exitCode(),0);
        auto config=d.prefs.remoteControl;config.certificate=certificate;config.privateKey=key;QString error;QVERIFY2(d.api.configure(config,&error),qPrintable(error));QVERIFY(d.api.endpoint().startsWith("https://"));QProcess client;QEventLoop loop;connect(&client,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),&loop,&QEventLoop::quit);
        const QString python=
#ifdef Q_OS_WIN
            "python";
#else
            "python3";
#endif
        client.start(python,{"-c","import ssl,urllib.request,sys,json; r=urllib.request.urlopen(urllib.request.Request(sys.argv[1],headers={'Authorization':'Bearer '+sys.argv[3]}),context=ssl.create_default_context(cafile=sys.argv[2]),timeout=5); print(r.status); assert json.load(r)['apiVersion']==1",d.api.endpoint()+"/api/v1/status",certificate,d.token});
        QTimer::singleShot(12000,&loop,&QEventLoop::quit);loop.exec();QVERIFY2(client.state()==QProcess::NotRunning,client.readAllStandardError().constData());QCOMPARE(client.exitCode(),0);QVERIFY(client.readAllStandardOutput().contains("200"));
    }
    void mainWindowIntegration(){
        QTemporaryDir directory;Store store(directory.path());Preferences prefs;prefs.inputDevice=dummyMicTone();prefs.outputDevice=dummySpeaker();prefs.remoteControl.enabled=true;prefs.remoteControl.port=freePort();const auto token=RemoteControlConfig::newToken();prefs.remoteControl.tokenHash=RemoteControlConfig::hashToken(token);QVERIFY(store.save({direct("bob")},prefs));
        MainWindow window(directory.path());QVERIFY(window.ready());window.show();auto *api=window.findChild<RemoteControlServer*>();QVERIFY(api&&api->isListening());auto *desk=window.findChild<DeskPhone*>();QVERIFY(desk);
        IsolatedSipEngine peer;QVERIFY(peer.start(prefs,directory.path()+"/peer",true));QVERIFY(peer.addAccount(direct("alice")));const auto dialed=request(api->endpoint(),"/api/v1/calls","POST",{{"accountId","bob"},{"target",uri(peer,"alice")}},token);QCOMPARE(dialed.status,202);QTRY_VERIFY(!peer.calls().isEmpty());for(auto *dialog:window.findChildren<CallDialog*>())QVERIFY(!dialog->isVisible());QVERIFY(desk->hangup->isEnabled());QCOMPARE(request(api->endpoint(),"/screencapture","GET",{},token).type,QByteArray("image/jpeg"));
        const auto id=dialed.json["call"].toObject()["id"].toString();QCOMPARE(request(api->endpoint(),"/api/v1/calls/"+id+"/hangup","POST",{},token).status,200);QTRY_VERIFY(peer.calls().isEmpty());QTRY_VERIFY_WITH_TIMEOUT(!request(api->endpoint(),"/api/v1/history","GET",{},token).json["history"].toArray().isEmpty(),5000);
        QProcess client;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("STEL_TOKEN",token);environment.insert("STEL_API",api->endpoint());client.setProcessEnvironment(environment);
        QEventLoop loop;connect(&client,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),&loop,&QEventLoop::quit);
#ifdef Q_OS_WIN
        const QString python="python";
#else
        const QString python="python3";
#endif
        client.start(python,{QFileInfo(QString::fromLocal8Bit(__FILE__)).absolutePath()+"/../scripts/stel-api.py","status"});QTimer::singleShot(12000,&loop,&QEventLoop::quit);loop.exec();QVERIFY(client.state()==QProcess::NotRunning);QVERIFY2(client.exitCode()==0,client.readAllStandardError().constData());QCOMPARE(QJsonDocument::fromJson(client.readAllStandardOutput()).object()["apiVersion"].toInt(),1);
        window.showSettings();auto *enabled=window.findChild<QCheckBox*>("apiEnabled");QVERIFY(enabled);QVERIFY(enabled->isChecked());QCOMPARE(request(api->endpoint(),"/api/v1/accounts/bob","PATCH",{{"label","changed"}},token).status,409);enabled->setChecked(false);window.findChild<QPushButton*>("apiApply")->click();QVERIFY(!api->isListening());
    }
};
QTEST_MAIN(ApiTests)
#include "test_api.moc"
