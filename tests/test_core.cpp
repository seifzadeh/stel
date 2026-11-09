#include "sipengine.h"
#include "secretstore.h"
#include "minipbx.h"
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QHostAddress>
#include <QUdpSocket>
#include <ortp/rtpsession.h>
#include <bctoolbox/port.h>
#include <mediastreamer2/msfactory.h>
#include <mediastreamer2/msvideo.h>
#include <QtTest>
#include <cmath>
#include <functional>
#include <memory>
#include <atomic>

using namespace stel;
namespace {
// Theora predates MS_VIDEO_DECODER_FIRST_IMAGE_DECODED. Count actual decoded
// remote YUV frames at an SDK display sink, without any display server/GPU.
std::atomic_int decodedVideoFrames{0};
MSFactory *videoReceiverFactory=nullptr;
void decodedVideoSink(MSFilter *f) {
    if(f->factory==videoReceiverFactory&&f->inputs[0])for(auto *m=ms_queue_get(f->inputs[0]);m;m=ms_queue_get(f->inputs[0])) {
        MSPicture picture{};if(ms_yuv_buf_init_from_mblk(&picture,m)==0&&picture.w>0&&picture.h>0&&picture.planes[0])++decodedVideoFrames;freemsg(m);
    }
    for(int n=0;n<2;++n)if(f->inputs[n])ms_queue_flush(f->inputs[n]);
}
MSFilterDesc decodedSinkDesc={MS_FILTER_PLUGIN_ID,"StelDecodedVideoSink","Decoded remote frame test sink",MS_FILTER_OTHER,nullptr,2,0,nullptr,nullptr,decodedVideoSink,nullptr,nullptr,nullptr,0};
bool waitFor(const std::function<bool()> &condition,int timeout=7000) {
    QElapsedTimer t;t.start();while(t.elapsed()<timeout){if(condition())return true;QTest::qWait(20);}return condition();
}
QString activeId(SipEngine &e) {for(const auto &i:e.calls())if(i.rawState!=LinphoneCallEnd&&i.rawState!=LinphoneCallReleased)return i.id;return {};}
bool streams(SipEngine &e) {for(const auto &i:e.calls())if(i.rawState==LinphoneCallStreamsRunning)return true;return false;}
QString uri(SipEngine &e,const QString &user) {
    auto *t=linphone_core_get_transports_used(e.native());const int port=linphone_transports_get_udp_port(t);linphone_transports_unref(t);
    return "sip:"+user+"@127.0.0.1:"+QString::number(port);
}
Account direct(const QString &user,const QString &enc="none") {
    Account a;a.id=user;a.label=user;a.username=user;a.domain="localhost";a.transport="udp";a.encryption=enc;a.ice=false;a.registerEnabled=false;return a;
}
void autoAnswer(SipEngine &e,bool video=false) {
    QObject::connect(&e,&SipEngine::callChanged,&e,[&e,video](const CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&e,[&e,id=i.id,video]{e.answer(id,video);});});
}
void writeTone(const QString &path) {
    QFile f(path);f.open(QIODevice::WriteOnly);QDataStream out(&f);out.setByteOrder(QDataStream::LittleEndian);
    constexpr int samples=8000*30;f.write("RIFF",4);out<<quint32(36+samples*2);f.write("WAVEfmt ",8);out<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(8000)<<quint32(16000)<<quint16(2)<<quint16(16);f.write("data",4);out<<quint32(samples*2);
    for(int n=0;n<samples;++n)out<<qint16(std::sin(n*2*3.141592653589793*440/8000)*5000);
}
double download(SipEngine &e,bool video=false) {
    for(const auto &i:e.calls()) {
        const auto *list=linphone_core_get_calls(e.native());
        for(auto *node=list;node;node=node->next) {
            auto *c=static_cast<LinphoneCall*>(node->data);
            if(linphone_call_get_state(c)!=LinphoneCallStreamsRunning)continue;
            auto *stats=linphone_call_get_stats(c,video?LinphoneStreamTypeVideo:LinphoneStreamTypeAudio);
            const double bw=linphone_call_stats_get_download_bandwidth(stats);linphone_call_stats_unref(stats);return bw;
        }
        Q_UNUSED(i);
    }return 0;
}
}
class CoreTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const auto resources=qEnvironmentVariable("STEL_RESOURCE_PREFIX");if(!resources.isEmpty())SipEngine::configureResources(resources);
        if(qEnvironmentVariableIsSet("STEL_VERBOSE_TESTS"))linphone_logging_service_set_log_level(linphone_logging_service_get(),LinphoneLogLevelMessage);
    }
    void persistenceWithoutSecrets() {
        QTemporaryDir tmp;Store store(tmp.path());auto a=direct("alice");a.password="never-write-password";a.keyPassword="never-write-key-password";a.turnPassword="never-write-turn-password";
        a.rememberPassword=false;Preferences p;p.caFile="/custom/ca";QVERIFY(store.save({a},p));
        QFile f(tmp.path()+"/settings.json");QVERIFY(f.open(QIODevice::ReadOnly));const auto bytes=f.readAll();QVERIFY(!bytes.contains("never-write"));
        QVector<Account> loaded;Preferences restored;QVERIFY(store.load(loaded,restored));QCOMPARE(loaded.size(),1);QCOMPARE(loaded[0].username,a.username);QVERIFY(loaded[0].password.isEmpty());QCOMPARE(restored.caFile,p.caFile);
        f.close();QVERIFY(f.open(QIODevice::WriteOnly));f.write("{broken");f.close();QString error;QVERIFY(!store.load(loaded,restored,&error));QVERIFY(!error.isEmpty());QCOMPARE(loaded.size(),1);
    }
    void accountCallerOptionsPersist(){
        auto a=direct("alice");QVERIFY(!a.autoAnswer);QVERIFY(a.callerIdName.isEmpty());a.autoAnswer=true;a.callerIdName=QString::fromUtf8("علی رضایی");
        auto restored=Account::fromJson(a.toJson());QVERIFY(restored.autoAnswer);QCOMPARE(restored.callerIdName,a.callerIdName);
        auto legacy=Account::fromJson({{"label","Old display name"}});QVERIFY(!legacy.autoAnswer);QCOMPARE(legacy.callerIdName,QString("Old display name"));
        a.callerIdName.clear();QCOMPARE(Account::fromJson(a.toJson()).callerIdName,QString());
        a.callerIdName="Name\r\nX-Fake: injected";QVERIFY(!a.validate().isEmpty());a.callerIdName=QString(257,'x');QVERIFY(!a.validate().isEmpty());
    }
    void callerIdNameOnSipWire_data(){
        QTest::addColumn<QString>("name");QTest::newRow("plain")<<QString("Support Team");QTest::newRow("unicode")<<QString::fromUtf8("تیم پشتیبانی");
        QTest::newRow("quotes-backslash")<<QString("Ops \"Desk\" \\ West");QTest::newRow("punctuation")<<QString("Sales, West; Desk");QTest::newRow("empty")<<QString();
    }
    void callerIdNameOnSipWire(){
        QFETCH(QString,name);QTemporaryDir tmp;QUdpSocket peer;QVERIFY(peer.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));QByteArray from;
        connect(&peer,&QUdpSocket::readyRead,this,[&]{while(peer.hasPendingDatagrams()){
            QByteArray packet(int(peer.pendingDatagramSize()),'\0');QHostAddress address;quint16 port;peer.readDatagram(packet.data(),packet.size(),&address,&port);if(!packet.startsWith("INVITE "))continue;
            QMap<QByteArray,QByteArray> h;for(auto line:packet.split('\n')){line=line.trimmed();int n=line.indexOf(':');if(n>0)h[line.left(n).toLower()]=line.mid(n+1).trimmed();}from=h["from"];
            QByteArray response="SIP/2.0 486 Busy Here\r\n";for(const auto &key:{QByteArray("via"),QByteArray("from"),QByteArray("to"),QByteArray("call-id"),QByteArray("cseq")})response+=key+": "+h[key]+(key=="to"?";tag=caller-id-test":"")+"\r\n";
            response+="Content-Length: 0\r\n\r\n";peer.writeDatagram(response,address,port);
        }});
        SipEngine e;QVERIFY(e.start({},tmp.path(),true));auto a=direct("alice");a.label="Local account label";a.callerIdName=name;QVERIFY(a.validate().isEmpty());QVERIFY(e.addAccount(a));
        QVERIFY(e.dial(a.id,"sip:bob@127.0.0.1:"+QString::number(peer.localPort())));QVERIFY(waitFor([&]{return !from.isEmpty();}));
        auto *parsed=linphone_factory_create_address(linphone_factory_get(),from.constData());QVERIFY2(parsed,from.constData());QCOMPARE(QString::fromUtf8(linphone_address_get_display_name(parsed)),name);QCOMPARE(QString::fromUtf8(linphone_address_get_username(parsed)),QString("alice"));linphone_address_unref(parsed);
        QVERIFY(!from.contains("Local account label"));if(name.contains('"'))QVERIFY(from.contains("\\\"Desk\\\""));
        QVERIFY(waitFor([&]{return e.calls().isEmpty();}));
    }
    void accountAutoAnswer_data(){
        QTest::addColumn<QString>("mode");QTest::addColumn<bool>("enabled");QTest::addColumn<bool>("video");
        QTest::newRow("RTP-enabled")<<QString("none")<<true<<false;QTest::newRow("SRTP-enabled")<<QString("srtp")<<true<<false;
        QTest::newRow("disabled")<<QString("none")<<false<<false;QTest::newRow("video-offer-audio-answer")<<QString("none")<<true<<true;
    }
    void accountAutoAnswer(){
        QFETCH(QString,mode);QFETCH(bool,enabled);QFETCH(bool,video);QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));
        auto alice=direct("alice",mode),bob=direct("bob",mode);bob.autoAnswer=enabled;QVERIFY(a.addAccount(alice));QVERIFY(b.addAccount(bob));QVERIFY(a.dial("alice",uri(b,"bob"),video));
        if(enabled){QVERIFY2(waitFor([&]{return streams(a)&&streams(b)&&download(a)>0&&download(b)>0;}),qPrintable(b.lastError()));for(const auto &i:b.calls())QVERIFY(!i.video);}
        else {QVERIFY(waitFor([&]{return !b.calls().isEmpty();}));QTest::qWait(300);QVERIFY(!streams(a)&&!streams(b));QCOMPARE(b.calls().first().rawState,LinphoneCallIncomingReceived);}
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
    }
    void autoAnswerIsPerAccount(){
        QTemporaryDir tmp;MiniPbx pbx;SipEngine e;QVERIFY(e.start({},tmp.path(),true));auto alice=direct("alice"),bob=direct("bob");alice.server=bob.server=pbx.server();alice.registerEnabled=bob.registerEnabled=true;bob.autoAnswer=true;
        QVERIFY(e.addAccount(alice));QVERIFY(e.addAccount(bob));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));QVERIFY(e.dial("alice","bob"));
        QVERIFY(waitFor([&]{if(e.calls().size()!=2)return false;for(const auto &i:e.calls())if(i.rawState!=LinphoneCallStreamsRunning||i.audioDownKbps<=0)return false;return true;}));
        for(const auto &i:e.calls())if(!i.incoming){QVERIFY(e.hangup(i.id));break;}QVERIFY(waitFor([&]{return e.calls().isEmpty();}));
        QVERIFY(e.dial("bob","alice"));QVERIFY(waitFor([&]{return e.calls().size()==2;}));QTest::qWait(300);for(const auto &i:e.calls())if(i.incoming){QCOMPARE(i.accountId,QString("alice"));QCOMPARE(i.rawState,LinphoneCallIncomingReceived);}
        for(const auto &i:e.calls())if(!i.incoming){QVERIFY(e.hangup(i.id));break;}QVERIFY(waitFor([&]{return e.calls().isEmpty();}));
    }
    void dndAndForwardOverrideAutoAnswer(){
        QTemporaryDir tmp;SipEngine a,b,c;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(c.start({},tmp.path()+"/c",true));
        auto bob=direct("bob"),charlie=direct("charlie");bob.autoAnswer=charlie.autoAnswer=true;QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(bob));QVERIFY(c.addAccount(charlie));
        b.setDnd(true);QSignalSpy finished(&a,&SipEngine::callFinished);QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return !finished.isEmpty();}));QVERIFY(!streams(b));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
        QVERIFY(b.removeAccount("bob"));bob.forwardTo=uri(c,"charlie");QVERIFY(b.addAccount(bob));QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return streams(a)&&streams(c)&&b.calls().isEmpty();}));
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void manualRejectBeforeQueuedAutoAnswer(){
        QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));auto bob=direct("bob");bob.autoAnswer=true;QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(bob));
        QSignalSpy errors(&b,&SipEngine::errorOccurred),finished(&a,&SipEngine::callFinished);bool connected=false;
        connect(&b,&SipEngine::callChanged,&b,[&](const CallInfo &i){connected|=i.rawState==LinphoneCallStreamsRunning;if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&b,[&,id=i.id]{b.hangup(id);});});
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return !finished.isEmpty();}));QVERIFY(!connected);QVERIFY(errors.isEmpty());
    }
    void defaultMediaIsRtp() {
        QCOMPARE(Account().encryption,QString("none"));QCOMPARE(Account::fromJson({}).encryption,QString("none"));
        auto saved=Account::fromJson({{"encryption","srtp"}});QCOMPARE(saved.encryption,QString("srtp"));
    }
    void requiredSrtpDoesNotRetryUnencrypted() {
        QTemporaryDir tmp;QUdpSocket pbx;QVERIFY(pbx.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));int invites=0;bool plaintext=false;
        connect(&pbx,&QUdpSocket::readyRead,this,[&]{while(pbx.hasPendingDatagrams()){
            QByteArray packet(int(pbx.pendingDatagramSize()),'\0');QHostAddress peer;quint16 port;pbx.readDatagram(packet.data(),packet.size(),&peer,&port);if(!packet.startsWith("INVITE "))continue;
            ++invites;plaintext|=packet.contains(" RTP/AVP ");QMap<QByteArray,QByteArray> headers;
            for(auto line:packet.split('\n')){line=line.trimmed();int colon=line.indexOf(':');if(colon>0)headers[line.left(colon).toLower()]=line.mid(colon+1).trimmed();}
            QByteArray response="SIP/2.0 488 Not Acceptable Here\r\n";for(const auto &name:{QByteArray("via"),QByteArray("from"),QByteArray("to"),QByteArray("call-id"),QByteArray("cseq")})response+=name+": "+headers[name]+(name=="to"?";tag=no-srtp":"")+"\r\n";
            response+="Content-Length: 0\r\n\r\n";pbx.writeDatagram(response,peer,port);
        }});
        SipEngine e;QVERIFY(e.start({},tmp.path(),true));QVERIFY(e.addAccount(direct("alice","srtp")));QSignalSpy done(&e,&SipEngine::callFinished);
        QVERIFY(e.dial("alice","sip:bob@127.0.0.1:"+QString::number(pbx.localPort())));QVERIFY(waitFor([&]{return !done.isEmpty();}));QCOMPARE(invites,1);QVERIFY(!plaintext);
    }
    void requiredSrtpRejectsPlainIncomingOffer() {
        QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob","srtp")));
        QString incoming;connect(&b,&SipEngine::callChanged,&b,[&](const CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)incoming=i.id;});QSignalSpy done(&a,&SipEngine::callFinished);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return !incoming.isEmpty()||!done.isEmpty();}));
        if(!incoming.isEmpty()){QVERIFY(!b.answer(incoming,false));QVERIFY(b.lastError().contains("requires SRTP"));}
        QVERIFY(waitFor([&]{return !done.isEmpty();}));QVERIFY(!streams(a)&&!streams(b));
    }
    void validationAndTargets() {
        auto a=direct("alice");QVERIFY(a.validate().isEmpty());a.transport="sctp";QVERIFY(!a.validate().isEmpty());
        a.transport="tls";a.webrtc=true;QVERIFY(!a.validate().isEmpty());a.encryption="dtls";a.ice=true;QVERIFY(a.validate().isEmpty());
        QCOMPARE(normalizeTarget(" 100 ","example.test"),QString("sip:100@example.test"));QVERIFY(normalizeTarget("100\r\nX: y","test").isEmpty());
        QCOMPARE(normalizeTarget("sips:a@example.test","ignored"),QString("sips:a@example.test"));
    }
    void callsBetweenAccounts_data() {
        QTest::addColumn<QString>("security");QTest::addColumn<bool>("ice");QTest::addColumn<QString>("caller");
        QTest::newRow("RTP-alice-to-bob")<<QString("none")<<false<<QString("alice");
        QTest::newRow("RTP-bob-to-alice")<<QString("none")<<false<<QString("bob");
        QTest::newRow("SRTP-ICE-alice-to-bob")<<QString("srtp")<<true<<QString("alice");
        QTest::newRow("SRTP-ICE-bob-to-alice")<<QString("srtp")<<true<<QString("bob");
    }
    void callsBetweenAccounts() {
        QFETCH(QString,security);QFETCH(bool,ice);QFETCH(QString,caller);const QString callee=caller=="alice"?"bob":"alice";QTemporaryDir tmp;MiniPbx pbx;SipEngine e;
        Preferences p;p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();QVERIFY(e.start(p,tmp.path(),true));
        auto a=direct("alice",security),b=direct("bob",security);a.registerEnabled=b.registerEnabled=true;a.server=b.server=pbx.server();a.ice=b.ice=ice;
        QVERIFY(e.addAccount(a));QVERIFY(e.addAccount(b));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));
        QString incoming,outgoing;QStringList events;
        connect(&e,&SipEngine::callChanged,&e,[&](const CallInfo &i){events<<i.accountId+" "+i.state+" "+i.message;if(i.rawState==LinphoneCallIncomingReceived)incoming=i.id;if(!i.incoming)outgoing=i.id;});
        QVERIFY(e.dial(caller,callee));QVERIFY2(waitFor([&]{return !incoming.isEmpty();}),qPrintable(events.join('\n')));
        for(const auto &i:e.calls())if(i.id==incoming)QCOMPARE(i.accountId,callee);
        QVERIFY2(e.answer(incoming,false),qPrintable(e.lastError()));
        QVERIFY2(waitFor([&]{const auto calls=e.calls();if(calls.size()!=2)return false;for(const auto &i:calls)if(i.rawState!=LinphoneCallStreamsRunning||i.audioDownKbps<=0)return false;return true;},10000),qPrintable(events.join('\n')));
        QCOMPARE(pbx.invites,1);QVERIFY(pbx.answers>=1);QVERIFY(!outgoing.isEmpty());
        for(const auto &i:e.calls()){QVERIFY(i.accountId=="alice"||i.accountId=="bob");QVERIFY(i.encryption.contains(security=="srtp"?"srtp":"none",Qt::CaseInsensitive));}
        QVERIFY(e.hold(outgoing,true));QVERIFY(waitFor([&]{int paused=0;for(const auto &i:e.calls())paused+=i.rawState==LinphoneCallPaused||i.rawState==LinphoneCallPausedByRemote;return paused==2;}));
        QVERIFY(e.hold(outgoing,false));QVERIFY(waitFor([&]{int running=0;for(const auto &i:e.calls())running+=i.rawState==LinphoneCallStreamsRunning;return running==2;}));
        QVERIFY(e.record(incoming,true));QVERIFY(e.record(outgoing,true));QTest::qWait(600);QVERIFY(e.record(incoming,false));QVERIFY(e.record(outgoing,false));
        QVERIFY(e.hangup(outgoing));QVERIFY(waitFor([&]{return e.calls().isEmpty();}));
        QCOMPARE(linphone_core_get_media_resource_mode(e.native()),LinphoneExclusiveMediaResources);
        const auto files=QDir(tmp.path()+"/recordings").entryList({"*.wav"},QDir::Files);QCOMPARE(files.size(),2);
        for(const auto &file:files){QFile recording(tmp.path()+"/recordings/"+file);QVERIFY(recording.open(QIODevice::ReadOnly));const auto pcm=recording.readAll();
            bool audible=false;for(int n=128;n+1<pcm.size();n+=2)audible|=std::abs(int(qint16(quint8(pcm[n])|(quint16(quint8(pcm[n+1]))<<8))))>200;QVERIFY(audible);}
    }
    void rememberedCredentialsAndOptOut() {
        QTemporaryDir tmp;Store store(tmp.path());auto a=direct("saved");a.password="test-sip-secret";a.keyPassword="test-key-secret";a.turnPassword="test-turn-secret";
        QVERIFY(a.rememberPassword);QString error;QVERIFY2(store.save({a},{},&error),qPrintable(error));
        QFile config(tmp.path()+"/settings.json");QVERIFY(config.open(QIODevice::ReadOnly));const auto data=config.readAll();QVERIFY(!data.contains("test-sip-secret"));QVERIFY(!data.contains("test-key-secret"));QVERIFY(!data.contains("test-turn-secret"));
        Store restarted(tmp.path());QVector<Account> accounts;Preferences prefs;QVERIFY2(restarted.load(accounts,prefs,&error),qPrintable(error));QCOMPARE(accounts.size(),1);
        QCOMPARE(accounts[0].password,a.password);QCOMPARE(accounts[0].keyPassword,a.keyPassword);QCOMPARE(accounts[0].turnPassword,a.turnPassword);
        accounts[0].rememberPassword=false;QVERIFY2(restarted.save(accounts,prefs,&error),qPrintable(error));
        Store sessionOnly(tmp.path());QVERIFY(sessionOnly.load(accounts,prefs,&error));QVERIFY(accounts[0].password.isEmpty());QVERIFY(accounts[0].keyPassword.isEmpty());QVERIFY(accounts[0].turnPassword.isEmpty());
        QByteArray secret;QVERIFY(SecretStore(tmp.path()).read(secret,&error));QVERIFY(secret.isEmpty());
    }
    void codecPolicyAndPersistence() {
        QTemporaryDir tmp;SipEngine e;QVERIFY(e.start({},tmp.path()+"/engine",true));
        const auto inventory=e.availableCodecs(false);QVERIFY(inventory.size()>=5);QStringList selected;
        for(const auto &c:inventory)if(c.mime=="G729"||c.mime=="PCMA")selected.prepend(c.id);
        QVERIFY(!selected.isEmpty());QVERIFY2(e.applyCodecSelection(false,selected),qPrintable(e.lastError()));
        QStringList actual;for(const auto &c:e.availableCodecs(false))if(c.enabled)actual<<c.id;QCOMPARE(actual,selected);
        QVERIFY(!e.applyCodecSelection(false,{}));QVERIFY(!e.applyCodecSelection(false,{"missing/8000/1;"}));QVERIFY(!e.applyCodecSelection(false,{selected[0],selected[0]}));
        actual.clear();for(const auto &c:e.availableCodecs(false))if(c.enabled)actual<<c.id;QCOMPARE(actual,selected);
        Preferences p;p.customAudioCodecs=true;p.audioCodecs=selected;Store store(tmp.path()+"/settings");QVERIFY(store.save({},p));
        QVector<Account> accounts;Preferences restored;QVERIFY(store.load(accounts,restored));QCOMPARE(restored.toJson(),p.toJson());
        SipEngine restarted;QVERIFY(restarted.start(restored,tmp.path()+"/restart",true));actual.clear();for(const auto &c:restarted.availableCodecs(false))if(c.enabled)actual<<c.id;QCOMPARE(actual,selected);
        QVERIFY(restarted.applyCodecSelection(false,{},false));QVERIFY(restarted.codecs(false).contains("opus/48000"));
    }
    void forcedAudioCodec_data() {
        QTest::addColumn<QString>("codec");QTest::addColumn<QString>("mime");QTemporaryDir tmp;SipEngine e;QVERIFY(e.start({},tmp.path(),true));
        for(const auto &c:e.availableCodecs(false))if(c.available)QTest::newRow(qPrintable(c.id))<<c.id<<c.mime;
    }
    void forcedAudioCodec() {
        QFETCH(QString,codec);QFETCH(QString,mime);QTemporaryDir tmp;
        Preferences p;p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();p.customAudioCodecs=true;p.audioCodecs=QStringList{codec};
        Preferences q=p;q.inputDevice=dummyMicSilence();q.audioPortMin=12000;q.audioPortMax=12098;
        SipEngine a,b;QVERIFY2(a.start(p,tmp.path()+"/a",true),qPrintable(a.lastError()));QVERIFY(b.start(q,tmp.path()+"/b",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));autoAnswer(b);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY2(waitFor([&]{return streams(a)&&streams(b);}),qPrintable(codec));
        QVERIFY(a.calls().first().audioCodec.startsWith(mime+"/"));QVERIFY(b.calls().first().audioCodec.startsWith(mime+"/"));
        QVERIFY(!a.applyCodecSelection(false,{codec}));QVERIFY(waitFor([&]{return download(b)>0;}));
        QVERIFY(b.record(activeId(b),true));QTest::qWait(650);QVERIFY(b.record(activeId(b),false));
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
        const auto files=QDir(tmp.path()+"/b/recordings").entryList({"*.wav"},QDir::Files);QVERIFY(!files.isEmpty());QFile f(tmp.path()+"/b/recordings/"+files.first());QVERIFY(f.open(QIODevice::ReadOnly));const auto pcm=f.readAll();
        bool audible=false;for(int n=128;n+1<pcm.size();n+=2){qint16 sample=qint16(quint8(pcm[n])|(quint16(quint8(pcm[n+1]))<<8));audible|=std::abs(int(sample))>200;}QVERIFY2(audible,qPrintable("No decoded tone with "+codec));
    }
    void forcedVideoCodec_data() {
        QTest::addColumn<QString>("codec");QTest::addColumn<QString>("mime");QTemporaryDir tmp;SipEngine e;QVERIFY(e.start({},tmp.path(),true));
        for(const auto &c:e.availableCodecs(true))if(c.available)QTest::newRow(qPrintable(c.id))<<c.id<<c.mime;
    }
    void forcedVideoCodec() {
        QFETCH(QString,codec);QFETCH(QString,mime);QTemporaryDir tmp;Preferences p;p.videoDevice=dummyCamera();p.customVideoCodecs=true;p.videoCodecs=QStringList{codec};
        SipEngine a,b;QVERIFY(a.start(p,tmp.path()+"/a",true));p.audioPortMin=12000;p.audioPortMax=12098;p.videoPortMin=13000;p.videoPortMax=13098;QVERIFY(b.start(p,tmp.path()+"/b",true));
        decodedVideoFrames=0;videoReceiverFactory=linphone_core_get_ms_factory(b.native());
        for(auto *e:{&a,&b}){ms_factory_register_filter(linphone_core_get_ms_factory(e->native()),&decodedSinkDesc);linphone_core_enable_video_display(e->native(),TRUE);linphone_core_set_video_display_filter(e->native(),"StelDecodedVideoSink");linphone_core_set_preferred_video_definition_by_name(e->native(),"cif");linphone_core_set_preferred_framerate(e->native(),10);}
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));int decoded=0;
        connect(&b,&SipEngine::callChanged,&b,[&](const CallInfo &i){if(i.rawState!=LinphoneCallIncomingReceived)return;
            auto *call=static_cast<LinphoneCall*>(linphone_core_get_calls(b.native())->data);auto *cb=linphone_factory_create_call_cbs(linphone_factory_get());linphone_call_cbs_set_user_data(cb,&decoded);
            linphone_call_cbs_set_next_video_frame_decoded(cb,[](LinphoneCall *c){++*static_cast<int*>(linphone_call_cbs_get_user_data(linphone_call_get_current_callbacks(c)));});linphone_call_add_callbacks(call,cb);linphone_call_cbs_unref(cb);linphone_call_request_notify_next_video_frame_decoded(call);
        });autoAnswer(b,true);
        QVERIFY(a.dial("alice",uri(b,"bob"),true));QVERIFY2(waitFor([&]{return streams(a)&&streams(b);}),qPrintable(codec));
        QVERIFY2(a.calls().first().videoCodec.startsWith(mime+"/"),qPrintable(a.calls().first().videoCodec));QVERIFY(b.calls().first().videoCodec.startsWith(mime+"/"));
        QVERIFY2(waitFor([&]{return decodedVideoFrames>0&&download(b,true)>0;},12000),qPrintable("No decoded video frame/RTP with "+codec));
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
    }
    void dummyDevicesAndSeparateNetworks() {
        QTemporaryDir tmp;Preferences p;p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();p.videoDevice=dummyCamera();
        p.signallingAddress="127.0.0.1";p.mediaAddress="127.0.0.1";
        QUdpSocket freePort;QVERIFY(freePort.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));p.sipUdpPort=freePort.localPort();freePort.close();
        for(const auto &address:localAddresses())if(QHostAddress(address.address).protocol()==QAbstractSocket::IPv4Protocol&&!QHostAddress(address.address).isLoopback()){p.mediaAddress=address.address;break;}
        SipEngine a,b;QVERIFY2(a.start(p,tmp.path()+"/a",true),qPrintable(a.lastError()));
        auto *sipPorts=linphone_core_get_transports_used(a.native());QCOMPARE(linphone_transports_get_udp_port(sipPorts),p.sipUdpPort);linphone_transports_unref(sipPorts);
        QVERIFY(a.soundDevices().join(' ').contains("Dummy mic"));QVERIFY(QFileInfo(tmp.path()+"/a/dummy-camera.jpg").size()>1000);
        Preferences q=p;q.sipUdpPort=-1;q.inputDevice=dummyMicSilence();q.mediaAddress="127.0.0.1";q.audioPortMin=12000;q.audioPortMax=12098;q.videoPortMin=13000;q.videoPortMax=13098;
        QVERIFY2(b.start(q,tmp.path()+"/b",true),qPrintable(b.lastError()));
        QVERIFY(a.addAccount(direct("alice","srtp")));QVERIFY(b.addAccount(direct("bob","srtp")));autoAnswer(b,true);
        QUdpSocket relay;QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
        auto *bPorts=linphone_core_get_transports_used(b.native());const auto bPort=quint16(linphone_transports_get_udp_port(bPorts));linphone_transports_unref(bPorts);
        QHostAddress callerIp;quint16 callerPort=0;QByteArray offer;
        connect(&relay,&QUdpSocket::readyRead,this,[&]{while(relay.hasPendingDatagrams()){
            QByteArray packet;packet.resize(int(relay.pendingDatagramSize()));QHostAddress sender;quint16 port;relay.readDatagram(packet.data(),packet.size(),&sender,&port);
            if(packet.startsWith("SIP/2.0 "))relay.writeDatagram(packet,callerIp,callerPort);
            else {callerIp=sender;callerPort=port;if(packet.startsWith("INVITE "))offer=packet;relay.writeDatagram(packet,QHostAddress(QHostAddress::LocalHost),bPort);}
        }});
        QVERIFY(a.dial("alice","sip:bob@127.0.0.1:"+QString::number(relay.localPort()),true));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        QCOMPARE(callerIp,QHostAddress(p.signallingAddress));
        QVERIFY2(offer.contains("c=IN IP4 "+p.mediaAddress.toUtf8()+"\r\n"),offer.constData());
        QVERIFY(waitFor([&]{return download(b)>0 && download(b,true)>0;},8000));
        const auto *node=linphone_core_get_calls(a.native());QVERIFY(node);
        auto *call=static_cast<LinphoneCall*>(node->data);auto *transport=linphone_call_get_meta_rtp_transport(call,0);QVERIFY(transport&&transport->session);
        const int rtpPort=rtp_session_get_local_port(transport->session);QVERIFY(rtpPort>=p.audioPortMin&&rtpPort<=p.audioPortMax);
        QVERIFY(offer.contains("m=audio "+QByteArray::number(rtpPort)+" "));
        sockaddr_storage socketAddress{};socklen_t length=sizeof(socketAddress);
        QCOMPARE(::getsockname(rtp_session_get_rtp_socket(transport->session),reinterpret_cast<sockaddr*>(&socketAddress),&length),0);
        char boundIp[128]{};int boundPort=0;bctbx_sockaddr_to_ip_address(reinterpret_cast<sockaddr*>(&socketAddress),length,boundIp,sizeof(boundIp),&boundPort);
        QCOMPARE(QHostAddress(QString::fromLatin1(boundIp)),QHostAddress(p.mediaAddress));QCOMPARE(boundPort,rtpPort);
        QVERIFY(!a.applyNetwork(p));
        QVERIFY(b.record(activeId(b),true));QTest::qWait(800);QVERIFY(b.record(activeId(b),false));
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
        auto files=QDir(tmp.path()+"/b/recordings").entryList({"*.wav"},QDir::Files);QVERIFY(!files.isEmpty());
        QFile recorded(tmp.path()+"/b/recordings/"+files[0]);QVERIFY(recorded.open(QIODevice::ReadOnly));const auto pcm=recorded.readAll();QVERIFY(pcm.size()>1000);
        bool audible=false;for(int i=128;i+1<pcm.size();i+=2){const auto value=qint16(quint8(pcm[i])|(quint16(quint8(pcm[i+1]))<<8));audible|=std::abs(int(value))>200;}QVERIFY2(audible,"The decoded remote recording must contain the generated test tone, not just RTP keepalives");
        p.sipUdpPort=-1;QVERIFY(a.applyNetwork(p));Preferences invalid=p;invalid.mediaAddress="192.0.2.99";QVERIFY(!a.applyNetwork(invalid));
        invalid=p;invalid.videoPortMin=invalid.audioPortMin;QVERIFY(!a.applyNetwork(invalid));
        Store store(tmp.path()+"/saved");QVERIFY(store.save({},p));QVector<Account> restored;Preferences saved;QVERIFY(store.load(restored,saved));QCOMPARE(saved.toJson(),p.toJson());
    }
    void audioHoldDtmfAndRecording() {
        QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));autoAnswer(b);
        int receivedDtmf=0;
        auto *dtmfCallbacks=linphone_factory_create_core_cbs(linphone_factory_get());
        linphone_core_cbs_set_user_data(dtmfCallbacks,&receivedDtmf);
        linphone_core_cbs_set_dtmf_received(dtmfCallbacks,[](LinphoneCore *core,LinphoneCall *,int digit){
            auto *received=static_cast<int*>(linphone_core_cbs_get_user_data(linphone_core_get_current_callbacks(core)));*received=digit;
        });
        linphone_core_add_callbacks(b.native(),dtmfCallbacks);
        const auto tone=tmp.path()+"/tone.wav";writeTone(tone);linphone_core_set_play_file(a.native(),tone.toUtf8().constData());
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY2(waitFor([&]{return streams(a)&&streams(b);}),"Both endpoints must establish RTP");
        QVERIFY2(waitFor([&]{return download(b)>0;},6000),"RTP must actually reach the other endpoint");
        const QString id=activeId(a);QVERIFY(a.record(id,true));QTest::qWait(300);QVERIFY(a.record(id,false));
        QVERIFY(a.dtmf(id,'5'));QVERIFY(waitFor([&]{return receivedDtmf=='5';}));
        linphone_core_remove_callbacks(b.native(),dtmfCallbacks);linphone_core_cbs_unref(dtmfCallbacks);
        QVERIFY(a.hold(id,true));QVERIFY(waitFor([&]{for(const auto &i:a.calls())if(i.rawState==LinphoneCallPaused)return true;return false;}));
        QVERIFY(a.hold(id,false));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        a.mute(true);QVERIFY(!linphone_core_mic_enabled(a.native()));a.mute(false);
        QVERIFY(a.hangup(id));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
        const auto files=QDir(tmp.path()+"/a/recordings").entryList({"*.wav"},QDir::Files);QVERIFY(!files.isEmpty());QVERIFY(QFileInfo(tmp.path()+"/a/recordings/"+files[0]).size()>44);
    }
    void encryptedVideo_data() {
        QTest::addColumn<QString>("encryption");
        QTest::newRow("SRTP")<<QString("srtp");
        QTest::newRow("WebRTC-DTLS-ICE-BUNDLE")<<QString("dtls");
    }
    void encryptedVideo() {
        QFETCH(QString,encryption);
        QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));
        auto alice=direct("alice",encryption),bob=direct("bob",encryption);
        if(encryption=="dtls"){alice.ice=bob.ice=true;alice.webrtc=bob.webrtc=true;}
        QVERIFY(a.addAccount(alice));QVERIFY(b.addAccount(bob));autoAnswer(b,true);
        QVERIFY2(!a.codecs(true).isEmpty(),"At least one video codec must be compiled in");
        QVERIFY(a.dial("alice",uri(b,"bob"),true));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        QVERIFY2(waitFor([&]{return download(b,true)>0;},8000),"Encrypted video RTP must reach the peer");
        bool video=false;for(const auto &i:a.calls()){video|=i.video;QVERIFY(i.encryption.contains(encryption,Qt::CaseInsensitive));}QVERIFY(video);
        QVERIFY(a.hangup(activeId(a)));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));
    }
    void forwarding() {
        QTemporaryDir tmp;SipEngine a,b,c;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(c.start({},tmp.path()+"/c",true));
        auto bob=direct("bob");bob.forwardTo=uri(c,"charlie");QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(bob));QVERIFY(c.addAccount(direct("charlie")));autoAnswer(c);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY2(waitFor([&]{return streams(a)&&streams(c);}),"302 must redirect caller to the third endpoint");
        a.hangup(activeId(a));QVERIFY(waitFor([&]{return a.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void blindTransfer() {
        QTemporaryDir tmp;SipEngine a,b,c;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(c.start({},tmp.path()+"/c",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));QVERIFY(c.addAccount(direct("charlie")));autoAnswer(b);autoAnswer(c);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        QVERIFY(b.transfer(activeId(b),uri(c,"charlie")));
        QVERIFY2(waitFor([&]{for(const auto &i:a.calls())if(i.remote.contains("charlie")&&i.rawState==LinphoneCallStreamsRunning)return streams(c)&&b.calls().isEmpty();return false;},10000),"REFER must establish the call with the transfer destination and complete the original dialog");
        bool found=false;for(const auto &i:a.calls())found|=i.remote.contains("charlie");QVERIFY(found);
        for(const auto &i:a.calls())a.hangup(i.id);
        QVERIFY(waitFor([&]{return a.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void attendedTransfer() {
        QTemporaryDir tmp;SipEngine a,b,c;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(c.start({},tmp.path()+"/c",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));QVERIFY(c.addAccount(direct("charlie")));autoAnswer(b);autoAnswer(c);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        const auto original=activeId(b);QVERIFY(b.hold(original,true));QVERIFY(waitFor([&]{for(const auto &i:b.calls())if(i.id==original&&i.rawState==LinphoneCallPaused)return true;return false;}));
        QVERIFY(b.dial("bob",uri(c,"charlie")));QVERIFY(waitFor([&]{return streams(b)&&streams(c);}));QString consult;
        for(const auto &i:b.calls())if(i.id!=original)consult=i.id;
        QVERIFY(!consult.isEmpty());
        QVERIFY(b.attendedTransfer(original,consult));
        QVERIFY2(waitFor([&]{for(const auto &i:c.calls())if(i.remote.contains("alice")&&i.rawState==LinphoneCallStreamsRunning)return streams(a)&&b.calls().isEmpty();return false;},10000),"REFER/Replaces must connect original caller to consultation peer and close the intermediary dialogs");
        for(const auto &i:a.calls())a.hangup(i.id);
        QVERIFY(waitFor([&]{return a.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void dndAndMultipleAccounts() {
        QTemporaryDir tmp;SipEngine a,b;QVERIFY(a.start({},tmp.path()+"/a",true));Preferences p;p.dnd=true;QVERIFY(b.start(p,tmp.path()+"/b",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(a.addAccount(direct("second")));QVERIFY(b.addAccount(direct("bob")));
        QSignalSpy done(&a,&SipEngine::callFinished);QVERIFY(a.dial("second",uri(b,"bob")));QVERIFY(waitFor([&]{return !done.isEmpty();}));QVERIFY(!streams(b));
        QVERIFY(a.removeAccount("second"));QVERIFY(!a.dial("second",uri(b,"bob")));
    }
    void conferenceJoinsOnlySelectedCalls() {
        QTemporaryDir tmp;SipEngine host,a,b,c;QVERIFY(host.start({},tmp.path()+"/host",true));QVERIFY(host.addAccount(direct("host")));
        const QList<SipEngine*> peers{&a,&b,&c};const QStringList users{"alice","bob","charlie"};QStringList ids;
        for(int n=0;n<peers.size();++n) {
            auto &peer=*peers[n];QVERIFY(peer.start({},tmp.path()+"/"+users[n],true));QVERIFY(peer.addAccount(direct(users[n])));autoAnswer(peer);
            QVERIFY(host.dial("host",uri(peer,users[n])));QVERIFY(waitFor([&]{return streams(host)&&streams(peer);}));
            QString id;for(const auto &i:host.calls())if(!ids.contains(i.id))id=i.id;QVERIFY(!id.isEmpty());ids<<id;
            QVERIFY(host.hold(id,true));QVERIFY(waitFor([&]{for(const auto &i:host.calls())if(i.id==id)return i.rawState==LinphoneCallPaused;return false;}));
        }
        QVERIFY(host.muteCall(ids[0],true));for(const auto &i:host.calls())QCOMPARE(i.muted,i.id==ids[0]);QVERIFY(host.muteCall(ids[0],false));
        QVERIFY2(host.conference(ids[0],ids[1]),qPrintable(host.lastError()));
        QVERIFY(waitFor([&]{int joined=0;for(const auto &i:host.calls()){if(i.id==ids[2]&&(i.inConference||i.rawState!=LinphoneCallPaused))return false;joined+=i.inConference&&i.conferenceParticipants==2;}return joined==2&&streams(a)&&streams(b);}));
        for(const auto &i:host.calls())host.hangup(i.id);
        QVERIFY(waitFor([&]{return host.calls().isEmpty()&&a.calls().isEmpty()&&b.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void localConference() {
        QTemporaryDir tmp;SipEngine a,b,c;QVERIFY(a.start({},tmp.path()+"/a",true));QVERIFY(b.start({},tmp.path()+"/b",true));QVERIFY(c.start({},tmp.path()+"/c",true));
        QVERIFY(a.addAccount(direct("alice")));QVERIFY(b.addAccount(direct("bob")));QVERIFY(c.addAccount(direct("charlie")));autoAnswer(b);autoAnswer(c);
        QVERIFY(a.dial("alice",uri(b,"bob")));QVERIFY(waitFor([&]{return streams(a)&&streams(b);}));
        const auto original=activeId(b);QVERIFY(b.hold(original,true));QVERIFY(waitFor([&]{for(const auto &i:b.calls())if(i.rawState==LinphoneCallPaused)return true;return false;}));
        QVERIFY(b.dial("bob",uri(c,"charlie")));QVERIFY(waitFor([&]{return streams(b)&&streams(c);}));
        QVERIFY(b.mergeCalls());
        QVERIFY(waitFor([&]{auto *conf=linphone_core_get_conference(b.native());return conf && linphone_conference_get_participant_count(conf)==2 && streams(a)&&streams(c);}));
        for(const auto &i:b.calls())b.hangup(i.id);
        QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty()&&c.calls().isEmpty();}));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
