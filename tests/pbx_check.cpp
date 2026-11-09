// Explicit opt-in check against saved accounts. Credentials stay in QtKeychain.
#include "mainwindow.h"
#include "calldialog.h"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QPushButton>
#include <QTextStream>
#include <QTimer>
#include <QtTest>
int main(int argc,char **argv){
    QApplication app(argc,argv);app.setOrganizationName("Stel");app.setApplicationName("Stel");app.setQuitOnLastWindowClosed(false);
    if(argc!=4){QTextStream(stderr)<<"Usage: stel_pbx_check CONFIG_DIRECTORY FROM_USER TO_USER\n";return 2;}
    stel::Store store(QString::fromLocal8Bit(argv[1]));QVector<stel::Account> accounts;stel::Preferences prefs;QString error,from,to;
    if(!store.load(accounts,prefs,&error)){QTextStream(stderr)<<"Cannot load saved accounts.\n";return 2;}
    for(const auto &a:accounts){if(a.username==QString::fromLocal8Bit(argv[2]))from=a.id;if(a.username==QString::fromLocal8Bit(argv[3]))to=a.id;}
    if(from.isEmpty()||to.isEmpty()||from==to)return 2;
    stel::MainWindow window(store.directory());if(!window.ready())return 2;window.show();auto *engine=window.findChild<stel::SipEngine*>();
    auto testDevices=prefs;testDevices.inputDevice=stel::dummyMicTone();testDevices.outputDevice=stel::dummySpeaker();
    if(!engine->applyDevices(testDevices))return 2; // Runtime only; leave saved devices intact.
    QObject::connect(engine,&stel::SipEngine::callChanged,&app,[](const stel::CallInfo &i){QTextStream(stdout)<<(i.incoming?"IN ":"OUT ")<<i.state<<" media="<<i.encryption<<" duration="<<i.duration<<Qt::endl;});
    bool dialed=false,answered=false,stopping=false;QElapsedTimer connected;QTimer poll;poll.setInterval(200);
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{
        if(stopping)return;
        if(!dialed){
            if(engine->registrationState(from)!="LinphoneRegistrationOk"||engine->registrationState(to)!="LinphoneRegistrationOk")return;
            dialed=true;auto *combo=window.findChild<QComboBox*>("callAccount");combo->setCurrentIndex(combo->findData(from));window.findChild<QLineEdit*>("dialTarget")->setText(QString::fromLocal8Bit(argv[3]));
            for(auto *b:window.findChildren<QPushButton*>())if(b->text()=="Call"){b->click();break;}
        }
        if(!answered)for(auto *d:window.findChildren<stel::CallDialog*>())if(d->callInfo().incoming&&d->callInfo().accountId==to&&d->callInfo().rawState==LinphoneCallIncomingReceived){answered=true;d->findChild<QPushButton*>("callAnswer")->click();break;}
        const auto calls=engine->calls();bool running=calls.size()==2;double down=0;
        for(const auto &i:calls){running&=i.rawState==LinphoneCallStreamsRunning&&i.encryption.contains("None",Qt::CaseInsensitive)&&i.audioDownKbps>0;down+=i.audioDownKbps;}
        if(running&&!connected.isValid())connected.start();
        if(connected.isValid()&&!running){QTextStream(stderr)<<"FAIL: connected call ended or lost media.\n";stopping=true;app.exit(1);return;}
        if(running&&connected.elapsed()>=20000){
            QTextStream(stdout)<<"PASS: saved PBX accounts connected for 20 seconds with bidirectional RTP; download="<<down<<" kbit/s\n";
            stopping=true;for(const auto &i:calls)engine->hangup(i.id);QTimer::singleShot(400,&app,[&]{app.exit(0);});
        }
    });poll.start();QTimer::singleShot(45000,&app,[&]{QTextStream(stderr)<<"FAIL: registration/call timed out.\n";app.exit(1);});return app.exec();
}
