#include "secretstore.h"
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
#include <qt6keychain/keychain.h>
#else
#include <qt5keychain/keychain.h>
#endif

namespace stel {
namespace {
int activeOperations=0;
bool run(QKeychain::Job &job,QString *error) {
    struct Busy {Busy(){++activeOperations;}~Busy(){--activeOperations;}} busy;
    job.setAutoDelete(false);job.setInsecureFallback(false);
    QEventLoop loop;QTimer timeout;timeout.setSingleShot(true);bool finished=false;
    QObject::connect(&job,&QKeychain::Job::finished,&loop,[&]{finished=true;loop.quit();});
    QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);
    timeout.start(15000);job.start();if(!finished)loop.exec();
    if(!finished){if(error)*error="System password store did not respond. Unlock your system keyring or turn off Remember passwords for this account.";return false;}
    if(job.error()!=QKeychain::NoError&&job.error()!=QKeychain::EntryNotFound){if(error)*error="System password store: "+job.errorString();return false;}
    return true;
}
}
bool credentialOperationActive(){return activeOperations>0;}
SecretStore::SecretStore(const QString &directory) {
    const auto path=QDir(directory).canonicalPath();
    key_="accounts/"+QString::fromLatin1(QCryptographicHash::hash((path.isEmpty()?QDir(directory).absolutePath():path).toUtf8(),QCryptographicHash::Sha256).toHex());
}
bool SecretStore::read(QByteArray &value,QString *error) const {
    QKeychain::ReadPasswordJob job("Stel");job.setKey(key_);
    if(!run(job,error))return false;
    value=job.error()==QKeychain::EntryNotFound?QByteArray():job.binaryData();return true;
}
bool SecretStore::write(const QByteArray &value,QString *error) const {
    if(value.isEmpty()){QKeychain::DeletePasswordJob job("Stel");job.setKey(key_);return run(job,error);}
    QKeychain::WritePasswordJob job("Stel");job.setKey(key_);job.setBinaryData(value);return run(job,error);
}
}
