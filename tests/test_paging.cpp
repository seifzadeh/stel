#include "isolatedsipengine.h"
#include "minipbx.h"
#include "paging.h"
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <functional>

using namespace stel;
namespace {
bool waitFor(const std::function<bool()> &condition,int timeout=8000) {
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<timeout)QTest::qWait(20);
    return condition();
}
Account account(const QString &name,MiniPbx &pbx,const QString &security="none") {
    Account a;a.id=a.username=name;a.domain="localhost";a.server=pbx.server();
    a.transport="udp";a.ice=false;a.encryption=security;return a;
}
Preferences devices() {Preferences p;p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();p.videoDevice=dummyCamera();return p;}
bool running(const SipEngine &engine) {for(const auto &call:engine.calls())if(call.rawState==LinphoneCallStreamsRunning)return true;return false;}
void endCalls(SipEngine &a,SipEngine &b) {for(const auto &call:a.calls())a.hangup(call.id);QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().isEmpty();}));}
}
class PagingTests:public QObject {
    Q_OBJECT
private slots:
    void init(){IsolatedSipEngine::resetPortPools();}
    void headers_data() {
        QTest::addColumn<QString>("alert");QTest::addColumn<QString>("call");QTest::addColumn<QString>("source");
        QTest::newRow("yealink-words")<<QString("Auto Answer")<<QString()<<QString("Alert-Info");
        QTest::newRow("freepbx")<<QString("<Auto Answer>")<<QString()<<QString("Alert-Info");
        QTest::newRow("yealink-info")<<QString("<sip:pbx@localhost>;info=alert-autoanswer;delay=0")<<QString()<<QString("Alert-Info");
        QTest::newRow("case-spaces-quotes")<<QString("<http://pbx.invalid>; INFO = \"ALERT-AUTOANSWER\" ; DELAY = 0")<<QString()<<QString("Alert-Info");
        QTest::newRow("answer-after")<<QString("Answer-After = 0")<<QString()<<QString("Alert-Info");
        QTest::newRow("quoted-answer-after")<<QString("<sip:pbx@localhost>;answer-after=\"0\"")<<QString()<<QString("Alert-Info");
        QTest::newRow("intercom")<<QString("Intercom")<<QString()<<QString("Alert-Info");
        QTest::newRow("intercom-parameter")<<QString("intercom=true")<<QString()<<QString("Alert-Info");
        QTest::newRow("comma-list")<<QString("<http://pbx/ring.wav>;info=family, <sip:pbx>;info=alert-autoanswer")<<QString()<<QString("Alert-Info");
        QTest::newRow("call-info")<<QString()<<QString("<sip:pbx@localhost>;answer-after=0")<<QString("Call-Info");
        QTest::newRow("call-info-precedence")<<QString("Intercom")<<QString("<sip:pbx>;answer-after=0")<<QString("Call-Info");
        QTest::newRow("ordinary")<<QString()<<QString()<<QString();
        QTest::newRow("distinctive-ring")<<QString("<http://pbx/ring.wav>;info=family")<<QString()<<QString();
        QTest::newRow("url-is-not-authority")<<QString("<http://pbx/auto-answer.wav?info=alert-autoanswer>")<<QString()<<QString();
        QTest::newRow("uri-parameter")<<QString("<sip:auto-answer@pbx;answer-after=0>")<<QString()<<QString();
        QTest::newRow("negative")<<QString("answer-after=-1")<<QString()<<QString();
        QTest::newRow("positive-delay")<<QString("info=alert-autoanswer;answer-after=5")<<QString()<<QString();
        QTest::newRow("nonzero-delay")<<QString("info=alert-autoanswer;delay=2")<<QString()<<QString();
        QTest::newRow("false-intercom")<<QString("Intercom;intercom=false")<<QString()<<QString();
        QTest::newRow("bad-number")<<QString("answer-after=0junk")<<QString()<<QString();
        QTest::newRow("invalid-quotes")<<QString("info=\"alert-autoanswer")<<QString()<<QString();
        QTest::newRow("header-injection")<<QString("Intercom\r\nOther: value")<<QString()<<QString();
        QTest::newRow("call-info-disables-immediate")<<QString("Intercom")<<QString("<sip:pbx>;answer-after=5")<<QString();
    }
    void headers() {QFETCH(QString,alert);QFETCH(QString,call);QFETCH(QString,source);const auto result=pagingRequest(alert,call);QCOMPARE(result.requested,!source.isEmpty());QCOMPARE(result.headerName,source);}
    void settingsRoundTrip() {
        const auto legacy=Account::fromJson({});QVERIFY(legacy.pagingEnabled);QVERIFY(legacy.pagingMicMuted);QVERIFY(legacy.pagingBeep);QVERIFY(!legacy.autoAnswer);
        auto a=legacy;a.pagingEnabled=a.pagingMicMuted=a.pagingBeep=false;const auto restored=Account::fromJson(a.toJson());QVERIFY(!restored.pagingEnabled);QVERIFY(!restored.pagingMicMuted);QVERIFY(!restored.pagingBeep);
    }
    void incomingPage_data() {
        QTest::addColumn<QByteArray>("header");QTest::addColumn<QByteArray>("value");QTest::addColumn<bool>("enabled");QTest::addColumn<bool>("muted");QTest::addColumn<bool>("beep");QTest::addColumn<bool>("accepted");QTest::addColumn<QString>("security");
        QTest::newRow("auto-answer-words")<<QByteArray("Alert-Info")<<QByteArray("Auto Answer")<<true<<true<<false<<true<<QString("none");
        QTest::newRow("freepbx")<<QByteArray("Alert-Info")<<QByteArray("<Auto Answer>")<<true<<true<<false<<true<<QString("none");
        QTest::newRow("yealink-info")<<QByteArray("Alert-Info")<<QByteArray("<sip:pbx@localhost>;info=alert-autoanswer;delay=0")<<true<<true<<false<<true<<QString("none");
        QTest::newRow("answer-after")<<QByteArray("Alert-Info")<<QByteArray("Answer-After = 0")<<true<<true<<false<<true<<QString("none");
        QTest::newRow("intercom-beep")<<QByteArray("Alert-Info")<<QByteArray("Intercom")<<true<<true<<true<<true<<QString("none");
        QTest::newRow("call-info")<<QByteArray("Call-Info")<<QByteArray("<sip:pbx@localhost>;answer-after=0")<<true<<true<<false<<true<<QString("none");
        QTest::newRow("two-way-intercom")<<QByteArray("Alert-Info")<<QByteArray("Intercom")<<true<<false<<false<<true<<QString("none");
        QTest::newRow("srtp-page")<<QByteArray("Alert-Info")<<QByteArray("Intercom")<<true<<true<<false<<true<<QString("srtp");
        QTest::newRow("disabled")<<QByteArray("Alert-Info")<<QByteArray("Intercom")<<false<<true<<false<<false<<QString("none");
        QTest::newRow("ringtone")<<QByteArray("Alert-Info")<<QByteArray("<http://pbx/ring.wav>;info=family")<<true<<true<<false<<false<<QString("none");
        QTest::newRow("ordinary-call")<<QByteArray()<<QByteArray()<<true<<true<<false<<false<<QString("none");
    }
    void incomingPage() {
        QFETCH(QByteArray,header);QFETCH(QByteArray,value);QFETCH(bool,enabled);QFETCH(bool,muted);QFETCH(bool,beep);QFETCH(bool,accepted);QFETCH(QString,security);
        QTemporaryDir tmp;MiniPbx pbx;if(!header.isEmpty())pbx.incomingInviteHeaders={{header,value}};
        IsolatedSipEngine a,b;QVERIFY(a.start(devices(),tmp.path()+"/a",true));QVERIFY(b.start(devices(),tmp.path()+"/b",true));
        auto bob=account("bob",pbx,security);bob.pagingEnabled=enabled;bob.pagingMicMuted=muted;bob.pagingBeep=beep;
        QVERIFY(!bob.autoAnswer);QVERIFY(a.addAccount(account("alice",pbx,security)));QVERIFY(b.addAccount(bob));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));
        QVERIFY(a.dial("alice","bob",true));QVERIFY(waitFor([&]{return !b.calls().isEmpty();}));
        if(accepted) {
            QVERIFY2(waitFor([&]{return running(a)&&running(b)&&b.calls().first().audioDownKbps>0;}),qPrintable(b.lastError()));
            const auto call=b.calls().first();QVERIFY(call.paging);QVERIFY(call.pagingAutoAnswered);QCOMPARE(call.pagingHeader,QString::fromUtf8(header));QCOMPARE(call.muted,muted);QVERIFY(!call.video);QVERIFY(!a.calls().first().paging);
            if(security=="srtp")QVERIFY(call.encryption.contains("srtp",Qt::CaseInsensitive));
        } else {QTest::qWait(300);QVERIFY(!running(a)&&!running(b));QCOMPARE(b.calls().first().rawState,LinphoneCallIncomingReceived);}
        endCalls(a,b);
        if(accepted) {
            // A page's mute, auto-answer and video policy do not leak into the
            // next ordinary call on the same core and account.
            pbx.incomingInviteHeaders.clear();QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return !b.calls().isEmpty();}));QTest::qWait(200);QVERIFY(!running(b));
            const auto normal=b.calls().first();QVERIFY(!normal.paging);QVERIFY(b.answer(normal.id,false));QVERIFY(waitFor([&]{return running(a)&&running(b);}));QVERIFY(!b.calls().first().muted);endCalls(a,b);
        }
    }
    void localAccountsCanReceivePage() {
        QTemporaryDir tmp;MiniPbx pbx;pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};IsolatedSipEngine engine;QVERIFY(engine.start(devices(),tmp.path(),true));
        auto bob=account("bob",pbx);bob.pagingBeep=false;QVERIFY(engine.addAccount(account("alice",pbx)));QVERIFY(engine.addAccount(bob));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));QVERIFY(engine.dial("alice","bob"));
        QVERIFY(waitFor([&]{if(engine.calls().size()!=2)return false;for(const auto &call:engine.calls())if(call.rawState!=LinphoneCallStreamsRunning)return false;return true;}));
        for(const auto &call:engine.calls())if(call.incoming){QVERIFY(call.pagingAutoAnswered);QVERIFY(call.muted);QCOMPARE(call.accountId,QString("bob"));}
        for(const auto &call:engine.calls())if(!call.incoming){engine.hangup(call.id);break;}
        QVERIFY(waitFor([&]{return engine.calls().isEmpty();}));
    }
    void rejectBeforeAutomaticPickup() {
        QTemporaryDir tmp;MiniPbx pbx;pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};IsolatedSipEngine a,b;QVERIFY(a.start(devices(),tmp.path()+"/a",true));QVERIFY(b.start(devices(),tmp.path()+"/b",true));
        QVERIFY(a.addAccount(account("alice",pbx)));QVERIFY(b.addAccount(account("bob",pbx)));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));bool connected=false;
        QSignalSpy errors(&b,&SipEngine::errorOccurred);connect(&b,&SipEngine::callChanged,&b,[&](const CallInfo &call){connected|=call.rawState==LinphoneCallStreamsRunning;if(call.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(40,&b,[&,id=call.id]{b.hangup(id);});});
        QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return pbx.invites>0&&a.calls().isEmpty()&&b.calls().isEmpty();}));QTest::qWait(220);QVERIFY(!connected);QVERIFY(errors.isEmpty());
    }
    void dndAndBusyTakePriority() {
        QTemporaryDir tmp;MiniPbx pbx;IsolatedSipEngine a,b,c;QVERIFY(a.start(devices(),tmp.path()+"/a",true));QVERIFY(b.start(devices(),tmp.path()+"/b",true));QVERIFY(c.start(devices(),tmp.path()+"/c",true));
        auto bob=account("bob",pbx),charlie=account("charlie",pbx);bob.autoAnswer=true;charlie.autoAnswer=true;bob.pagingBeep=false;
        QVERIFY(a.addAccount(account("alice",pbx)));QVERIFY(b.addAccount(bob));QVERIFY(c.addAccount(charlie));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob")&&pbx.registered("charlie");}));
        pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};b.setDnd(true);QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return pbx.invites>0&&a.calls().isEmpty()&&b.calls().isEmpty();}));QVERIFY(!running(b));b.setDnd(false);
        pbx.incomingInviteHeaders.clear();QVERIFY(b.dial("bob","charlie"));QVERIFY(waitFor([&]{return running(b)&&running(c);}));const auto active=b.calls().first().id;
        pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return b.calls().size()==2;}));QTest::qWait(350);
        for(const auto &call:b.calls())if(call.id==active)QCOMPARE(call.rawState,LinphoneCallStreamsRunning);else {QVERIFY(call.paging);QVERIFY(!call.pagingAutoAnswered);QCOMPARE(call.rawState,LinphoneCallIncomingReceived);}
        QVERIFY(running(c));for(const auto &call:a.calls())a.hangup(call.id);
        QVERIFY(waitFor([&]{return a.calls().isEmpty()&&b.calls().size()==1;}));
        for(const auto &call:b.calls())b.hangup(call.id);
        QVERIFY(waitFor([&]{return b.calls().isEmpty()&&c.calls().isEmpty();}));
    }
    void forwardingStillWins() {
        QTemporaryDir tmp;MiniPbx pbx;pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};IsolatedSipEngine a,b,c;
        QVERIFY(a.start(devices(),tmp.path()+"/a",true));QVERIFY(b.start(devices(),tmp.path()+"/b",true));QVERIFY(c.start(devices(),tmp.path()+"/c",true));
        auto bob=account("bob",pbx),charlie=account("charlie",pbx);charlie.autoAnswer=true;bob.forwardTo="charlie";bob.pagingBeep=false;
        QVERIFY(a.addAccount(account("alice",pbx)));QVERIFY(b.addAccount(bob));QVERIFY(c.addAccount(charlie));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob")&&pbx.registered("charlie");}));
        QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return running(a)&&running(c)&&b.calls().isEmpty();}));endCalls(a,c);
    }
    void pagingIsPerAccount() {
        QTemporaryDir tmp;MiniPbx pbx;pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};IsolatedSipEngine caller,phone;
        QVERIFY(caller.start(devices(),tmp.path()+"/caller",true));QVERIFY(phone.start(devices(),tmp.path()+"/phone",true));auto bob=account("bob",pbx),charlie=account("charlie",pbx);bob.pagingEnabled=false;charlie.pagingBeep=false;
        QVERIFY(caller.addAccount(account("alice",pbx)));QVERIFY(phone.addAccount(bob));QVERIFY(phone.addAccount(charlie));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob")&&pbx.registered("charlie");}));
        QVERIFY(caller.dial("alice","bob"));QVERIFY(waitFor([&]{return !phone.calls().isEmpty();}));QTest::qWait(220);QVERIFY(!running(phone));QCOMPARE(phone.calls().first().accountId,QString("bob"));endCalls(caller,phone);
        QVERIFY(caller.dial("alice","charlie"));QVERIFY(waitFor([&]{return running(caller)&&running(phone);}));QCOMPARE(phone.calls().first().accountId,QString("charlie"));QVERIFY(phone.calls().first().pagingAutoAnswered);endCalls(caller,phone);
    }
    void pagingPreservesRequiredEncryption() {
        QTemporaryDir tmp;MiniPbx pbx;pbx.incomingInviteHeaders={{"Alert-Info","Intercom"}};IsolatedSipEngine a,b;
        QVERIFY(a.start(devices(),tmp.path()+"/a",true));QVERIFY(b.start(devices(),tmp.path()+"/b",true));auto bob=account("bob",pbx,"srtp");bob.pagingBeep=false;
        QVERIFY(a.addAccount(account("alice",pbx)));QVERIFY(b.addAccount(bob));QVERIFY(waitFor([&]{return pbx.registered("alice")&&pbx.registered("bob");}));bool connected=false;
        connect(&b,&SipEngine::callChanged,&b,[&](const CallInfo &call){connected|=call.rawState==LinphoneCallStreamsRunning;});
        QSignalSpy finished(&a,&SipEngine::callFinished);QVERIFY(a.dial("alice","bob"));QVERIFY(waitFor([&]{return !finished.isEmpty()&&a.calls().isEmpty()&&b.calls().isEmpty();}));QVERIFY(!connected);
    }
};
QTEST_MAIN(PagingTests)
#include "test_paging.moc"
