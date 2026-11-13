#include "isolatedsipengine.h"
#include "mainwindow.h"
#include "calldialog.h"
#include "settingswidget.h"
#include "minipbx.h"
#include "chatwidget.h"
#include <QInputMethodEvent>
#include <QScrollArea>
#include <QTextOption>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QScreen>
#include <QScrollArea>
#include <QFile>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTemporaryDir>
#include <QUdpSocket>
#include <QtTest>
#include <memory>

class UiTests:public QObject {
    Q_OBJECT
    void screenshot(QWidget &widget,const QString &name) {
        const auto directory=qEnvironmentVariable("STEL_QA_SCREENSHOTS");if(directory.isEmpty())return;
        QDir().mkpath(directory);QTest::qWait(120);auto *dialog=widget.findChild<QDialog*>("settingsDialog");(dialog&&dialog->isVisible()?static_cast<QWidget*>(dialog):&widget)->grab().save(directory+"/"+name+".png");
    }
    static stel::Account direct(const QString &user) {
        stel::Account a;a.id=user;a.label=user;a.username=user;a.domain="localhost";a.registerEnabled=false;a.transport="udp";a.encryption="none";a.ice=false;return a;
    }
    static QString uri(stel::SipEngine &engine,const QString &user) {
        auto *t=linphone_core_get_transports_used(engine.native());const int port=linphone_transports_get_udp_port(t);linphone_transports_unref(t);return "sip:"+user+"@127.0.0.1:"+QString::number(port);
    }
    static bool streams(stel::SipEngine &e){for(const auto &i:e.calls())if(i.rawState==LinphoneCallStreamsRunning)return true;return false;}
    static stel::CallDialog *dialog(stel::MainWindow &w,bool incoming) {
        for(auto *d:w.findChildren<stel::CallDialog*>())if(d->callInfo().incoming==incoming&&!stel::callHasEnded(d->callInfo().rawState))return d;
        return nullptr;
    }
    static QPushButton *button(QWidget &w,const char *name){return w.findChild<QPushButton*>(name);}
    void captureCall(stel::CallDialog &d,const QString &name) {
        const auto directory=qEnvironmentVariable("STEL_QA_SCREENSHOTS");if(directory.isEmpty())return;QDir().mkpath(directory);QTest::qWait(100);
        if(name.startsWith("call-video")&&QApplication::platformName()=="xcb"){d.raise();d.activateWindow();QTest::qWait(200);const auto pos=d.mapToGlobal(QPoint(0,0));d.screen()->grabWindow(0,pos.x(),pos.y(),d.width(),d.height()).save(directory+"/"+name+".png");}
        else d.grab().save(directory+"/"+name+".png");
    }
private slots:
    void init(){IsolatedSipEngine::resetPortPools();}
    void messagesChatPersianAndRestart(){
        QTemporaryDir tmp;MiniPbx pbx;stel::Store store(tmp.path());stel::Preferences prefs;
        prefs.inputDevice=stel::dummyMicSilence();prefs.outputDevice=stel::dummySpeaker();QVector<stel::Account> accounts;
        for(const auto &id:{"alice","bob"}){auto a=direct(id);a.server=pbx.server();a.registerEnabled=true;a.password="message-ui-test-secret";accounts<<a;}
        QString error;QVERIFY2(store.save(accounts,prefs,&error),qPrintable(error));
        const auto body=QString::fromUtf8("سلام، می‌توانم فارسی تایپ کنم 👋\nSIP 62223 / شماره ۱۲۳۴ <b>متن ساده</b>");
        const auto reply=QString::fromUtf8("بله، پیام شما رسید ✅ — received 123");
        {
            stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();QCOMPARE(window.layoutDirection(),Qt::LeftToRight);
            QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("alice")&&pbx.registered("bob"),6000);
            auto *tabs=window.findChild<QTabWidget*>("mainPages");QCOMPARE(tabs->tabText(0),QString("Phone"));QCOMPARE(tabs->tabText(1),QString("Messages"));tabs->setCurrentIndex(1);
            auto *page=window.findChild<stel::ChatWidget*>();QVERIFY(page);QCOMPARE(page->layoutDirection(),Qt::LeftToRight);
            auto *account=page->findChild<QComboBox*>("messageAccount");auto *recipient=page->findChild<QLineEdit*>("messageRecipient");
            auto *composer=page->findChild<QPlainTextEdit*>("messageComposer");QVERIFY(composer);QCOMPARE(composer->document()->defaultTextOption().textDirection(),Qt::LayoutDirectionAuto);
            account->setCurrentIndex(account->findData("alice"));recipient->setText("bob");composer->setFocus();
            QInputMethodEvent input;input.setCommitString(body);QApplication::sendEvent(composer,&input);QCOMPARE(composer->toPlainText(),body);
            QTest::keyClick(composer,Qt::Key_Return,Qt::ShiftModifier);QCOMPARE(composer->toPlainText(),body+'\n');
            QTest::keyClick(composer,Qt::Key_Return);QVERIFY(composer->toPlainText().isEmpty());
            QVector<stel::MessageInfo> messages;auto load=[&]{messages.clear();return store.loadMessages(messages)&&messages.size()==2;};
            QTRY_VERIFY_WITH_TIMEOUT(load(),6000);QCOMPARE(pbx.messageBodies.first(),(body+'\n').toUtf8());QCOMPARE(page->unreadCount(),1);
            auto *list=page->findChild<QListWidget*>("messageConversations");QCOMPARE(list->count(),2);QListWidgetItem *bob=nullptr;
            for(int n=0;n<list->count();++n)if(list->item(n)->data(Qt::UserRole).toString()=="bob")bob=list->item(n);
            QVERIFY(bob);list->setCurrentItem(bob);QCOMPARE(account->currentData().toString(),QString("bob"));QCOMPARE(page->unreadCount(),0);
            auto *timeline=page->findChild<QScrollArea*>("messageTimeline");auto bodies=timeline->widget()->findChildren<QLabel*>("messageBody");QCOMPARE(bodies.size(),1);
            QCOMPARE(bodies[0]->text(),body+'\n');QCOMPARE(bodies[0]->textFormat(),Qt::PlainText);
            composer->setPlainText(reply);QTest::keyClick(composer,Qt::Key_Return);QTRY_COMPARE_WITH_TIMEOUT(pbx.messageBodies.size(),2,6000);
            QTRY_VERIFY_WITH_TIMEOUT(store.loadMessages(messages)&&messages.size()==4,6000);QCOMPARE(page->unreadCount(),1);
            QVERIFY(pbx.messageFrom.last().contains("bob@"));screenshot(window,"messages-persian-chat");
            account->setCurrentIndex(account->findData("alice"));recipient->setText("bob");const QString oversized(32769,'x');composer->setPlainText(oversized);
            const int accepted=pbx.messageBodies.size();button(*page,"sendMessage")->click();QCOMPARE(composer->toPlainText(),oversized);QCOMPARE(pbx.messageBodies.size(),accepted);
            recipient->setText("missing");composer->setPlainText("This recipient does not exist");button(*page,"sendMessage")->click();
            auto failed=[&]{if(!store.loadMessages(messages))return false;for(const auto &m:messages)if(m.state=="failed")return true;return false;};
            QTRY_VERIFY_WITH_TIMEOUT(failed(),6000);QTRY_VERIFY_WITH_TIMEOUT(timeline->widget()->findChild<QPushButton*>("retryMessage"),3000);
            const int before=pbx.messageBodies.size();timeline->widget()->findChild<QPushButton*>("retryMessage")->click();QTRY_COMPARE_WITH_TIMEOUT(pbx.messageBodies.size(),before+1,6000);
            tabs->setCurrentIndex(0);auto *engine=window.findChild<stel::SipEngine*>();QVERIFY(engine);
            QVERIFY(!engine->sendMessage("bob","alice",reply).isEmpty());
            auto unreadWhileCalling=[&]{if(!store.loadMessages(messages))return false;int count=0;for(const auto &m:messages)count+=m.incoming;return count==3&&page->unreadCount()>=1;};
            QTRY_VERIFY_WITH_TIMEOUT(unreadWhileCalling(),6000);QCOMPARE(tabs->currentIndex(),0);
        }
        {
            stel::MainWindow restarted(tmp.path());QVERIFY(restarted.ready());restarted.show();auto *page=restarted.findChild<stel::ChatWidget*>();QVERIFY(page->unreadCount()>=1);
            QVector<stel::MessageInfo> messages;QVERIFY(store.loadMessages(messages));int incoming=0;
            for(const auto &m:messages)if(m.incoming){++incoming;QVERIFY(m.text==body+'\n'||m.text==reply);}
            QCOMPARE(incoming,3);
        }
        QVERIFY(store.save({},prefs));
    }
    void aboutShowsReleaseAndCredits() {
        QTemporaryDir tmp;stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();window.showSettings();
        auto *pages=window.findChild<QTabWidget*>("settingsPages");QVERIFY(pages);auto *page=window.findChild<QWidget*>("aboutPage");QVERIFY(page);
        pages->setCurrentWidget(page);QCOMPARE(pages->tabText(pages->currentIndex()),QString("About"));
        QCOMPARE(page->findChild<QLabel*>("aboutVersion")->text(),QString("Stel ")+STEL_VERSION);
        auto *details=page->findChild<QTextBrowser*>("aboutDetails");QVERIFY(details);const auto text=details->toPlainText();
        for(const auto &credit:{"Mehrdad Seifzadeh","mehrdad.seifzadeh@gmail.com","GNU GPL","AGPL","QtKeychain","Linphone SDK"})QVERIFY(text.contains(credit));
        QVERIFY(details->toHtml().contains("https://github.com/seifzadeh/stel"));
        QVERIFY(page->findChild<QTextBrowser*>("licenseGPL-3.0")->toPlainText().contains("GNU GENERAL PUBLIC LICENSE"));
        QVERIFY(page->findChild<QTextBrowser*>("licenseAGPL-3.0")->toPlainText().contains("GNU AFFERO GENERAL PUBLIC LICENSE"));
        screenshot(window,"stel-about");
    }
    void initTestCase(){if(qEnvironmentVariableIsSet("STEL_VERBOSE_TESTS"))linphone_logging_service_set_log_level(linphone_logging_service_get(),LinphoneLogLevelMessage);}
    void answerAnotherAccountInSameWindow_data(){QTest::addColumn<bool>("video");QTest::newRow("audio")<<false;QTest::newRow("video")<<true;}
    void answerAnotherAccountInSameWindow() {
        QFETCH(bool,video);
        QTemporaryDir tmp;MiniPbx pbx;stel::Store store(tmp.path());stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();p.videoDevice=stel::dummyCamera();
        QVector<stel::Account> accounts;
        for(const auto &user:{"alice","bob"}){stel::Account a;a.id=user;a.username=user;a.domain="localhost";a.server=pbx.server();a.password="ui-local-call-secret";a.transport="udp";a.encryption="srtp";a.ice=true;accounts<<a;}
        QString error;QVERIFY2(store.save(accounts,p,&error),qPrintable(error));
        {
            stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("alice")&&pbx.registered("bob"),6000);
            auto *account=window.findChild<QComboBox*>("callAccount");account->setCurrentIndex(account->findData("alice"));window.findChild<QLineEdit*>("dialTarget")->setText("bob");
            auto button=[&](const QString &text)->QPushButton*{for(auto *b:window.findChildren<QPushButton*>())if(b->text()==text)return b;return nullptr;};
            QVERIFY(button(video?"Video call":"Call"));QTest::mouseClick(button(video?"Video call":"Call"),Qt::LeftButton);auto *calls=window.findChild<QListWidget*>("activeCalls");QVERIFY(calls);
            QTRY_COMPARE_WITH_TIMEOUT(calls->count(),2,6000);QListWidgetItem *incoming=nullptr;
            for(int n=0;n<calls->count();++n)if(calls->item(n)->data(Qt::UserRole+2).toBool())incoming=calls->item(n);
            QVERIFY(incoming);QCOMPARE(incoming->data(Qt::UserRole+1).toString(),QString("bob"));calls->setCurrentItem(incoming);
            auto *in=dialog(window,true),*out=dialog(window,false);QVERIFY(in&&out&&in!=out);QVERIFY(in->isVisible()&&out->isVisible());
            QCOMPARE(in->callInfo().accountId,QString("bob"));QVERIFY(in->findChild<QLabel*>("callerAddress")->text().contains("alice"));
            captureCall(*in,video?"call-incoming-video-offer":"call-incoming");QVERIFY(UiTests::button(*in,video?"callAnswerVideo":"callAnswer")->isEnabled());QTest::mouseClick(UiTests::button(*in,video?"callAnswerVideo":"callAnswer"),Qt::LeftButton);
            auto running=[&]{int count=0;for(int n=0;n<calls->count();++n)count+=calls->item(n)->data(Qt::UserRole+3).toInt()==LinphoneCallStreamsRunning;return count==2;};
            QTRY_VERIFY_WITH_TIMEOUT(running(),10000);
            if(video){
                for(auto *d:{in,out})QTRY_VERIFY_WITH_TIMEOUT(d->findChild<QWidget*>("callRemoteVideo")->property("hasVideoFrame").toBool(),8000);
                QVERIFY(in->findChild<QWidget*>("callRemoteVideo")->winId()!=out->findChild<QWidget*>("callRemoteVideo")->winId());
            }
            captureCall(*out,video?"call-two-accounts-video":"call-connected");screenshot(window,"two-accounts-connected");
            // ICE's late re-INVITE can temporarily disable controls after the
            // first video frame. Activate the button once negotiation settles.
            auto *mute=UiTests::button(*out,"callMute");QTRY_VERIFY_WITH_TIMEOUT(running()&&mute->isEnabled(),6000);
            mute->click();QTRY_VERIFY_WITH_TIMEOUT(out->callInfo().muted,3000);QVERIFY(!in->callInfo().muted);
            QTRY_VERIFY_WITH_TIMEOUT(running()&&mute->isEnabled(),6000);mute->click();QTRY_VERIFY_WITH_TIMEOUT(!out->callInfo().muted,3000);
            bool asked=false;QTimer::singleShot(0,out,[&]{auto *q=out->findChild<QMessageBox*>("confirmCloseCall");QVERIFY(q);asked=true;QCOMPARE(q->defaultButton(),q->button(QMessageBox::No));out->reject();QVERIFY(q->isVisible());screenshot(*q,"confirm-close-call");q->button(QMessageBox::No)->click();});
            QVERIFY(!out->close());QVERIFY(asked);QVERIFY(out->isVisible());QVERIFY(window.isVisible());QTRY_VERIFY_WITH_TIMEOUT(running(),6000);
            QPointer<stel::CallDialog> closed=out;asked=false;QTimer::singleShot(0,out,[&]{auto *q=out->findChild<QMessageBox*>("confirmCloseCall");QVERIFY(q);asked=true;q->button(QMessageBox::Yes)->click();});
            QVERIFY(out->close());QVERIFY(asked);QVERIFY(!closed||!closed->isVisible());QTRY_COMPARE_WITH_TIMEOUT(calls->count(),0,6000);
            QCOMPARE(store.history().size(),2);for(const auto &entry:store.history())QVERIFY(entry["state"].toString()!="LinphoneCallReleased");
        }
        QVERIFY(store.save({},p,&error));
    }
    void closeCallConfirmation_data(){
        QTest::addColumn<QString>("state");QTest::addColumn<QString>("trigger");
        QTest::newRow("connected-escape")<<QString("connected")<<QString("escape");
        QTest::newRow("connected-close-button")<<QString("connected")<<QString("button");
        QTest::newRow("held-window-close")<<QString("held")<<QString("window");
        QTest::newRow("incoming-ringing")<<QString("incoming")<<QString("window");
        QTest::newRow("outgoing-ringing")<<QString("outgoing")<<QString("window");
    }
    void closeCallConfirmation(){
        QFETCH(QString,state);QFETCH(QString,trigger);QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));
        stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();auto *e=w.findChild<stel::SipEngine*>();IsolatedSipEngine a;QVERIFY(a.start(p,tmp.path()+"/a",true));QVERIFY(a.addAccount(direct("alice")));
        if(state!="incoming"&&state!="outgoing")connect(&a,&stel::SipEngine::callChanged,&a,[&](const stel::CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&a,[&,id=i.id]{a.answer(id,false);});});
        if(state=="incoming")QVERIFY(a.dial("alice",uri(*e,"bob")));else QVERIFY(e->dial("bob",uri(a,"alice")));
        QTRY_VERIFY_WITH_TIMEOUT(dialog(w,state=="incoming"),6000);QPointer<stel::CallDialog> d=dialog(w,state=="incoming");
        if(state=="connected"||state=="held")QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallStreamsRunning,6000);
        if(state=="held"){QVERIFY(e->hold(d->callInfo().id,true));QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallPaused,6000);}
        if(state=="outgoing")QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallOutgoingRinging,6000);
        auto requestClose=[&]{if(trigger=="escape")QTest::keyClick(d,Qt::Key_Escape);else if(trigger=="button")button(*d,"callClose")->click();else d->close();};
        bool asked=false;QTimer::singleShot(0,d,[&]{auto *q=d->findChild<QMessageBox*>("confirmCloseCall");QVERIFY(q);asked=true;QCOMPARE(q->defaultButton(),q->button(QMessageBox::No));QTest::keyClick(q,Qt::Key_Escape);});
        requestClose();QVERIFY(asked);QVERIFY(d&&d->isVisible());QCOMPARE(e->calls().size(),1);QCOMPARE(a.calls().size(),1);if(state=="held")QCOMPARE(d->callInfo().rawState,LinphoneCallPaused);
        asked=false;QTimer::singleShot(0,d,[&]{auto *q=d->findChild<QMessageBox*>("confirmCloseCall");QVERIFY(q);asked=true;q->button(QMessageBox::Yes)->click();});
        requestClose();QVERIFY(asked);QVERIFY(!d||!d->isVisible());QTRY_VERIFY_WITH_TIMEOUT(e->calls().isEmpty()&&a.calls().isEmpty(),6000);QVERIFY(w.isVisible());
    }
    void peerEndsCallWhileCloseQuestionIsOpen(){
        QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));
        stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();auto *e=w.findChild<stel::SipEngine*>();IsolatedSipEngine a;QVERIFY(a.start(p,tmp.path()+"/a",true));QVERIFY(a.addAccount(direct("alice")));
        connect(&a,&stel::SipEngine::callChanged,&a,[&](const stel::CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&a,[&,id=i.id]{a.answer(id,false);});});
        QVERIFY(e->dial("bob",uri(a,"alice")));QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*e),6000);QPointer<stel::CallDialog> d=dialog(w,false);QVERIFY(d);bool asked=false;
        QTimer::singleShot(0,d,[&]{auto *q=d->findChild<QMessageBox*>("confirmCloseCall");QVERIFY(q);asked=true;QTimer::singleShot(30,&a,[&]{for(const auto &i:a.calls())a.hangup(i.id);});});
        QVERIFY(d->close());QVERIFY(asked);QVERIFY(!d||!d->isVisible());QTRY_VERIFY_WITH_TIMEOUT(e->calls().isEmpty()&&a.calls().isEmpty(),6000);QVERIFY(w.isVisible());
    }
    void endedCallClosesWithoutQuestion(){
        QTemporaryDir tmp;IsolatedSipEngine e;QVERIFY(e.start({},tmp.path(),true));stel::CallInfo info;info.id="ended";info.rawState=LinphoneCallEnd;stel::CallDialog d(e,info);d.setAttribute(Qt::WA_DeleteOnClose,false);d.show();
        bool asked=false;QTimer::singleShot(0,&d,[&]{if(auto *q=d.findChild<QMessageBox*>("confirmCloseCall")){asked=true;q->button(QMessageBox::No)->click();}});
        QVERIFY(d.close());QTest::qWait(30);QVERIFY(!asked);QVERIFY(!d.isVisible());
    }
    void incomingCallActions_data() {
        QTest::addColumn<QString>("action");
        for(const auto &action:{"reject","forward","ring-transfer","blind-transfer","answer"})QTest::newRow(action)<<QString(action);
    }
    void incomingCallActions() {
        QFETCH(QString,action);QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();
        stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();
        auto *engine=w.findChild<stel::SipEngine*>();QVERIFY(engine);IsolatedSipEngine a,c;
        QVERIFY(a.start(p,tmp.path()+"/a",true));QVERIFY(c.start(p,tmp.path()+"/c",true));auto alice=direct("alice");alice.label="Local account label";alice.callerIdName="Alice Test";
        QVERIFY(a.addAccount(alice));QVERIFY(c.addAccount(direct("charlie")));
        connect(&c,&stel::SipEngine::callChanged,&c,[&](const stel::CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&c,[&,id=i.id]{c.answer(id,false);});});
        QSignalSpy done(&a,&stel::SipEngine::callFinished);QVERIFY(a.dial("alice",uri(*engine,"bob")));QTRY_VERIFY_WITH_TIMEOUT(dialog(w,true),6000);auto *d=dialog(w,true);QVERIFY(d->isVisible());
        QCOMPARE(d->findChild<QLabel*>("callerName")->text(),QString("Alice Test"));QVERIFY(d->findChild<QLabel*>("callAccountDetails")->text().contains("bob"));
        if(action=="reject") {QTest::mouseClick(button(*d,"callHangup"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(),5000);QVERIFY(!streams(a));}
        else if(action=="forward"||action=="ring-transfer"||action=="blind-transfer") {
            if(action=="blind-transfer"){QTest::mouseClick(button(*d,"callAnswer"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*engine),8000);}
            QTest::mouseClick(button(*d,action=="forward"?"callForward":action=="ring-transfer"?"callIncomingTransfer":"callTransfer"),Qt::LeftButton);
            auto *scroll=d->findChild<QScrollArea*>();auto *panel=d->findChild<QWidget*>("callRoutePanel");
            auto panelVisible=[&]{return scroll->viewport()->rect().contains(QRect(panel->mapTo(scroll->viewport(),QPoint(0,0)),panel->size()));};QTRY_VERIFY_WITH_TIMEOUT(panelVisible(),2000);
            auto *target=d->findChild<QLineEdit*>("callRouteTarget");QVERIFY(target->isVisible());target->setText(uri(c,"charlie"));captureCall(*d,"call-"+action);
            QTest::mouseClick(button(*d,"callRouteApply"),Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(c)&&engine->calls().isEmpty(),10000);
            // A 302 retains the original remote URI in this SDK. Verify the new
            // endpoint identity and bidirectional media instead.
            bool found=false;for(const auto &i:c.calls())found|=i.incoming&&i.remote.contains("alice");QVERIFY(found);
            auto media=[&]{double downA=0,downC=0;for(const auto &i:a.calls())downA+=i.audioDownKbps;for(const auto &i:c.calls())downC+=i.audioDownKbps;return downA>0&&downC>0;};
            QTRY_VERIFY_WITH_TIMEOUT(media(),6000);
        } else {
            int digit=0;auto *cb=linphone_factory_create_core_cbs(linphone_factory_get());linphone_core_cbs_set_user_data(cb,&digit);
            linphone_core_cbs_set_dtmf_received(cb,[](LinphoneCore *lc,LinphoneCall *,int value){*static_cast<int*>(linphone_core_cbs_get_user_data(linphone_core_get_current_callbacks(lc)))=value;});linphone_core_add_callbacks(a.native(),cb);
            QTest::mouseClick(button(*d,"callAnswer"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*engine),8000);
            QTest::mouseClick(button(*d,"callDtmf_5"),Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(digit,int('5'),5000);QCOMPARE(d->findChild<QLineEdit*>("callDtmfDisplay")->text(),QString("5"));
            QTest::mouseClick(button(*d,"callHold"),Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallPaused,6000);QVERIFY(!button(*d,"callDtmf_5")->isEnabled());
            QTest::mouseClick(button(*d,"callHold"),Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallStreamsRunning,6000);
            QTest::mouseClick(button(*d,"callRecord"),Qt::LeftButton);QVERIFY(d->callInfo().recording);QTest::qWait(350);QTest::mouseClick(button(*d,"callRecord"),Qt::LeftButton);QVERIFY(!d->callInfo().recording);
            linphone_core_remove_callbacks(a.native(),cb);linphone_core_cbs_unref(cb);
            QTest::mouseClick(button(*d,"callHangup"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(),5000);
            const auto files=QDir(store.directory()+"/recordings").entryList({"*.wav"},QDir::Files);QVERIFY(!files.isEmpty());QVERIFY(QFileInfo(store.directory()+"/recordings/"+files[0]).size()>44);
        }
        for(const auto &i:a.calls())a.hangup(i.id);
        QTRY_VERIFY_WITH_TIMEOUT(a.calls().isEmpty()&&c.calls().isEmpty(),6000);
    }
    void failedOutgoingCallShowsReason() {
        QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));
        stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();IsolatedSipEngine a;p.dnd=true;QVERIFY(a.start(p,tmp.path()+"/a",true));QVERIFY(a.addAccount(direct("alice")));
        auto *target=w.findChild<QLineEdit*>("dialTarget");target->setText(uri(a,"alice"));QTest::keyClick(target,Qt::Key_Return);QPointer<stel::CallDialog> d=w.findChild<stel::CallDialog*>();QVERIFY(d&&d->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(d->callInfo().rawState,LinphoneCallError,5000);QVERIFY(!d->callInfo().error.isEmpty());QVERIFY(d->findChild<QLabel*>("callActionResult")->isVisible());QVERIFY(!button(*d,"callHangup")->isEnabled());QVERIFY(w.isVisible());captureCall(*d,"call-failed");
        QTRY_COMPARE_WITH_TIMEOUT(w.findChild<QListWidget*>("activeCalls")->count(),0,5000);
    }
    void videoInCallDialog_data() {
        QTest::addColumn<bool>("incoming");QTest::newRow("incoming")<<true;QTest::newRow("outgoing")<<false;
    }
    void videoInCallDialog() {
        QFETCH(bool,incoming);QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();p.videoDevice=stel::dummyCamera();
        stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();auto *engine=w.findChild<stel::SipEngine*>();QVERIFY(engine);
        // The two independent loopback endpoints must not allocate overlapping
        // RTP/RTCP ports; unlike real remote hosts they share this machine's IP.
        auto peerPrefs=p;peerPrefs.audioPortMin=12000;peerPrefs.audioPortMax=12098;peerPrefs.videoPortMin=13000;peerPrefs.videoPortMax=13098;
        IsolatedSipEngine a;QVERIFY(a.start(peerPrefs,tmp.path()+"/a",true));QVERIFY(a.addAccount(direct("alice")));
        connect(&a,&stel::SipEngine::callChanged,&a,[&](const stel::CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&a,[&,id=i.id]{a.answer(id,true);});});
        if(incoming){QVERIFY(a.dial("alice",uri(*engine,"bob"),true));QTRY_VERIFY_WITH_TIMEOUT(dialog(w,true),6000);auto *d=dialog(w,true);QVERIFY(button(*d,"callAnswerVideo")->isVisible());QTest::mouseClick(button(*d,"callAnswerVideo"),Qt::LeftButton);}
        else {w.findChild<QLineEdit*>("dialTarget")->setText(uri(a,"alice"));QPushButton *video=nullptr;for(auto *b:w.findChildren<QPushButton*>())if(b->text()=="Video call")video=b;QVERIFY(video);QTest::mouseClick(video,Qt::LeftButton);}
        QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*engine),8000);auto *d=dialog(w,incoming);QVERIFY(d);QTRY_VERIFY_WITH_TIMEOUT(d->callInfo().video&&d->callInfo().videoDownKbps>0,8000);
        QCOMPARE(d->findChild<QTabWidget*>("callMediaTabs")->currentIndex(),1);
        for(const auto &name:{"callRemoteVideo","callPreviewVideo"})QTRY_VERIFY_WITH_TIMEOUT(d->findChild<QWidget*>(name)->property("hasVideoFrame").toBool(),8000);
        const auto painted=d->findChild<QWidget*>("callRemoteVideo")->grab().toImage();int coloured=0;
        for(int y=0;y<painted.height();y+=5)for(int x=0;x<painted.width();x+=5){const auto colour=painted.pixel(x,y);coloured+=qMax(qRed(colour),qMax(qGreen(colour),qBlue(colour)))-qMin(qRed(colour),qMin(qGreen(colour),qBlue(colour)))>90;}
        QVERIFY(coloured>100);auto *calls=linphone_core_get_calls(engine->native());QVERIFY(calls);
        auto *native=static_cast<LinphoneCall*>(calls->data);QCOMPARE(reinterpret_cast<quintptr>(linphone_call_get_native_video_window_id(native)),quintptr(d->findChild<QWidget*>("callRemoteVideo")->winId()));
        const auto decoded=tmp.path()+"/decoded.jpg";QCOMPARE(linphone_call_take_video_snapshot(native,decoded.toUtf8().constData()),0);QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(decoded).size()>1000,8000);QVERIFY(!QImage(decoded).isNull());QTest::qWait(300);
        captureCall(*d,incoming?"call-video-incoming":"call-video-outgoing");
        qInfo()<<"Video renderer:"<<linphone_core_get_video_display_filter(engine->native())<<"default:"<<linphone_core_get_default_video_display_filter(engine->native());
        QTest::mouseClick(button(*d,"callHangup"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(a.calls().isEmpty()&&engine->calls().isEmpty(),6000);
    }
    void consultationActions_data() {
        QTest::addColumn<bool>("conference");QTest::newRow("attended-transfer")<<false;QTest::newRow("conference")<<true;
    }
    void consultationActions() {
        QFETCH(bool,conference);QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();
        stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({direct("bob")},p));stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();auto *engine=w.findChild<stel::SipEngine*>();QVERIFY(engine);
        IsolatedSipEngine a,c;QVERIFY(a.start(p,tmp.path()+"/a",true));QVERIFY(c.start(p,tmp.path()+"/c",true));QVERIFY(a.addAccount(direct("alice")));QVERIFY(c.addAccount(direct("charlie")));
        for(auto *e:{&a,&c})connect(e,&stel::SipEngine::callChanged,e,[e](const stel::CallInfo &i){if(i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,e,[e,id=i.id]{e->answer(id,false);});});
        auto *target=w.findChild<QLineEdit*>("dialTarget");target->setText(uri(a,"alice"));QTest::keyClick(target,Qt::Key_Return);QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*engine),8000);
        auto *original=dialog(w,false);QVERIFY(original);const auto originalId=original->callInfo().id;
        QTest::mouseClick(button(*original,"callAddCall"),Qt::LeftButton);QTRY_COMPARE_WITH_TIMEOUT(original->callInfo().rawState,LinphoneCallPaused,6000);QTRY_VERIFY_WITH_TIMEOUT(target->text().isEmpty(),3000);
        target->setText(uri(c,"charlie"));QTest::keyClick(target,Qt::Key_Return);QTRY_VERIFY_WITH_TIMEOUT(streams(c)&&streams(*engine),8000);
        QTest::mouseClick(button(*original,conference?"callConference":"callAttendedTransfer"),Qt::LeftButton);
        auto *scroll=original->findChild<QScrollArea*>();auto *panel=original->findChild<QWidget*>("callPeerPanel");
        auto panelVisible=[&]{return scroll->viewport()->rect().contains(QRect(panel->mapTo(scroll->viewport(),QPoint(0,0)),panel->size()));};QTRY_VERIFY_WITH_TIMEOUT(panelVisible(),2000);
        auto *peers=original->findChild<QComboBox*>("callPeerChoice");QCOMPARE(peers->count(),1);QVERIFY(peers->currentData().toString()!=originalId);QVERIFY(button(*original,"callPeerApply")->isEnabled());captureCall(*original,conference?"call-conference-choice":"call-attended-choice");
        QTest::mouseClick(button(*original,"callPeerApply"),Qt::LeftButton);
        if(conference) {
            auto joined=[&]{int n=0;for(const auto &i:engine->calls())n+=i.inConference&&i.conferenceParticipants==2&&i.rawState==LinphoneCallStreamsRunning;return n==2&&streams(a)&&streams(c);};
            QTRY_VERIFY_WITH_TIMEOUT(joined(),10000);captureCall(*original,"call-conference");
            QTest::mouseClick(button(*original,"callMute"),Qt::LeftButton);for(const auto &i:engine->calls())QVERIFY(i.muted);
            QTest::mouseClick(button(*original,"callMute"),Qt::LeftButton);for(const auto &i:engine->calls())QVERIFY(!i.muted);
            QTest::mouseClick(button(*original,"callRecord"),Qt::LeftButton);QVERIFY(original->callInfo().recording);QTest::qWait(350);QTest::mouseClick(button(*original,"callRecord"),Qt::LeftButton);QVERIFY(!original->callInfo().recording);
            for(const auto &i:engine->calls())engine->hangup(i.id);
        } else {QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(c)&&engine->calls().isEmpty(),10000);bool replaced=false;for(const auto &i:c.calls())replaced|=i.remote.contains("alice");QVERIFY(replaced);}
        for(const auto &i:a.calls())a.hangup(i.id);
        QTRY_VERIFY_WITH_TIMEOUT(a.calls().isEmpty()&&c.calls().isEmpty(),6000);
    }
    void settingsCloseExitAndAuthenticatedRestart() {
        QTemporaryDir tmp;stel::Store store(tmp.path());stel::Preferences prefs;prefs.signallingAddress="127.0.0.1";
        QUdpSocket registrar;QVERIFY(registrar.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
        QUdpSocket available;QVERIFY(available.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));prefs.sipUdpPort=available.localPort();available.close();
        QSet<QString> authenticated,unregistered;int requests=0;
        auto md5=[](const QByteArray &s){return QCryptographicHash::hash(s,QCryptographicHash::Md5).toHex();};
        connect(&registrar,&QUdpSocket::readyRead,this,[&]{while(registrar.hasPendingDatagrams()) {
            QByteArray packet(int(registrar.pendingDatagramSize()),'\0');QHostAddress sender;quint16 port;registrar.readDatagram(packet.data(),packet.size(),&sender,&port);if(!packet.startsWith("REGISTER "))continue;++requests;
            QMap<QByteArray,QByteArray> headers;for(auto line:packet.split('\n')){line=line.trimmed();const int colon=line.indexOf(':');if(colon>0)headers[line.left(colon).toLower()]=line.mid(colon+1).trimmed();}
            const bool off=headers["expires"]=="0"||headers["contact"].contains("expires=0");
            QMap<QString,QString> digest;auto matches=QRegularExpression("(\\w+)=(?:\"([^\"]*)\"|([^,\\s]+))").globalMatch(QString::fromUtf8(headers["authorization"]));
            while(matches.hasNext()){const auto m=matches.next();digest[m.captured(1)]=m.captured(2).isNull()?m.captured(3):m.captured(2);}
            const auto username=digest["username"];bool valid=false;
            if(!username.isEmpty()) {
                const auto ha1=md5(username.toUtf8()+":stel.test:restart-test-secret"),ha2=md5("REGISTER:"+digest["uri"].toUtf8());
                const auto expected=md5(ha1+":stel-ui:"+digest["nc"].toUtf8()+":"+digest["cnonce"].toUtf8()+":auth:"+ha2);
                valid=digest["response"].toLatin1()==expected;
            }
            if(valid&&!off)authenticated.insert(username);
            if(valid&&off)unregistered.insert(username);
            QByteArray response=valid?"SIP/2.0 200 OK\r\n":"SIP/2.0 401 Unauthorized\r\n";
            for(const auto &key:{QByteArray("via"),QByteArray("from"),QByteArray("to"),QByteArray("call-id"),QByteArray("cseq")})response+=key+": "+headers[key]+(key=="to"?";tag=restart-test":"")+"\r\n";
            if(valid)response+="Contact: "+headers["contact"]+"\r\nExpires: "+(off?QByteArray("0"):QByteArray("600"))+"\r\n";
            else response+="WWW-Authenticate: Digest realm=\"stel.test\", nonce=\"stel-ui\", algorithm=MD5, qop=\"auth\"\r\n";
            response+="Content-Length: 0\r\n\r\n";registrar.writeDatagram(response,sender,port);
        }});
        QVector<stel::Account> accounts;
        for(const auto &username:{"alice","bob"}){stel::Account a;a.id=username;a.username=username;a.domain="localhost";a.server="sip:127.0.0.1:"+QString::number(registrar.localPort());a.transport="udp";a.encryption="none";a.ice=false;a.password="restart-test-secret";accounts<<a;}
        QString error;QVERIFY2(store.save(accounts,prefs,&error),qPrintable(error));
        QFile config(tmp.path()+"/settings.json");QVERIFY(config.open(QIODevice::ReadOnly));QVERIFY(!config.readAll().contains("restart-test-secret"));config.close();
        auto window=std::make_unique<stel::MainWindow>(tmp.path());QVERIFY(window->ready());window->show();
        auto registered=[&]{int count=0;auto *combo=window->findChild<QComboBox*>("callAccount");for(int n=0;n<combo->count();++n)count+=combo->itemText(n).contains("Registered");return count==2;};
        QTRY_VERIFY_WITH_TIMEOUT(registered(),7000);QCOMPARE(authenticated.size(),2);
        window->showSettings();auto *dialog=window->findChild<QDialog*>("settingsDialog");QVERIFY(dialog&&dialog->isVisible());
        QVERIFY(window->findChild<QCheckBox*>("rememberPassword")->isChecked());
        screenshot(*window,"accounts-two-registered");
        auto *mainPages=window->findChild<QTabWidget*>("mainPages");QCOMPARE(mainPages->count(),4);
        dialog->close();QVERIFY(window->isVisible());QVERIFY(!dialog->isVisible());QVERIFY(registered());QVERIFY(unregistered.isEmpty());
        auto close=[&](QMessageBox::StandardButton answer){QTimer::singleShot(0,window.get(),[&]{auto *question=window->findChild<QMessageBox*>("confirmExit");QVERIFY(question);screenshot(*question,"confirm-exit");question->button(answer)->click();});window->close();};
        close(QMessageBox::Cancel);QVERIFY(window->isVisible());QVERIFY(registered());
        close(QMessageBox::Yes);QVERIFY(!window->isVisible());window.reset();
        QTRY_COMPARE_WITH_TIMEOUT(unregistered.size(),2,5000);
        authenticated.clear();window=std::make_unique<stel::MainWindow>(tmp.path());QVERIFY(window->ready());window->show();
        QTRY_VERIFY_WITH_TIMEOUT(registered(),7000);QCOMPARE(authenticated.size(),2);
        window->showSettings();QCOMPARE(window->findChild<QLineEdit*>("password")->text(),QString("restart-test-secret"));
        window->findChild<QDialog*>("settingsDialog")->close();close(QMessageBox::Yes);window.reset();
        QVERIFY(requests>=8);QVERIFY(store.save({},prefs,&error)); // Remove this test's keyring entry.
    }
    void accountsLifecycleAndRegistration() {
        QTemporaryDir tmp;QUdpSocket registrar;QVERIFY(registrar.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));
        stel::Store initial(tmp.path());stel::Preferences initialPrefs;initialPrefs.signallingAddress="127.0.0.1";QVERIFY(initial.save({},initialPrefs));
        bool unregisterReceived=false,reject=false;QString sourceAddress;
        connect(&registrar,&QUdpSocket::readyRead,this,[&]{while(registrar.hasPendingDatagrams()) {
            QByteArray data;data.resize(int(registrar.pendingDatagramSize()));QHostAddress sender;quint16 port;
            registrar.readDatagram(data.data(),data.size(),&sender,&port);sourceAddress=sender.toString();
            if(!data.startsWith("REGISTER "))continue;
            QMap<QByteArray,QByteArray> h;for(auto line:data.split('\n')){line=line.trimmed();int c=line.indexOf(':');if(c>0)h[line.left(c).toLower()]=line.mid(c+1).trimmed();}
            const bool off=h["expires"]=="0"||h["contact"].contains("expires=0");unregisterReceived|=off;
            QByteArray response=reject?"SIP/2.0 403 Forbidden\r\n":"SIP/2.0 200 OK\r\n";
            for(const auto &key:{QByteArray("via"),QByteArray("from"),QByteArray("to"),QByteArray("call-id"),QByteArray("cseq")})response+=key+": "+h[key]+(key=="to"?";tag=ui-test":"")+"\r\n";
            response+="Contact: "+h["contact"]+"\r\nExpires: "+(off?QByteArray("0"):QByteArray("600"))+"\r\nContent-Length: 0\r\n\r\n";
            registrar.writeDatagram(response,sender,port);
        }});
        stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();
        QTest::mouseClick(window.findChild<QPushButton*>("openSettings"),Qt::LeftButton);
        auto *settings=window.findChild<stel::SettingsWidget*>();QVERIFY(settings);
        auto *list=settings->findChild<QListWidget*>("settingsAccounts");auto *editor=settings->findChild<stel::AccountEditor*>();
        QTest::mouseClick(settings->findChild<QPushButton*>("addAccount"),Qt::LeftButton);
        auto edit=[&](const char *name,const QString &value){auto *field=editor->findChild<QLineEdit*>(name);QVERIFY(field);field->setText(value);};
        edit("label","Office test");edit("username","alice");edit("domain","127.0.0.1");edit("server","sip:127.0.0.1:"+QString::number(registrar.localPort()));edit("password","session-secret");
        editor->findChild<QComboBox*>("transport")->setCurrentText("udp");QCOMPARE(editor->findChild<QComboBox*>("encryption")->currentText(),QString("RTP"));QCOMPARE(editor->findChild<QComboBox*>("encryption")->currentData().toString(),QString("none"));
        QTest::mouseClick(settings->findChild<QPushButton*>("saveAccount"),Qt::LeftButton);QCOMPARE(list->count(),1);
        QFile file(tmp.path()+"/settings.json");QVERIFY(file.open(QIODevice::ReadOnly));auto bytes=file.readAll();QVERIFY(!bytes.contains("session-secret"));file.close();
        QCOMPARE(QJsonDocument::fromJson(bytes).object()["accounts"].toArray()[0].toObject()["register"].toBool(),false);
        auto *dot=settings->findChild<QLabel*>("registrationDot");QVERIFY(!dot->property("registered").toBool());
        QTest::mouseClick(settings->findChild<QPushButton*>("registerAccount"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(dot->property("registered").toBool(),6000);
        QCOMPARE(QHostAddress(sourceAddress),QHostAddress("127.0.0.1"));
        QVERIFY(settings->findChild<QLabel*>("registrationStatus")->text().contains("Registered"));
        screenshot(window,"accounts-registered");
        QTest::mouseClick(settings->findChild<QPushButton*>("unregisterAccount"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(unregisterReceived,6000);QTRY_VERIFY_WITH_TIMEOUT(!dot->property("registered").toBool(),6000);
        edit("label","Renamed account");QTest::mouseClick(settings->findChild<QPushButton*>("saveAccount"),Qt::LeftButton);QVERIFY(list->item(0)->text().contains("Renamed account"));
        reject=true;QTest::mouseClick(settings->findChild<QPushButton*>("registerAccount"),Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(settings->findChild<QLabel*>("registrationStatus")->text().contains("failed"),6000);
        QVERIFY(!dot->property("registered").toBool());QVERIFY(settings->findChild<QPlainTextEdit*>("registrationResponse")->toPlainText().contains("Forbidden",Qt::CaseInsensitive));
        QTest::mouseClick(settings->findChild<QPushButton*>("removeAccount"),Qt::LeftButton);QCOMPARE(list->count(),0);QCOMPARE(window.findChild<QComboBox*>("callAccount")->count(),0);
    }
    void accountCallerOptionsSaveAndReload(){
        QTemporaryDir tmp;stel::MainWindow w(tmp.path());QVERIFY(w.ready());w.show();w.showSettings();auto *settings=w.findChild<stel::SettingsWidget*>();button(*settings,"addAccount")->click();
        auto *editor=settings->findChild<stel::AccountEditor*>();auto *autoAnswer=editor->findChild<QCheckBox*>("autoAnswer");auto *name=editor->findChild<QLineEdit*>("callerIdName");QVERIFY(autoAnswer&&name);QVERIFY(!autoAnswer->isChecked());QVERIFY(name->text().isEmpty());
        editor->findChild<QLineEdit*>("label")->setText("Office");editor->findChild<QLineEdit*>("username")->setText("alice");editor->findChild<QLineEdit*>("domain")->setText("localhost");autoAnswer->setChecked(true);name->setText(QString::fromUtf8("علی رضایی"));button(*settings,"saveAccount")->click();
        stel::Store store(tmp.path());QVector<stel::Account> accounts;stel::Preferences prefs;QVERIFY(store.load(accounts,prefs));QCOMPARE(accounts.size(),1);QVERIFY(accounts[0].autoAnswer);QCOMPARE(accounts[0].callerIdName,name->text());QCOMPARE(accounts[0].label,QString("Office"));screenshot(w,"account-auto-answer-caller-id");
        {stel::MainWindow reopened(tmp.path());QVERIFY(reopened.ready());reopened.showSettings();QVERIFY(reopened.findChild<QCheckBox*>("autoAnswer")->isChecked());QCOMPARE(reopened.findChild<QLineEdit*>("callerIdName")->text(),name->text());}
        autoAnswer->setChecked(false);name->clear();button(*settings,"saveAccount")->click();QVERIFY(store.load(accounts,prefs));QVERIFY(!accounts[0].autoAnswer);QVERIFY(accounts[0].callerIdName.isEmpty());
    }
    void incomingCallAutomaticallyAnswersAndShowsCallerName(){
        QTemporaryDir tmp;stel::Preferences p;p.inputDevice=stel::dummyMicTone();p.outputDevice=stel::dummySpeaker();auto bob=direct("bob");bob.autoAnswer=true;stel::Store store(tmp.path()+"/ui");QVERIFY(store.save({bob},p));
        stel::MainWindow w(store.directory());QVERIFY(w.ready());w.show();auto *e=w.findChild<stel::SipEngine*>();IsolatedSipEngine a;QVERIFY(a.start(p,tmp.path()+"/a",true));auto alice=direct("alice");alice.callerIdName="Support Team";QVERIFY(a.addAccount(alice));QVERIFY(a.dial("alice",uri(*e,"bob")));
        QTRY_VERIFY_WITH_TIMEOUT(streams(a)&&streams(*e),6000);auto *d=dialog(w,true);QVERIFY(d&&d->isVisible());QCOMPARE(d->findChild<QLabel*>("callerName")->text(),QString("Support Team"));QVERIFY(!button(*d,"callAnswer")->isVisible());screenshot(*d,"auto-answered-call");
        for(const auto &i:a.calls())a.hangup(i.id);
        QTRY_VERIFY_WITH_TIMEOUT(a.calls().isEmpty()&&e->calls().isEmpty(),6000);
    }
    void mediaTypeCanBeSelectedAndSaved() {
        QTemporaryDir tmp;stel::MainWindow w(tmp.path());QVERIFY(w.ready());w.show();w.showSettings();auto *settings=w.findChild<stel::SettingsWidget*>();button(*settings,"addAccount")->click();
        auto *editor=settings->findChild<stel::AccountEditor*>();auto *media=editor->findChild<QComboBox*>("encryption");QVERIFY(media->isVisible());QCOMPARE(media->currentText(),QString("RTP"));QCOMPARE(media->currentData().toString(),QString("none"));
        editor->findChild<QLineEdit*>("username")->setText("alice");editor->findChild<QLineEdit*>("domain")->setText("localhost");media->setCurrentIndex(media->findData("srtp"));button(*settings,"saveAccount")->click();
        stel::Store store(tmp.path());QVector<stel::Account> accounts;stel::Preferences prefs;QVERIFY(store.load(accounts,prefs));QCOMPARE(accounts.size(),1);QCOMPARE(accounts[0].encryption,QString("srtp"));
        media->setCurrentIndex(media->findData("none"));button(*settings,"saveAccount")->click();QVERIFY(store.load(accounts,prefs));QCOMPARE(accounts[0].encryption,QString("none"));
    }
    void dummySettingsAndNetworkPersistence() {
        QTemporaryDir tmp;stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();QTest::mouseClick(window.findChild<QPushButton*>("openSettings"),Qt::LeftButton);
        auto *settings=window.findChild<stel::SettingsWidget*>();auto *pages=settings->findChild<QTabWidget*>("settingsPages");
        pages->setCurrentIndex(1);auto *input=settings->findChild<QComboBox*>("microphoneDevice");input->setCurrentIndex(input->findData(stel::dummyMicTone()));
        auto *speaker=settings->findChild<QComboBox*>("speakerDevice");speaker->setCurrentIndex(speaker->findData(stel::dummySpeaker()));
        QTest::mouseClick(settings->findChild<QPushButton*>("applyAudio"),Qt::LeftButton);
        QVERIFY2(settings->findChild<QLabel*>("audioResult")->text().contains("applied"),qPrintable(settings->findChild<QLabel*>("audioResult")->text()));
        screenshot(window,"audio-settings");
        pages->setCurrentIndex(2);auto *camera=settings->findChild<QComboBox*>("cameraDevice");camera->setCurrentIndex(camera->findData(stel::dummyCamera()));
        QTest::mouseClick(settings->findChild<QPushButton*>("applyVideo"),Qt::LeftButton);
        QVERIFY2(settings->findChild<QLabel*>("videoResult")->text().contains("applied"),qPrintable(settings->findChild<QLabel*>("videoResult")->text()));
        screenshot(window,"video-settings");
        pages->setCurrentIndex(3);auto *signal=settings->findChild<QComboBox*>("signallingAddress");auto *media=settings->findChild<QComboBox*>("mediaAddress");signal->setCurrentIndex(signal->findData("127.0.0.1"));media->setCurrentIndex(media->findData("127.0.0.1"));
        QTest::mouseClick(settings->findChild<QPushButton*>("applyNetwork"),Qt::LeftButton);QVERIFY(settings->findChild<QLabel*>("networkResult")->text().contains("Applied"));
        screenshot(window,"network-settings");
        QVector<stel::Account> accounts;stel::Preferences prefs;stel::Store store(tmp.path());QVERIFY(store.load(accounts,prefs));QCOMPARE(prefs.inputDevice,stel::dummyMicTone());QCOMPARE(prefs.videoDevice,stel::dummyCamera());QCOMPARE(prefs.signallingAddress,QString("127.0.0.1"));QCOMPARE(prefs.mediaAddress,QString("127.0.0.1"));
    }
    void codecSelectionPriorityAndDefaults() {
        QTemporaryDir tmp;stel::MainWindow window(tmp.path());QVERIFY(window.ready());window.show();QTest::mouseClick(window.findChild<QPushButton*>("openSettings"),Qt::LeftButton);
        auto *settings=window.findChild<stel::SettingsWidget*>();auto *pages=settings->findChild<QTabWidget*>("settingsPages");pages->setCurrentIndex(1);
        auto *audio=settings->findChild<QListWidget*>("audioCodecs");QVERIFY(audio);QListWidgetItem *g729=nullptr;
        for(int n=0;n<audio->count();++n){auto *item=audio->item(n);const auto id=item->data(Qt::UserRole).toString();item->setCheckState(id.startsWith("g729/")||id.startsWith("pcma/")?Qt::Checked:Qt::Unchecked);if(id.startsWith("g729/"))g729=item;}
        if(!g729)QSKIP("SDK does not include G.729");
        audio->setCurrentItem(g729);
        while(audio->row(g729)>0)QTest::mouseClick(settings->findChild<QPushButton*>("audioCodecUp"),Qt::LeftButton);
        QTest::mouseClick(settings->findChild<QPushButton*>("audioApplyCodecs"),Qt::LeftButton);
        QVERIFY(settings->findChild<QLabel*>("audioCodecResult")->text().contains("G729/8000"));screenshot(window,"audio-codecs");
        stel::Store store(tmp.path());QVector<stel::Account> accounts;stel::Preferences prefs;QVERIFY(store.load(accounts,prefs));QVERIFY(prefs.customAudioCodecs);QCOMPARE(prefs.audioCodecs.first(),g729->data(Qt::UserRole).toString());QCOMPARE(prefs.audioCodecs.size(),2);
        {
            stel::MainWindow reopened(tmp.path());QVERIFY(reopened.ready());QTest::mouseClick(reopened.findChild<QPushButton*>("openSettings"),Qt::LeftButton);auto *restored=reopened.findChild<QListWidget*>("audioCodecs");QVERIFY(restored);QCOMPARE(restored->item(0)->data(Qt::UserRole).toString(),prefs.audioCodecs.first());QCOMPARE(restored->item(0)->checkState(),Qt::Checked);
        }
        for(int n=0;n<audio->count();++n)audio->item(n)->setCheckState(Qt::Unchecked);
        QTest::mouseClick(settings->findChild<QPushButton*>("audioApplyCodecs"),Qt::LeftButton);QVERIFY(settings->findChild<QLabel*>("audioCodecResult")->text().contains("at least one"));stel::Preferences unchanged;QVERIFY(store.load(accounts,unchanged));QCOMPARE(unchanged.audioCodecs,prefs.audioCodecs);
        QTest::mouseClick(settings->findChild<QPushButton*>("audioCodecDefaults"),Qt::LeftButton);QTest::mouseClick(settings->findChild<QPushButton*>("audioApplyCodecs"),Qt::LeftButton);QVERIFY(store.load(accounts,prefs));QVERIFY(!prefs.customAudioCodecs);QVERIFY(prefs.audioCodecs.isEmpty());
        pages->setCurrentIndex(2);auto *video=settings->findChild<QListWidget*>("videoCodecs");QVERIFY(video);QStringList checked;
        for(int n=0;n<video->count();++n){auto *item=video->item(n);if(item->data(Qt::UserRole+1).toBool()){item->setCheckState(Qt::Checked);checked<<item->data(Qt::UserRole).toString();}}
        QTest::mouseClick(settings->findChild<QPushButton*>("videoApplyCodecs"),Qt::LeftButton);QVERIFY(store.load(accounts,prefs));QVERIFY(prefs.customVideoCodecs);QCOMPARE(prefs.videoCodecs,checked);screenshot(window,"video-codecs");
    }
};
QTEST_MAIN(UiTests)
#include "test_ui.moc"
