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
class QKeyEvent;
namespace stel {
class SettingsWidget;
class RemoteControlServer;
class DeskPhone;
class CallDialog;
class ChatWidget;
class FaxWidget;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString &configDirectory = {}, QWidget *parent = nullptr);
    ~MainWindow() override;
    bool ready() const { return ready_; }
    void showSettings();
protected:
    void closeEvent(QCloseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    Store store_;
    Preferences prefs_;
    QVector<Account> accounts_;
    SipEngine engine_;
    RemoteControlServer *remoteControl_=nullptr;
    void reloadApiConfiguration(const QString &);
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
    DeskPhone *desk_ = nullptr;
    SettingsWidget *settings_ = nullptr;
    ChatWidget *chat_ = nullptr;
    FaxWidget *fax_ = nullptr;
    QDialog *settingsDialog_ = nullptr;
    QHash<QString,CallInfo> callCache_;
    QHash<QString,QPointer<CallDialog>> callDialogs_;
    QHash<QString,QString> accountStates_;
    QString videoCallId_,inlineVideoId_,lastCallMessage_;
    void mainCallAction(const char *);
    void updateMainVideo(const CallInfo &);
    void rebuildAccounts();
    void refreshExpansion();
    void configureExpansion(int);
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
    QWidget *createContacts();
    QWidget *createHistory();
    QWidget *createSettings();
};
}
