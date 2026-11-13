#include "isolatedsipengine.h"
#include "minipbx.h"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <memory>

using namespace stel;
class MessageTests:public QObject {
    Q_OBJECT
    static Account account(const QString &id,const MiniPbx &pbx){
        Account a;a.id=a.username=id;a.domain="localhost";a.transport="udp";a.server=pbx.server();a.ice=false;return a;
    }
private slots:
    void init(){IsolatedSipEngine::resetPortPools();}
    void initTestCase(){const auto prefix=qEnvironmentVariable("STEL_RESOURCE_PREFIX");if(!prefix.isEmpty())SipEngine::configureResources(prefix);}
    void persianMessagesOverSip_data(){QTest::addColumn<bool>("sameCore");QTest::newRow("two-accounts-one-core")<<true;QTest::newRow("two-independent-cores")<<false;}
    void persianMessagesOverSip(){
        QFETCH(bool,sameCore);QTemporaryDir tmp;MiniPbx pbx;QVector<MessageInfo> events;IsolatedSipEngine sender,other;
        QVERIFY(sender.start({},tmp.path()+"/a",true));QVERIFY(other.start({},tmp.path()+"/b",true));
        auto &receiver=sameCore?sender:other;
        auto alice=account("alice",pbx);alice.callerIdName=QString::fromUtf8("مهر داد");QVERIFY(sender.addAccount(alice));QVERIFY(receiver.addAccount(account("bob",pbx)));
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("alice")&&pbx.registered("bob"),6000);
        connect(&sender,&SipEngine::messageChanged,this,[&](const MessageInfo &m){events<<m;});
        if(!sameCore)connect(&receiver,&SipEngine::messageChanged,this,[&](const MessageInfo &m){events<<m;});
        const auto body=QString::fromUtf8("سلام! پیام فارسی می‌فرستم 👋\nSIP 62223 → 62224 / test@example.com\nعدد ۱۲۳۴ و 5678 <b>متن ساده</b>");
        const auto id=sender.sendMessage("alice","bob",body);QVERIFY2(!id.isEmpty(),qPrintable(sender.lastError()));
        auto sent=[&]{for(const auto &m:events)if(m.id==id&&m.state=="sent")return true;return false;};
        auto received=[&]{for(const auto &m:events)if(m.incoming&&m.accountId=="bob"&&m.text==body)return true;return false;};
        QTRY_VERIFY_WITH_TIMEOUT(sent()&&received(),7000);QCOMPARE(pbx.messageBodies.size(),1);QCOMPARE(pbx.messageBodies.first(),body.toUtf8());
        QVERIFY(pbx.messageContentTypes.first().contains("text/plain"));QVERIFY(pbx.messageContentTypes.first().toLower().contains("utf-8"));
        auto *from=linphone_factory_create_address(linphone_factory_get(),pbx.messageFrom.first().constData());QVERIFY(from);
        QCOMPARE(QString::fromUtf8(linphone_address_get_username(from)),QString("alice"));linphone_address_unref(from);
        const auto reply=QString::fromUtf8("پاسخ به پیام شما — received ✅");const auto replyId=receiver.sendMessage("bob","alice",reply);QVERIFY(!replyId.isEmpty());
        auto replied=[&]{bool received=false,sent=false;for(const auto &m:events){received|=m.incoming&&m.accountId=="alice"&&m.text==reply;sent|=m.id==replyId&&m.state=="sent";}return received&&sent;};
        QTRY_VERIFY_WITH_TIMEOUT(replied(),7000);
        for(const auto &m:events)if(m.incoming){QVERIFY(m.id.startsWith("in-"));QVERIFY(m.timestamp.isValid());QVERIFY(m.unread);}
    }
    void failureAndValidation(){
        QTemporaryDir tmp;MiniPbx pbx;IsolatedSipEngine e;QVERIFY(e.start({},tmp.path(),true));QVERIFY(e.addAccount(account("alice",pbx)));
        QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("alice"),6000);QSignalSpy changed(&e,&SipEngine::messageChanged);
        const auto id=e.sendMessage("alice","missing",QString::fromUtf8("این پیام رد می‌شود"));QVERIFY(!id.isEmpty());
        auto failure=[&]{for(const auto &args:changed){const auto m=qvariant_cast<MessageInfo>(args[0]);if(m.id==id&&m.state=="failed")return m;}return MessageInfo{};};
        QTRY_VERIFY_WITH_TIMEOUT(!failure().id.isEmpty(),6000);QCOMPARE(pbx.rejectedMessages,1);QVERIFY(!failure().error.isEmpty());
        const int requests=pbx.messageBodies.size();
        QVERIFY(e.sendMessage("missing","alice","test").isEmpty());QVERIFY(e.sendMessage("alice","bob","  \n ").isEmpty());
        QVERIFY(e.sendMessage("alice","bob\r\nX: test","body").isEmpty());QVERIFY(e.sendMessage("alice","bob",QString::fromUtf8("a\0b",3)).isEmpty());
        QVERIFY(e.sendMessage("alice","bob",QString(32769,'a')).isEmpty());QTest::qWait(100);QCOMPARE(pbx.messageBodies.size(),requests);
    }
    void historyRoundTripAndCorruption(){
        QTemporaryDir tmp;Store store(tmp.path());MessageInfo m;m.id="one";m.accountId="alice";m.peer="sip:bob@localhost";m.incoming=true;m.unread=true;
        m.text=QString::fromUtf8("می‌توانم متن فارسی، نیم‌فاصله و emoji 🙂 را ذخیره کنم\nEnglish 123");m.state="received";m.timestamp=QDateTime::currentDateTimeUtc();
        QString error;QVERIFY2(store.saveMessages({m},&error),qPrintable(error));QVector<MessageInfo> restored;QVERIFY(store.loadMessages(restored,&error));QCOMPARE(restored.size(),1);QCOMPARE(restored[0].toJson(),m.toJson());
        QFile file(tmp.path()+"/messages.json");QVERIFY(file.open(QIODevice::WriteOnly));file.write("{invalid history");file.close();
        QVERIFY(!store.loadMessages(restored,&error));QCOMPARE(restored.size(),1);QCOMPARE(restored[0].text,m.text);QVERIFY(!error.isEmpty());
    }
    void pendingSendIsInterruptedOnShutdown(){
        QTemporaryDir tmp;MiniPbx pbx;IsolatedSipEngine e;QVERIFY(e.start({},tmp.path(),true));auto a=account("alice",pbx);a.registerEnabled=false;QVERIFY(e.addAccount(a));
        QUdpSocket blackhole;QVERIFY(blackhole.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));QSignalSpy changed(&e,&SipEngine::messageChanged);
        const auto id=e.sendMessage("alice","sip:bob@127.0.0.1:"+QString::number(blackhole.localPort()),"pending");QVERIFY(!id.isEmpty());e.shutdown();
        bool interrupted=false;for(const auto &args:changed){const auto m=qvariant_cast<MessageInfo>(args[0]);interrupted|=m.id==id&&m.state=="interrupted";}
        QVERIFY(interrupted);QTest::qWait(50);
    }
};
QTEST_GUILESS_MAIN(MessageTests)
#include "test_messages.moc"
