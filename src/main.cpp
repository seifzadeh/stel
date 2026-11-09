#include "mainwindow.h"
#include "branding.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDialog>
#include <QLocalServer>
#include <QLocalSocket>
#include <QLockFile>
#include <QTimer>
#include <cstdio>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

static void portableRuntime() {
    QString executable;
#ifdef Q_OS_WIN
    wchar_t path[32768];const auto count=GetModuleFileNameW(nullptr,path,32768);executable=QString::fromWCharArray(path,int(count));
#elif defined(Q_OS_LINUX)
    char path[4096];const auto count=readlink("/proc/self/exe",path,sizeof(path));if(count>0)executable=QString::fromUtf8(path,int(count));
#endif
    if(executable.isEmpty())return;
    const auto directory=QFileInfo(executable).absolutePath();QString prefix;
    for(const auto &candidate:{directory,QDir(directory+"/..").absolutePath()})if(QDir(candidate+"/share/belr/grammars").exists()){prefix=candidate;break;}
    if(prefix.isEmpty())return;
    auto set=[&](const char *name,const QString &value){qputenv(name,value.toUtf8());};
    set("STEL_RESOURCE_PREFIX",prefix);set("STEL_CA_BUNDLE",prefix+"/share/certs/ca-certificates.crt");
    const auto plugins=QDir(prefix+"/plugins/platforms").exists()?prefix+"/plugins":prefix;
    set("QT_PLUGIN_PATH",plugins);set("QT_QPA_PLATFORM_PLUGIN_PATH",plugins+"/platforms");qputenv("QT_QPA_PLATFORMTHEME","");
    if(QDir(prefix+"/share/alsa").exists())set("ALSA_CONFIG_DIR",prefix+"/share/alsa");
    if(QDir(prefix+"/lib/alsa-lib").exists())set("ALSA_PLUGIN_DIR",prefix+"/lib/alsa-lib");
}

int main(int argc,char **argv) {
    // Qt's GUI-subsystem command-line helper can show a message box on Windows.
    // Keep the common version command usable by terminals and build automation.
    if(argc==2 && (!std::strcmp(argv[1],"--version")||!std::strcmp(argv[1],"-v"))) {
        std::printf("Stel %s\n",STEL_VERSION);return 0;
    }
    portableRuntime();
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif
    QApplication app(argc,argv);app.setOrganizationName("Stel");app.setApplicationName("Stel");app.setApplicationVersion(STEL_VERSION);
    app.setWindowIcon(stel::applicationIcon());app.setDesktopFileName("stel");
    QCommandLineParser parser;parser.setApplicationDescription("Compact multi-account SIP softphone powered by liblinphone");parser.addHelpOption();parser.addVersionOption();
    parser.addOption({"config-dir","Use an isolated configuration directory.","path"});
    parser.addOption({"screenshot","Save a UI screenshot and exit (no account required).","path"});
    parser.addOption({"settings","Open Settings → Accounts."});
    parser.addOption({"smoke-test","Start the real core and UI, then exit after two seconds."});parser.process(app);
    auto resourcePrefix=qEnvironmentVariable("STEL_RESOURCE_PREFIX");
    if(resourcePrefix.isEmpty() && QDir(QCoreApplication::applicationDirPath()+"/share/belr/grammars").exists())resourcePrefix=QCoreApplication::applicationDirPath();
    if(resourcePrefix.isEmpty() && QDir(QCoreApplication::applicationDirPath()+"/../share/belr/grammars").exists())resourcePrefix=QDir(QCoreApplication::applicationDirPath()+"/..").absolutePath();
    if(!resourcePrefix.isEmpty()) {
        stel::SipEngine::configureResources(resourcePrefix);
        if(!qEnvironmentVariableIsSet("STEL_CA_BUNDLE"))qputenv("STEL_CA_BUNDLE",(resourcePrefix+"/share/certs/ca-certificates.crt").toUtf8());
    }
    stel::Store location(parser.value("config-dir"));
    QLockFile instanceLock(location.directory()+"/instance.lock");instanceLock.setStaleLockTime(0);
    const auto serverName="stel-"+QString::fromLatin1(QCryptographicHash::hash(QFileInfo(location.directory()).canonicalFilePath().toUtf8(),QCryptographicHash::Sha256).toHex().left(32));
    if(!instanceLock.tryLock(0)) {
        QLocalSocket socket;socket.connectToServer(serverName);
        if(socket.waitForConnected(1500)){socket.write(parser.isSet("settings")?"settings":"activate");socket.waitForBytesWritten(500);}
        return 0;
    }
    QLocalServer server;server.setSocketOptions(QLocalServer::UserAccessOption);QLocalServer::removeServer(serverName);server.listen(serverName);
    stel::MainWindow window(location.directory());window.show();
    QObject::connect(&server,&QLocalServer::newConnection,&window,[&]{while(auto *socket=server.nextPendingConnection()) {
        QObject::connect(socket,&QLocalSocket::readyRead,&window,[&,socket]{const auto command=socket->readAll();if(command=="settings")window.showSettings();else{window.showNormal();window.raise();window.activateWindow();}socket->disconnectFromServer();});
        QObject::connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);
    }});
    if(parser.isSet("settings"))window.showSettings();
    if(!window.ready())return 2;
    if(parser.isSet("screenshot"))QTimer::singleShot(1200,&app,[&]{auto *dialog=window.findChild<QDialog*>("settingsDialog");const bool ok=(dialog&&dialog->isVisible()?static_cast<QWidget*>(dialog):&window)->grab().save(parser.value("screenshot"));app.exit(ok?0:3);});
    else if(parser.isSet("smoke-test"))QTimer::singleShot(2000,&app,[&]{app.exit(0);});
    return app.exec();
}
