#include "accountdialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QUuid>
#include <QSignalBlocker>

namespace stel {
AccountEditor::AccountEditor(QWidget *parent):QWidget(parent) {
    Account a;
    auto *layout=new QVBoxLayout(this); auto *tabs=new QTabWidget;tabs->setObjectName("accountTabs");layout->addWidget(tabs);
    auto form=[&](const QString &title){auto *w=new QWidget;auto *f=new QFormLayout(w);f->setSpacing(6);tabs->addTab(w,title);return f;};
    auto edit=[&](QFormLayout *f,const QString &key,const QString &label,const QString &value,bool secret=false){
        auto *e=new QLineEdit(value);e->setObjectName(key); if(secret)e->setEchoMode(QLineEdit::Password); text_[key]=e; f->addRow(label,e); return e;
    };
    auto check=[&](QFormLayout *f,const QString &key,const QString &label,bool value){
        auto *c=new QCheckBox(label); c->setChecked(value); check_[key]=c;c->setObjectName(key); f->addRow(c);return c;
    };
    auto file=[&](QFormLayout *f,const QString &key,const QString &label,const QString &value){
        auto *w=new QWidget; auto *row=new QHBoxLayout(w);row->setContentsMargins(0,0,0,0);
        auto *e=new QLineEdit(value);e->setObjectName(key);text_[key]=e;auto *b=new QPushButton("Browse…");row->addWidget(e);row->addWidget(b);f->addRow(label,w);
        connect(b,&QPushButton::clicked,this,[this,e]{auto s=QFileDialog::getOpenFileName(this,"Select PEM file");if(!s.isEmpty())e->setText(s);});
    };
    auto *sip=form("General");
    edit(sip,"label","Account name",a.label)->setPlaceholderText("Office / home / provider");
    auto *callerName=edit(sip,"callerIdName","Caller ID Name",a.callerIdName);callerName->setMaxLength(256);callerName->setPlaceholderText("Name shown to people you call");callerName->setToolTip("Sent as the display name in the SIP From header.");
    edit(sip,"username","SIP username",a.username);
    edit(sip,"domain","SIP domain",a.domain)->setPlaceholderText("pbx.example.com");
    edit(sip,"server","Registrar / proxy",a.server)->setPlaceholderText("sip:192.168.17.12:5061");
    edit(sip,"authUser","Authentication ID",a.authUser)->setPlaceholderText("Blank = SIP username");
    edit(sip,"password","Password",a.password,true);
    check(sip,"rememberPassword","Remember passwords in the system password store",a.rememberPassword);
    check(sip,"autoAnswer","Auto Answer",a.autoAnswer)->setToolTip("Automatically answer incoming calls on this account with audio only. Do not disturb and forwarding keep priority.");
    transport_=new QComboBox;transport_->setObjectName("transport");transport_->addItems({"tls","tcp","udp"});transport_->setCurrentText(a.transport);sip->addRow("SIP transport",transport_);
    encryption_=new QComboBox;encryption_->setObjectName("encryption");
    for(const auto &mode:{"none","srtp","dtls","zrtp"})encryption_->addItem(mediaSecurityLabel(mode),QString(mode));
    encryption_->setCurrentIndex(encryption_->findData(a.encryption));sip->addRow("Media type",encryption_);
    auto *encNote=new QLabel("RTP is the default. Select SRTP or another encrypted mode when your PBX supports it. TLS protects signalling separately; encrypted media modes require encryption on both sides.");encNote->setWordWrap(true);sip->addRow(encNote);
    expiry_=new QSpinBox;expiry_->setRange(60,86400);expiry_->setValue(a.expires);
    check(sip,"enabled","Enable account",a.enabled);
    auto *note=new QLabel("Passwords are remembered by default when you Save or Register. Turn this off to keep this account's passwords only for the current run.");note->setWordWrap(true);sip->addRow(note);
    auto *security=form("Security");
    auto *securityNote=new QLabel("TLS verifies the server certificate and hostname. Add your PBX root CA in Settings. Client certificates below enable mutual TLS per account.");securityNote->setWordWrap(true);security->addRow(securityNote);
    file(security,"certificate","Client certificate (PEM)",a.certificate);file(security,"privateKey","Private key (PEM)",a.privateKey);
    edit(security,"keyPassword","Private key password",a.keyPassword,true);
    auto *nat=form("NAT / WebRTC");
    nat->addRow("Registration expiry (s)",expiry_);
    check(nat,"ice","Enable ICE",a.ice);edit(nat,"stunServer","STUN / TURN server",a.stunServer)->setPlaceholderText("stun.example.com:3478");
    check(nat,"turn","Enable TURN relay",a.turn);edit(nat,"turnUser","TURN username",a.turnUser);edit(nat,"turnPassword","TURN password",a.turnPassword,true);
    auto *web=check(nat,"webrtc","WebRTC media profile (DTLS-SRTP + ICE + BUNDLE)",a.webrtc);
    connect(web,&QCheckBox::toggled,this,[this](bool on){if(on){check_["ice"]->setChecked(true);encryption_->setCurrentIndex(encryption_->findData("dtls"));}});
    auto *webNote=new QLabel("This profile enables SIP-to-WebRTC media interoperability through a compatible PBX/gateway. Direct browser signalling, WSS and SCTP data channels are not provided by this backend.");webNote->setWordWrap(true);nat->addRow(webNote);
    auto *forward=form("Forwarding");edit(forward,"forwardTo","Forward incoming calls to",a.forwardTo)->setPlaceholderText("Blank = off; extension or SIP URI");
    check(forward,"forwardBusy","Forward only when busy / DND",a.forwardBusy);
    auto *forwardNote=new QLabel("Uses SIP 302 redirection while Stel is running. The PBX must permit redirects. Always-on forwarding while the app is offline must be configured on the PBX.");forwardNote->setWordWrap(true);forward->addRow(forwardNote);
    for(auto *e:text_)connect(e,&QLineEdit::textChanged,this,[this]{dirty_=true;emit edited();});
    for(auto *c:check_)connect(c,&QCheckBox::toggled,this,[this]{dirty_=true;emit edited();});
    for(auto *c:{transport_,encryption_})connect(c,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{dirty_=true;emit edited();});
    connect(expiry_,QOverload<int>::of(&QSpinBox::valueChanged),this,[this]{dirty_=true;emit edited();});
    setAccount(a);
}
void AccountEditor::setAccount(const Account &a) {
    const QSignalBlocker blocker(this);original_=a;
    const QMap<QString,QString> strings{{"label",a.label},{"callerIdName",a.callerIdName},{"username",a.username},{"domain",a.domain},{"server",a.server},{"authUser",a.authUser},{"password",a.password},
        {"certificate",a.certificate},{"privateKey",a.privateKey},{"keyPassword",a.keyPassword},{"stunServer",a.stunServer},{"turnUser",a.turnUser},{"turnPassword",a.turnPassword},{"forwardTo",a.forwardTo}};
    for(auto it=strings.cbegin();it!=strings.cend();++it)text_[it.key()]->setText(it.value());
    const QMap<QString,bool> values{{"enabled",a.enabled},{"autoAnswer",a.autoAnswer},{"ice",a.ice},{"turn",a.turn},{"webrtc",a.webrtc},{"forwardBusy",a.forwardBusy},{"rememberPassword",a.rememberPassword}};
    for(auto it=values.cbegin();it!=values.cend();++it)check_[it.key()]->setChecked(it.value());
    transport_->setCurrentText(a.transport);encryption_->setCurrentIndex(encryption_->findData(a.encryption));expiry_->setValue(a.expires);dirty_=false;
}
AccountDialog::AccountDialog(const Account &a,QWidget *parent):QDialog(parent) {
    setWindowTitle(a.id.isEmpty()?"Add SIP account":"Edit SIP account");resize(560,570);
    auto *layout=new QVBoxLayout(this);editor_=new AccountEditor;editor_->setAccount(a);layout->addWidget(editor_);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,this,[this]{auto error=account().validate();if(error.isEmpty())accept();else QMessageBox::warning(this,"Check account",error);});
}
Account AccountEditor::account() const {
    Account a=original_;if(a.id.isEmpty())a.id=QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.label=text_["label"]->text().trimmed();a.username=text_["username"]->text().trimmed();a.domain=text_["domain"]->text().trimmed();
    a.callerIdName=text_["callerIdName"]->text().trimmed();a.autoAnswer=check_["autoAnswer"]->isChecked();
    a.server=text_["server"]->text().trimmed();a.authUser=text_["authUser"]->text().trimmed();a.password=text_["password"]->text();
    a.rememberPassword=check_["rememberPassword"]->isChecked();if(!a.password.isEmpty()||!a.rememberPassword)a.credentialsRequired=false;
    a.transport=transport_->currentText();a.encryption=encryption_->currentData().toString();a.expires=expiry_->value();
    a.certificate=text_["certificate"]->text().trimmed();a.privateKey=text_["privateKey"]->text().trimmed();a.keyPassword=text_["keyPassword"]->text();
    a.enabled=check_["enabled"]->isChecked();a.ice=check_["ice"]->isChecked();a.webrtc=check_["webrtc"]->isChecked();
    a.turn=check_["turn"]->isChecked();a.stunServer=text_["stunServer"]->text().trimmed();a.turnUser=text_["turnUser"]->text().trimmed();a.turnPassword=text_["turnPassword"]->text();
    a.forwardTo=text_["forwardTo"]->text().trimmed();a.forwardBusy=check_["forwardBusy"]->isChecked();return a;
}
}
