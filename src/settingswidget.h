#pragma once
#include "accountdialog.h"
#include "sipengine.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
namespace stel {
QString registrationLabel(const QString &state);
class SettingsWidget : public QWidget {
    Q_OBJECT
public:
    SettingsWidget(SipEngine &,QVector<Account> &,Preferences &,Store &,bool canSave,QWidget *parent=nullptr);
    void openAccounts();
signals:
    void accountsChanged();
private:
    SipEngine &engine_;
    QVector<Account> &accounts_;
    Preferences &prefs_;
    Store &store_;
    bool canSave_;
    QTabWidget *pages_;
    QListWidget *list_;
    AccountEditor *editor_;
    QPushButton *saveAccount_, *register_, *unregister_, *remove_;
    QLabel *registrationDot_, *registrationText_, *accountNetwork_;
    QPlainTextEdit *response_;
    QHash<QString,Account> drafts_;
    QHash<QString,QString> states_, details_;
    QString editingId_;
    QWidget *accountsPage();
    QWidget *audioPage();
    QWidget *videoPage();
    QWidget *codecSettings(bool video);
    QWidget *networkPage();
    QWidget *securityPage();
    QWidget *aboutPage();
    void rebuildList(const QString &select = {});
    void selectAccount(const QString &);
    void updateAccountStatus();
    bool persist(QString *error=nullptr);
    bool commitAccount(bool registerAfter);
    void addAccount();
    void removeAccount();
    void report(const QString &,bool error=false);
};
}
