#include "chatwidget.h"
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace stel {
namespace {
class Composer : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<void()> send;
protected:
    void keyPressEvent(QKeyEvent *event) override {
        if((event->key()==Qt::Key_Return||event->key()==Qt::Key_Enter)&&!event->modifiers().testFlag(Qt::ShiftModifier)&&!preedit_){
            if(send)send();
            event->accept();return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        preedit_=!event->preeditString().isEmpty();QPlainTextEdit::inputMethodEvent(event);
    }
private:
    bool preedit_=false;
};
QLabel *plainLabel(const QString &text,QWidget *parent=nullptr){
    auto *label=new QLabel(text,parent);label->setTextFormat(Qt::PlainText);return label;
}
}
ChatWidget::ChatWidget(SipEngine &engine,Store &store,QWidget *parent):QWidget(parent),engine_(engine),store_(store){
    setObjectName("messagesPage");setLayoutDirection(Qt::LeftToRight);
    auto *layout=new QHBoxLayout(this);layout->setContentsMargins(14,14,14,14);layout->setSpacing(16);
    auto *sidebar=new QWidget;sidebar->setMinimumWidth(190);sidebar->setMaximumWidth(270);auto *left=new QVBoxLayout(sidebar);left->setContentsMargins(0,0,0,0);
    auto *title=plainLabel("CONVERSATIONS");title->setObjectName("muted");left->addWidget(title);
    conversations_=new QListWidget;conversations_->setObjectName("messageConversations");conversations_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);conversations_->setTextElideMode(Qt::ElideRight);left->addWidget(conversations_,1);
    auto *newChat=new QPushButton("+ New conversation");newChat->setObjectName("newConversation");left->addWidget(newChat);layout->addWidget(sidebar,1);
    auto *right=new QVBoxLayout;layout->addLayout(right,3);
    auto *address=new QHBoxLayout;account_=new QComboBox;account_->setObjectName("messageAccount");account_->setMinimumWidth(130);
    recipient_=new QLineEdit;recipient_->setObjectName("messageRecipient");recipient_->setLayoutDirection(Qt::LeftToRight);recipient_->setPlaceholderText("Number or SIP address");recipient_->setClearButtonEnabled(true);
    auto *open=new QPushButton("Open chat");open->setObjectName("openConversation");auto *settings=new QPushButton("Settings");
    address->addWidget(account_,1);address->addWidget(recipient_,2);address->addWidget(open);address->addWidget(settings);right->addLayout(address);
    heading_=plainLabel("Messages");heading_->setObjectName("conversationHeading");heading_->setWordWrap(true);heading_->setStyleSheet("font-size:16px;font-weight:600;padding:4px 0;");right->addWidget(heading_);
    scroll_=new QScrollArea;scroll_->setObjectName("messageTimeline");scroll_->setWidgetResizable(true);scroll_->setFrameShape(QFrame::NoFrame);right->addWidget(scroll_,1);
    auto *composeRow=new QHBoxLayout;auto *composer=new Composer;composer_=composer;composer_->setObjectName("messageComposer");
    composer_->setPlaceholderText("Write a message…\nEnter to send · Shift+Enter for a new line");composer_->setMinimumHeight(80);composer_->setMaximumHeight(120);
    // Preserve the LTR application layout. Qt's Unicode bidi/shaping engine
    // determines each paragraph's text direction, including Persian + Latin.
    auto option=composer_->document()->defaultTextOption();option.setTextDirection(Qt::LayoutDirectionAuto);option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);composer_->document()->setDefaultTextOption(option);
    send_=new QPushButton("Send");send_->setObjectName("sendMessage");send_->setStyleSheet("QPushButton{background:#138e81;color:white;font-weight:600;border:0;} QPushButton:disabled{background:#dce5e7;color:#89979e;}");
    composeRow->addWidget(composer_,1);composeRow->addWidget(send_);right->addLayout(composeRow);
    notice_=plainLabel("SIP messages use the selected account. Your PBX/provider must support messaging.");notice_->setObjectName("messageNotice");notice_->setWordWrap(true);notice_->setStyleSheet("color:#667888;");right->addWidget(notice_);
    QString error;historyWritable_=store_.loadMessages(messages_,&error);if(!historyWritable_)notice_->setText(error);
    bool interrupted=false;for(auto &m:messages_)if(!m.incoming&&m.state=="sending"){m.state="interrupted";m.error="The previous session ended before the send result was received.";interrupted=true;}
    if(interrupted)persist();
    connect(&engine_,&SipEngine::messageChanged,this,&ChatWidget::receive);
    connect(open,&QPushButton::clicked,this,&ChatWidget::openConversation);connect(recipient_,&QLineEdit::returnPressed,this,&ChatWidget::openConversation);
    connect(send_,&QPushButton::clicked,this,&ChatWidget::sendText);composer->send=[this]{if(send_->isEnabled())sendText();};
    connect(composer_,&QPlainTextEdit::textChanged,this,&ChatWidget::updateSend);connect(recipient_,&QLineEdit::textChanged,this,&ChatWidget::updateSend);
    connect(account_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
        const auto id=account_->currentData().toString();
        if(!currentPeer_.isEmpty()&&!id.isEmpty()&&id!=currentAccount_){
            const auto peer=engine_.messageTarget(id,recipient_->text());
            if(!peer.isEmpty()){selectConversation(id,peer);return;}
        }
        updateSend();
    });
    connect(settings,&QPushButton::clicked,this,&ChatWidget::settingsRequested);
    connect(newChat,&QPushButton::clicked,this,[this]{
        if(!currentPeer_.isEmpty())drafts_[key(currentAccount_,currentPeer_)]=composer_->toPlainText();
        currentAccount_.clear();currentPeer_.clear();conversations_->clearSelection();recipient_->clear();composer_->clear();refreshMessages(true);recipient_->setFocus();
    });
    connect(conversations_,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item){if(item)selectConversation(item->data(Qt::UserRole).toString(),item->data(Qt::UserRole+1).toString());});
    refreshConversations();refreshMessages();updateSend();
}
QString ChatWidget::key(const QString &account,const QString &peer) const{return account+'\n'+peer;}
QString ChatWidget::accountLabel(const QString &id) const {
    for(const auto &account:accounts_)if(account.id==id)return account.label.isEmpty()?account.username+'@'+account.domain:account.label;
    return "Removed account";
}
void ChatWidget::setAccounts(const QVector<Account> &accounts){
    const auto previous=account_->currentData().toString();accounts_=accounts;const QSignalBlocker blocker(account_);account_->clear();
    for(const auto &a:accounts_)account_->addItem(accountLabel(a.id)+(a.enabled?QString():" (disabled)"),a.id);
    const auto selected=currentAccount_.isEmpty()?previous:currentAccount_;
    account_->setCurrentIndex(selected.isEmpty()?(account_->count()?0:-1):account_->findData(selected));
    refreshConversations();refreshMessages();updateSend();
}
int ChatWidget::unreadCount() const{int count=0;for(const auto &m:messages_)count+=m.incoming&&m.unread;return count;}
void ChatWidget::openConversation(){
    const auto id=account_->currentData().toString();const auto peer=engine_.messageTarget(id,recipient_->text());
    if(peer.isEmpty()){notice_->setText("Choose an account and enter a valid recipient number or SIP address.");return;}
    selectConversation(id,peer);composer_->setFocus();
}
void ChatWidget::selectConversation(const QString &id,const QString &peer){
    if(id!=currentAccount_||peer!=currentPeer_){
        if(!currentPeer_.isEmpty())drafts_[key(currentAccount_,currentPeer_)]=composer_->toPlainText();
        currentAccount_=id;currentPeer_=peer;visibleLimit_=100;composer_->setPlainText(drafts_.value(key(id,peer)));
    }
    {const QSignalBlocker blocker(account_);account_->setCurrentIndex(account_->findData(id));}
    recipient_->setText(peer);markRead();refreshMessages(true);updateSend();
}
void ChatWidget::sendText(){
    const auto body=composer_->toPlainText();if(body.trimmed().isEmpty())return;
    const auto id=account_->currentData().toString();const auto peer=engine_.messageTarget(id,recipient_->text());
    if(peer.isEmpty()){notice_->setText("Choose an account and enter a valid recipient number or SIP address.");return;}
    selectConversation(id,peer);
    if(composer_->toPlainText()!=body)composer_->setPlainText(body);
    if(engine_.sendMessage(id,peer,body).isEmpty()){notice_->setText(engine_.lastError());return;}
    drafts_.remove(key(id,peer));composer_->clear();
    if(historyWritable_)notice_->setText("Sent uses the SIP server's acknowledgement; it does not mean the recipient has read the message.");
    refreshMessages(true);
}
void ChatWidget::receive(const MessageInfo &message){
    auto found=std::find_if(messages_.begin(),messages_.end(),[&](const MessageInfo &m){return m.id==message.id;});
    if(found!=messages_.end()){
        if(message.incoming)return; // Retransmitted SIP MESSAGE: retain the existing read state.
        const bool unread=found->unread;*found=message;found->unread=unread;
    }else{
        auto added=message;added.unread=added.incoming&&!(isVisible()&&added.accountId==currentAccount_&&added.peer==currentPeer_);messages_.append(added);
    }
    persist();refreshConversations();
    if(message.accountId==currentAccount_&&message.peer==currentPeer_)refreshMessages();
    emit unreadCountChanged(unreadCount());
}
void ChatWidget::persist(){
    if(!historyWritable_)return;
    QString error;if(!store_.saveMessages(messages_,&error))notice_->setText("Could not save message history: "+error);
}
void ChatWidget::markRead(){
    if(!isVisible())return;
    bool changed=false;for(auto &m:messages_)if(m.accountId==currentAccount_&&m.peer==currentPeer_&&m.unread){m.unread=false;changed=true;}
    if(changed){persist();refreshConversations();emit unreadCountChanged(unreadCount());}
}
void ChatWidget::showEvent(QShowEvent *event){QWidget::showEvent(event);markRead();}
void ChatWidget::refreshConversations(){
    struct Conversation{MessageInfo last;int unread=0;};QHash<QString,Conversation> indexed;
    for(const auto &m:messages_){auto &c=indexed[key(m.accountId,m.peer)];if(c.last.id.isEmpty()||m.timestamp>=c.last.timestamp)c.last=m;c.unread+=m.incoming&&m.unread;}
    auto entries=indexed.values();std::sort(entries.begin(),entries.end(),[](const Conversation &a,const Conversation &b){return a.last.timestamp>b.last.timestamp;});
    const QSignalBlocker blocker(conversations_);conversations_->clear();
    for(const auto &c:entries){
        const auto &m=c.last;auto preview=m.text.simplified();if(preview.size()>48)preview=preview.left(48)+"…";
        const auto name=m.peerName.isEmpty()?m.peer:m.peerName;
        auto *item=new QListWidgetItem(name+(c.unread?"  ("+QString::number(c.unread)+")":QString())+'\n'+accountLabel(m.accountId)+'\n'+preview,conversations_);
        item->setData(Qt::UserRole,m.accountId);item->setData(Qt::UserRole+1,m.peer);item->setToolTip(m.peer+"\n"+accountLabel(m.accountId));
        auto font=item->font();font.setBold(c.unread>0);item->setFont(font);
        if(m.accountId==currentAccount_&&m.peer==currentPeer_)conversations_->setCurrentItem(item);
    }
}
void ChatWidget::refreshMessages(bool bottom){
    const int previous=scroll_->verticalScrollBar()->value();const bool atBottom=bottom||previous>=scroll_->verticalScrollBar()->maximum()-24;
    auto *content=new QWidget;auto *layout=new QVBoxLayout(content);layout->setContentsMargins(8,10,8,10);layout->setSpacing(12);
    heading_->setText(currentPeer_.isEmpty()?"Messages":currentPeer_+"  ·  "+accountLabel(currentAccount_));
    QVector<MessageInfo> selected;for(const auto &m:messages_)if(m.accountId==currentAccount_&&m.peer==currentPeer_)selected.append(m);
    if(selected.isEmpty()){
        auto *empty=plainLabel(currentPeer_.isEmpty()?"Your conversations appear here.\nChoose an account and a recipient to start chatting.":"No messages yet.\nWrite your first message below.");
        empty->setObjectName("emptyConversation");empty->setWordWrap(true);empty->setAlignment(Qt::AlignCenter);empty->setStyleSheet("color:#7a8995;font-size:14px;padding:30px;");layout->addStretch();layout->addWidget(empty);layout->addStretch();
    }else{
        if(selected.size()>visibleLimit_){auto *earlier=new QPushButton("Load earlier messages");layout->addWidget(earlier);connect(earlier,&QPushButton::clicked,this,[this]{visibleLimit_+=100;refreshMessages();});}
        for(int n=qMax(0,int(selected.size())-visibleLimit_);n<selected.size();++n){
            const auto &m=selected[n];auto *row=new QHBoxLayout;row->setContentsMargins(0,0,0,0);auto *bubble=new QFrame;bubble->setObjectName("messageBubble");bubble->setMaximumWidth(560);
            bubble->setProperty("messageId",m.id);bubble->setProperty("incoming",m.incoming);
            bubble->setStyleSheet(QString("QFrame#messageBubble{background:%1;border:1px solid %2;border-radius:10px;} ").arg(m.incoming?"#ffffff":"#e0f3ee",m.incoming?"#dbe4e9":"#bbddd3"));
            auto *inside=new QVBoxLayout(bubble);inside->setContentsMargins(13,10,13,9);inside->setSpacing(6);
            auto *body=plainLabel(m.text);body->setObjectName("messageBody");body->setWordWrap(true);body->setTextInteractionFlags(Qt::TextSelectableByMouse|Qt::TextSelectableByKeyboard);body->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);
            body->setStyleSheet("font-size:14px;color:#213344;");inside->addWidget(body);
            auto *state=plainLabel(m.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm")+"  ·  "+messageStateLabel(m));state->setObjectName("messageDeliveryState");state->setStyleSheet("font-size:10px;color:#647887;");inside->addWidget(state);
            if(!m.error.isEmpty()){
                auto *error=plainLabel((m.sipStatus?"SIP "+QString::number(m.sipStatus)+" · ":QString())+m.error);error->setObjectName("messageError");error->setWordWrap(true);error->setStyleSheet("color:#b33748;font-size:11px;");inside->addWidget(error);
                if(!m.incoming&&m.state=="failed"){
                    auto *retry=new QPushButton("Send again");retry->setObjectName("retryMessage");inside->addWidget(retry,0,Qt::AlignLeft);
                    connect(retry,&QPushButton::clicked,this,[this,m]{if(engine_.sendMessage(m.accountId,m.peer,m.text).isEmpty())notice_->setText(engine_.lastError());});
                }
            }
            if(!m.incoming)row->addStretch();
            row->addWidget(bubble,0);
            if(m.incoming)row->addStretch();
            layout->addLayout(row);
        }
        layout->addStretch();
    }
    auto *old=scroll_->takeWidget();scroll_->setWidget(content);if(old)old->deleteLater();
    QTimer::singleShot(0,this,[this,atBottom,previous]{auto *bar=scroll_->verticalScrollBar();bar->setValue(atBottom?bar->maximum():previous);});
}
void ChatWidget::updateSend(){
    bool enabled=false;for(const auto &a:accounts_)if(a.id==account_->currentData().toString())enabled=a.enabled;
    send_->setEnabled(enabled&&!recipient_->text().trimmed().isEmpty()&&!composer_->toPlainText().trimmed().isEmpty());
}
}
