#pragma once
#include "sipengine.h"
#include <QWidget>
class QComboBox;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QScrollArea;
class QLabel;
class QPushButton;
class QVBoxLayout;
namespace stel {
class ChatWidget : public QWidget {
    Q_OBJECT
public:
    ChatWidget(SipEngine &,Store &,QWidget *parent=nullptr);
    void setAccounts(const QVector<Account> &);
    int unreadCount() const;
signals:
    void unreadCountChanged(int count);
    void settingsRequested();
protected:
    void showEvent(QShowEvent *) override;
private:
    SipEngine &engine_;
    Store &store_;
    QVector<Account> accounts_;
    QVector<MessageInfo> messages_;
    QHash<QString,QString> drafts_;
    QString currentAccount_,currentPeer_;
    bool historyWritable_=true;
    int visibleLimit_=100;
    QComboBox *account_;
    QLineEdit *recipient_;
    QListWidget *conversations_;
    QPlainTextEdit *composer_;
    QScrollArea *scroll_;
    QLabel *heading_,*notice_;
    QPushButton *send_;
    QString key(const QString &,const QString &) const;
    QString accountLabel(const QString &) const;
    void openConversation();
    void selectConversation(const QString &,const QString &);
    void sendText();
    void receive(const MessageInfo &);
    void refreshConversations();
    void refreshMessages(bool bottom=false);
    void updateSend();
    void markRead();
    void persist();
};
}
