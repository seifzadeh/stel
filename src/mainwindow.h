#pragma once
#include "sipengine.h"
#include <QMainWindow>
#include <QPointer>
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QLabel;
class QPushButton;
class QTabWidget;
class QSystemTrayIcon;
class QDialog;
class QCloseEvent;
namespace stel {
class SettingsWidget;
class CallDialog;
class ChatWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &configDirectory = {}, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool ready() const { return ready_; }
    void showSettings();
protected:
    void closeEvent(QCloseEvent *) override;
private:
    Store store_;
    Preferences prefs_;
    QVector<Account> accounts_;
    SipEngine engine_;
    bool ready_ = false, settingsValid_ = true;
    bool closing_ = false, confirmingExit_ = false;
    QComboBox *accountBox_ = nullptr;
    QLineEdit *target_ = nullptr;
    QListWidget *calls_ = nullptr, *contacts_ = nullptr, *history_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QLabel *status_ = nullptr, *detail_ = nullptr;
    QPushButton *openCall_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    SettingsWidget *settings_ = nullptr;
    ChatWidget *chat_ = nullptr;
    QDialog *settingsDialog_ = nullptr;
    QWidget *callAccountBar_ = nullptr;
    QHash<QString,CallInfo> callCache_;
    QHash<QString,QPointer<CallDialog>> callDialogs_;
    QHash<QString,QString> accountStates_;
    void rebuildAccounts();
    void save();
    void updateCall(const CallInfo &);
    void updateControls();
    void showCall(const QString &);
    void refreshCallDialogs();
    void dial(bool video);
    void refreshContacts();
    void refreshHistory();
    void appendLog(const QString &);
    QString selectedCall() const;
    QWidget *createPhone();
    QWidget *createContacts();
    QWidget *createHistory();
    QWidget *createSettings();
};
}
