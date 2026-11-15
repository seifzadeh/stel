// Capture the production widgets with disposable loopback SIP accounts.
#include "mainwindow.h"
#include "calldialog.h"
#include "chatwidget.h"
#include "faxwidget.h"
#include "minipbx.h"
#include <QApplication>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>
#include <QtTest>
#include <functional>
#include <stdexcept>

using namespace stel;
namespace {
void require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
void waitFor(const std::function<bool()> &condition,const char *message,int timeout=12000){
    QElapsedTimer timer;timer.start();
    while(!condition()&&timer.elapsed()<timeout)QTest::qWait(20);
    require(condition(),message);
}
template<typename T>T *child(QObject &parent,const char *name){
    auto *value=*name?parent.findChild<T*>(name):parent.findChild<T*>();require(value,*name?name:"Required widget is missing");return value;
}
CallDialog *callDialog(MainWindow &window,bool incoming){
    for(auto *dialog:window.findChildren<CallDialog*>())
        if(dialog->callInfo().incoming==incoming&&!callHasEnded(dialog->callInfo().rawState))return dialog;
    return nullptr;
}
bool running(SipEngine &engine){
    const auto calls=engine.calls();if(calls.size()!=2)return false;
    for(const auto &call:calls)if(call.rawState!=LinphoneCallStreamsRunning)return false;
    return true;
}
}

int main(int argc,char **argv){
    QApplication app(argc,argv);app.setApplicationName("Stel");
    try{
        require(argc==2,"Usage: stel_screenshots OUTPUT_DIRECTORY");
        QDir output(QString::fromLocal8Bit(argv[1]));require(output.mkpath("."),"Cannot create screenshot directory");
        QTemporaryDir state;require(state.isValid(),"Cannot create private application state");
        MiniPbx pbx;Store store(state.path());Preferences prefs;
        prefs.inputDevice=dummyMicTone();prefs.outputDevice=dummySpeaker();prefs.videoDevice=dummyCamera();
        prefs.signallingAddress=prefs.mediaAddress="127.0.0.1";
        prefs.audioPortMin=24000;prefs.audioPortMax=24098;prefs.videoPortMin=25000;prefs.videoPortMax=25098;
        QUdpSocket probe;require(probe.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)),"Cannot allocate SIP port");
        prefs.sipUdpPort=probe.localPort();probe.close();prefs.sipTcpPort=prefs.sipTlsPort=0;
        QVector<Account> accounts;
        for(const auto &user:{QString("1001"),QString("1002")}){
            Account account;account.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
            account.username=user;account.label=account.callerIdName=user=="1001"?"Office":"Support";
            account.domain="localhost";account.server=pbx.server();account.transport="udp";account.ice=false;
            account.password="stel-local-screenshot-demo";accounts<<account;
        }
        QString error;require(store.save(accounts,prefs,&error),qPrintable(error));
        require(store.saveContacts({{{"name","Office"},{"uri","1001"}},
            {{"name",QString::fromUtf8("پشتیبانی / Support")},{"uri","1002"}}}),"Cannot save demo contacts");
        MainWindow window(state.path());require(window.ready(),"Application could not start");
        window.resize(1448,1086);window.show();auto *engine=child<SipEngine>(window,"");
        auto *pages=child<QTabWidget>(window,"mainPages");
        waitFor([&]{return pbx.registered("1001")&&pbx.registered("1002");},"Demo accounts did not register");
        QJsonArray images;
        const auto capture=[&](QWidget &widget,const QString &name){
            QTest::qWait(180);const auto image=widget.grab().toImage();
            const auto filename=name+".png";require(image.save(output.filePath(filename)),"Screenshot save failed");
            QFile file(output.filePath(filename));require(file.open(QIODevice::ReadOnly),"Cannot hash screenshot");
            images.append(QJsonObject{{"file",filename},{"width",image.width()},{"height",image.height()},
                {"sha256",QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex())}});
            qInfo().noquote()<<"Captured"<<filename<<image.size();
        };
        child<QComboBox>(window,"callAccount")->setCurrentIndex(child<QComboBox>(window,"callAccount")->findData(accounts[0].id));
        child<QLineEdit>(window,"dialTarget")->setText("1002");
        require(engine->dial(accounts[0].id,"1002",false),"Cannot place demo voice call");
        waitFor([&]{return callDialog(window,true)&&callDialog(window,false);},"Incoming call window did not open");
        auto *incoming=callDialog(window,true);incoming->resize(620,780);capture(*incoming,"incoming-call");
        child<QPushButton>(*incoming,"callAnswer")->click();waitFor([&]{return running(*engine);},"Voice call did not connect");
        auto *outgoing=callDialog(window,false);outgoing->resize(620,800);
        child<QPushButton>(*outgoing,"callDtmf_5")->click();QTest::qWait(1200);capture(*outgoing,"call");
        const auto hangup=[&]{for(const auto &call:engine->calls())engine->hangup(call.id);waitFor([&]{return engine->calls().isEmpty();},"Calls did not end");QTest::qWait(2700);};
        hangup();
        require(engine->dial(accounts[0].id,"1002",true),"Cannot place demo video call");
        waitFor([&]{return callDialog(window,true);},"Incoming video call did not open");
        child<QPushButton>(*callDialog(window,true),"callAnswerVideo")->click();
        waitFor([&]{auto *dialog=callDialog(window,false);return running(*engine)&&dialog&&child<QWidget>(*dialog,"callRemoteVideo")->property("hasVideoFrame").toBool();},"Video did not render",15000);
        outgoing=callDialog(window,false);outgoing->resize(640,820);capture(*outgoing,"video-call");hangup();

        pages->setCurrentIndex(1);auto *chat=child<ChatWidget>(window,"");
        const auto message=[&](int from,const QString &to,const QString &body,int expected){
            auto *account=child<QComboBox>(*chat,"messageAccount");account->setCurrentIndex(account->findData(accounts[from].id));
            child<QLineEdit>(*chat,"messageRecipient")->setText(to);child<QPlainTextEdit>(*chat,"messageComposer")->setPlainText(body);
            child<QPushButton>(*chat,"sendMessage")->click();
            waitFor([&]{QVector<MessageInfo> history;return store.loadMessages(history)&&history.size()>=expected;},"SIP message was not delivered");
        };
        message(0,"1002",QString::fromUtf8("سلام، صدا و تصویر به‌درستی دریافت شد؟\nVoice and video test — 1001 → 1002"),2);
        message(1,"1001",QString::fromUtf8("بله، هر دو درست هستند. فکس دوصفحه‌ای را هم بفرست.\nReady to receive the fax."),4);
        message(0,"1002",QString::fromUtf8("فکس را با T.38 می‌فرستم.\nThe A4 preview is ready."),6);
        capture(window,"messages");

        pages->setCurrentIndex(2);auto *fax=child<FaxWidget>(window,"");
        child<QComboBox>(*fax,"faxAccount")->setCurrentIndex(child<QComboBox>(*fax,"faxAccount")->findData(accounts[0].id));
        child<QLineEdit>(*fax,"faxTarget")->setText("1002");child<QComboBox>(*fax,"faxSource")->setCurrentIndex(1);
        const auto letter=QString::fromUtf8(
            "Stel — Fax\n\nاز: Office / 1001\nبه: Support / 1002\nموضوع: بررسی ارسال و دریافت فکس\n\n"
            "سلام،\nاین سند برای بررسی فکس در Stel آماده شده است.\nمتن فارسی و انگلیسی در یک صفحه قرار دارند.\nنیم‌فاصله و اعداد فارسی: می‌توانیم، ۱۲۳۴۵\n\n"
            "مشخصات سند:\nاندازهٔ کاغذ: A4\nحاشیهٔ امن: ۱۰ میلی‌متر\nپروتکل ارسال: T.38\nتعداد صفحات: ۲\n\n"
            "Stel fax check\nPage size: A4 / 210 × 297 mm\nTransport: T.38 / UDPTL\nSource: typed Persian and English text\n\n"
            "پیش از ارسال، اندازه و چیدمان صفحه در پیش‌نمایش بررسی می‌شود.\nنسخهٔ دریافت‌شده در جدول فکس‌ها قرار می‌گیرد.\n\nبا احترام\nStel team"
            "\fStel — Page 2\n\nصفحهٔ دوم فکس\nارسال چندصفحه‌ای با اندازهٔ استاندارد A4.\n\nEnd of document.");
        QTimer::singleShot(0,&window,[&]{auto *dialog=child<QDialog>(window,"faxTextDialog");child<QPlainTextEdit>(*dialog,"faxTextInput")->setPlainText(letter);child<QDialogButtonBox>(*dialog,"")->button(QDialogButtonBox::Ok)->click();});
        child<QPushButton>(*fax,"faxEditText")->click();
        bool previewCaptured=false;QTimer previewTimer;previewTimer.setInterval(30);
        QObject::connect(&previewTimer,&QTimer::timeout,&window,[&]{
            auto *dialog=window.findChild<QDialog*>("faxPreviewDialog");if(!dialog||!dialog->isVisible())return;
            previewTimer.stop();dialog->resize(800,1160);QTest::qWait(100);
            child<QPushButton>(*dialog,"faxPreviewNext")->click();child<QPushButton>(*dialog,"faxPreviewPrevious")->click();
            capture(*dialog,"fax-preview");previewCaptured=true;dialog->reject();
        });
        previewTimer.start();child<QPushButton>(*fax,"faxPreview")->click();
        waitFor([&]{return previewCaptured;},"A4 preview did not open");
        const auto faxReceiver=QObject::connect(engine,&SipEngine::callChanged,&window,[&](const CallInfo &call){
            if(call.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,engine,[&,id=call.id]{require(engine->receiveFax(id,"t38"),"Cannot receive demo fax");});
        });
        child<QPushButton>(*fax,"faxSend")->click();
        waitFor([&]{int completed=0;for(const auto &job:engine->faxes()){
            require(job.state!="failed",qPrintable(job.detail));completed+=job.state=="completed"&&job.pages==2;
        }return completed==2;},"Two-page fax did not complete",100000);
        waitFor([&]{return engine->calls().isEmpty();},"Fax call did not close");QTest::qWait(2700);
        QObject::disconnect(faxReceiver);
        auto *inbox=child<QTableWidget>(*fax,"receivedFaxes");require(inbox->rowCount()==1,"Received fax is missing");
        child<QPushButton>(*fax,"faxDeskTab1")->click();inbox->selectRow(0);capture(window,"fax");
        child<QPushButton>(*fax,"faxDeskTab2")->click();
        auto *outbox=child<QTableWidget>(*fax,"sentFaxes");require(outbox->rowCount()==1,"Sent fax is missing");outbox->selectRow(0);capture(window,"fax-outbox");
        child<QPushButton>(*fax,"faxDeskTab0")->click();capture(window,"fax-send");pages->setCurrentIndex(0);
        require(engine->dial(accounts[0].id,"1002",false),"Cannot place the main-screen demo call");
        waitFor([&]{return callDialog(window,true);},"Main-screen incoming call did not open");
        child<QPushButton>(*callDialog(window,true),"callAnswer")->click();waitFor([&]{return running(*engine);},"Main-screen call did not connect");
        QTest::qWait(1200);capture(window,"main-window");hangup();
        child<QLineEdit>(window,"dialTarget")->clear();capture(window,"main-idle");
        pages->setCurrentIndex(3);capture(window,"contacts");
        pages->setCurrentIndex(4);capture(window,"history");

        window.showSettings();auto *settings=child<QDialog>(window,"settingsDialog");settings->resize(1080,900);
        auto *settingsPages=child<QTabWidget>(*settings,"settingsPages");
        for(const auto &entry:QList<QPair<int,QString>>{{0,"accounts"},{1,"codecs"},{2,"video-codecs"},{3,"network"},{4,"security"},{5,"about"}}){
            settings->resize(1080,entry.first==0||entry.first==5?900:800);settingsPages->setCurrentIndex(entry.first);
            if(entry.first==1){
                auto *codecs=child<QListWidget>(*settings,"audioCodecs");QListWidgetItem *g729=nullptr;
                for(int n=0;n<codecs->count();++n)if(codecs->item(n)->data(Qt::UserRole).toString().startsWith("g729/"))g729=codecs->item(n);
                require(g729,"This capture profile needs a build with G.729");g729->setCheckState(Qt::Checked);codecs->setCurrentItem(g729);
                while(codecs->row(g729)>0)child<QPushButton>(*settings,"audioCodecUp")->click();
                child<QPushButton>(*settings,"audioApplyCodecs")->click();
            }
            capture(*settings,entry.second);
        }
        settingsPages->setCurrentIndex(0);settings->resize(1080,900);
        auto *accountPages=child<QTabWidget>(*settings,"accountTabs");
        for(const auto &entry:QList<QPair<int,QString>>{{1,"account-security"},{2,"account-nat"},{3,"account-forwarding"}}){
            accountPages->setCurrentIndex(entry.first);capture(*settings,entry.second);
        }
        QJsonObject manifest{{"version",QString::fromLatin1(STEL_VERSION)},{"qt",QString::fromLatin1(qVersion())},
            {"liblinphone",QString::fromUtf8(linphone_core_get_version())},{"platform",QSysInfo::prettyProductName()},
            {"captured_at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
            {"ui_source_commit",qEnvironmentVariable("STEL_SCREENSHOT_REVISION")},
            {"fixture","Disposable loopback PBX, two registered demo accounts, dummy microphone and camera; real SIP messages and a two-page T.38 fax."},
            {"images",images}};
        QFile metadata(output.filePath("capture.json"));require(metadata.open(QIODevice::WriteOnly),"Cannot write capture metadata");metadata.write(QJsonDocument(manifest).toJson());
        qInfo()<<"Screenshots captured from Stel"<<STEL_VERSION;return 0;
    }catch(const std::exception &error){qCritical()<<error.what();return 1;}
}
