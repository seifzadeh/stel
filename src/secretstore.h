#pragma once
#include <QByteArray>
#include <QString>
namespace stel {
bool credentialOperationActive();
class SecretStore {
public:
    explicit SecretStore(const QString &configDirectory);
    bool read(QByteArray &, QString *error=nullptr) const;
    bool write(const QByteArray &, QString *error=nullptr) const;
private:
    QString key_;
};
}
