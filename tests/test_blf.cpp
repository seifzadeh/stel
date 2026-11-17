#include "blf.h"
#include "isolatedsipengine.h"
#include "minipbx.h"
#include <QTemporaryDir>
#include <QtTest>
using namespace stel;
namespace {
QByteArray document(int version,const QByteArray &dialogs={},const QByteArray &mode="full") {
    return "<dialog-info xmlns=\"urn:ietf:params:xml:ns:dialog-info\" version=\""+QByteArray::number(version)+"\" state=\""+mode+"\" entity=\"sip:2001@localhost\">"+dialogs+"</dialog-info>";
}
QByteArray dialog(const QByteArray &id,const QByteArray &state){return "<dialog id=\""+id+"\"><state>"+state+"</state></dialog>";}
Account account(const QString &name,MiniPbx &pbx){Account a;a.id=a.username=name;a.domain="localhost";a.server=pbx.server();a.transport="udp";a.ice=false;return a;}
}
class BlfTests:public QObject {
    Q_OBJECT
private slots:
    void init(){IsolatedSipEngine::resetPortPools();}
    void dialogStateAndPartialUpdates() {
        DialogInfo info;QString error;QCOMPARE(info.state(),BlfState::Unknown);
        QVERIFY(info.update(document(0),&error));QCOMPARE(info.state(),BlfState::Idle);
        QVERIFY(info.update(document(1,dialog("a","early")),&error));QCOMPARE(info.state(),BlfState::Ringing);
        QVERIFY(info.update(document(2,dialog("a","confirmed")),&error));QCOMPARE(info.state(),BlfState::Busy);
        QVERIFY(info.update(document(3,dialog("b","confirmed"),"partial"),&error));
        QVERIFY(info.update(document(4,dialog("a","terminated"),"partial"),&error));QCOMPARE(info.state(),BlfState::Busy);
        QVERIFY(info.update(document(5,dialog("b","terminated"),"partial"),&error));QCOMPARE(info.state(),BlfState::Idle);
        QVERIFY(info.update(document(6,dialog("c","confirmed")),&error));
        QVERIFY(info.update(document(5),&error));QCOMPARE(info.state(),BlfState::Busy);
        QVERIFY(!info.update(document(8,{},"partial"),&error));QCOMPARE(info.state(),BlfState::Busy);
        QVERIFY(!info.update("<dialog-info>",&error));
        DialogInfo empty;QVERIFY(!empty.update(document(0,{},"partial"),&error));QCOMPARE(empty.state(),BlfState::Unknown);
    }
    void settingsRoundTrip() {
        Preferences p;QCOMPARE(p.expansionKeys.size(),12);
        p.expansionKeys[0]={"speed-dial",QString::fromUtf8("پشتیبانی"),"account","62224"};
        p.expansionKeys[11]={"blf","Office","other","sip:2001@pbx.example"};
        QCOMPARE(Preferences::fromJson(p.toJson()).toJson(),p.toJson());
        QCOMPARE(Preferences::fromJson({}).expansionKeys.size(),12);
        QVERIFY(p.validate().isEmpty());p.expansionKeys[11].target="2001\r\nBad: header";QVERIFY(!p.validate().isEmpty());
    }
    void sipSubscriptionsUseTheirOwnAccount() {
        QTemporaryDir temporary;MiniPbx pbx;IsolatedSipEngine engine;Preferences p;
        p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();QVERIFY(engine.start(p,temporary.path(),true));
        QVERIFY(engine.addAccount(account("1001",pbx)));QVERIFY(engine.addAccount(account("1003",pbx)));
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("1001")&&pbx.registered("1003"),6000);
        auto keys=p.expansionKeys;keys[0]={"blf","Office","1001","2001"};keys[11]={"blf","Office 2","1003","2001"};
        engine.setExpansionKeys(keys);
        QTRY_COMPARE_WITH_TIMEOUT(engine.blfInfo(0).state,BlfState::Idle,6000);
        QTRY_COMPARE_WITH_TIMEOUT(engine.blfInfo(11).state,BlfState::Idle,6000);
        QCOMPARE(pbx.activeBlfSubscriptions(),2);bool first=false,second=false;
        for(const auto &from:pbx.blfFrom){first|=from.contains("1001@");second|=from.contains("1003@");}QVERIFY(first);QVERIFY(second);
        for(const auto &accept:pbx.blfAccept)QVERIFY(accept.contains("application/dialog-info+xml"));
        pbx.notifyBlf("2001",document(1,dialog("call","early")));QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Ringing);
        pbx.notifyBlf("2001",document(2,dialog("call","confirmed")));QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Busy);QTRY_COMPARE(engine.blfInfo(11).state,BlfState::Busy);
        engine.refreshRegistrations();QTest::qWait(150);QCOMPARE(engine.blfInfo(0).state,BlfState::Busy);
        pbx.notifyBlf("2001",document(3,dialog("call","terminated"),"partial"));QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Idle);QTRY_COMPARE(engine.blfInfo(11).state,BlfState::Idle);
        QVERIFY(engine.setRegistration("1001",false));QCOMPARE(engine.blfInfo(0).state,BlfState::Unknown);QCOMPARE(engine.blfInfo(11).state,BlfState::Idle);
        QTRY_VERIFY(pbx.blfUnsubscribes>0);QVERIFY(pbx.blfNotifyReplies>0);
        QVERIFY(engine.setRegistration("1001",true));QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Idle);
        keys[11].type="disabled";engine.setExpansionKeys(keys);QTRY_VERIFY(pbx.blfUnsubscribes>=2);
        QCOMPARE(engine.blfInfo(11).state,BlfState::Unknown);
        QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Idle);
        QTest::qWait(qEnvironmentVariableIsSet("STEL_BLF_LONG_TEST")?68000:120);
        pbx.notifyBlf("2001",document(1,dialog("after-reconnect","confirmed")));
        QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Busy);
        engine.shutdown(); // late NOTIFYs must stay safe during core_stop
    }
    void rejectedAndMalformedNotificationsStayUnknown() {
        QTemporaryDir temporary;MiniPbx pbx;pbx.blfResponse=403;IsolatedSipEngine engine;Preferences p;
        p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();QVERIFY(engine.start(p,temporary.path(),true));QVERIFY(engine.addAccount(account("1001",pbx)));
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("1001"),6000);auto keys=p.expansionKeys;keys[0]={"blf","Office","1001","2001"};engine.setExpansionKeys(keys);
        QTRY_COMPARE_WITH_TIMEOUT(engine.blfInfo(0).state,BlfState::Error,6000);QVERIFY(engine.blfInfo(0).detail.contains("403"));
        keys[0].label="Retry";pbx.blfResponse=200;engine.setExpansionKeys(keys);QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Idle);
        pbx.notifyBlf("2001","<broken");QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Error);
    }
    void wrongExtensionCannotTurnTheLampGreen() {
        QTemporaryDir temporary;MiniPbx pbx;IsolatedSipEngine engine;Preferences p;
        p.inputDevice=dummyMicTone();p.outputDevice=dummySpeaker();QVERIFY(engine.start(p,temporary.path(),true));QVERIFY(engine.addAccount(account("1001",pbx)));
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("1001"),6000);auto keys=p.expansionKeys;keys[0]={"blf","Office","1001","2001"};engine.setExpansionKeys(keys);
        QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Idle);
        auto body=document(1);body.replace("2001@localhost","9999@localhost");pbx.notifyBlf("2001",body);
        QTRY_COMPARE(engine.blfInfo(0).state,BlfState::Error);QVERIFY(engine.blfInfo(0).detail.contains("different extension"));
    }
};
QTEST_GUILESS_MAIN(BlfTests)
#include "test_blf.moc"
