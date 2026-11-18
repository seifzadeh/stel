#include "windowlayout.h"
#include "remotecontrolwidget.h"
#include "remotecontrol.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QVBoxLayout>
namespace stel {
RemoteControlWidget::RemoteControlWidget(RemoteControlServer &api,QVector<Account> &accounts,Preferences &prefs,Store &store,bool canSave,QWidget *parent):QWidget(parent){
    setObjectName("remoteControlSettings");auto *layout=new QVBoxLayout(this);auto *title=new QLabel("Remote Control API");title->setStyleSheet(uiStyleSheet("font-size:20px;font-weight:600;"));layout->addWidget(title);
    auto *intro=new QLabel("Control calls, messages, fax and expansion keys from your own applications.\nYealink Action URI commands and a JSON API are available on the same address.");intro->setWordWrap(true);layout->addWidget(intro);
    auto *form=new QFormLayout;layout->addLayout(form);auto *enabled=new QCheckBox("Enable Remote Control");enabled->setObjectName("apiEnabled");enabled->setChecked(prefs.remoteControl.enabled);form->addRow(enabled);
    auto *address=new QComboBox;address->setEditable(true);address->setObjectName("apiAddress");address->addItem("127.0.0.1");address->addItem("::1");for(const auto &ip:localAddresses())if(address->findText(ip.address)<0)address->addItem(ip.address);address->setCurrentText(prefs.remoteControl.address);form->addRow("Listen on IP",address);
    auto *port=new QSpinBox;port->setObjectName("apiPort");port->setRange(1,65535);port->setValue(prefs.remoteControl.port);form->addRow("Port",port);
    auto *allowed=new QLineEdit(prefs.remoteControl.allowedPeers.join(", "));allowed->setObjectName("apiAllowedPeers");allowed->setPlaceholderText("127.0.0.1/32, 192.168.17.0/24");form->addRow("Allowed client IPs / CIDRs",allowed);
    auto *token=new QLineEdit;token->setObjectName("apiToken");token->setReadOnly(true);token->setEchoMode(QLineEdit::Password);token->setPlaceholderText(prefs.remoteControl.tokenHash.isEmpty()?"Generate a token":"Token configured — generate a new token to replace it");
    auto *tokenRow=new QHBoxLayout;tokenRow->addWidget(token,1);auto *generate=new QPushButton("Generate token");generate->setObjectName("apiGenerateToken");tokenRow->addWidget(generate);auto *copy=new QPushButton("Copy");copy->setObjectName("apiCopyToken");tokenRow->addWidget(copy);form->addRow("API token",tokenRow);
    connect(generate,&QPushButton::clicked,this,[token]{token->setText(RemoteControlConfig::newToken());});connect(copy,&QPushButton::clicked,this,[token]{if(!token->text().isEmpty())QApplication::clipboard()->setText(token->text());});
    const auto fileField=[&](const QString &label,const QString &value,const QString &name){auto *row=new QHBoxLayout;auto *edit=new QLineEdit(value);edit->setObjectName(name);row->addWidget(edit,1);auto *browse=new QPushButton("Browse…");row->addWidget(browse);connect(browse,&QPushButton::clicked,this,[this,edit]{const auto path=QFileDialog::getOpenFileName(this,"Select PEM file",{},"PEM files (*.pem *.crt *.key);;All files (*)");if(!path.isEmpty())edit->setText(path);});form->addRow(label,row);return edit;};
    auto *certificate=fileField("HTTPS certificate (optional)",prefs.remoteControl.certificate,"apiCertificate");auto *key=fileField("HTTPS private key",prefs.remoteControl.privateKey,"apiPrivateKey");
    auto *hint=new QLabel("Default: local access only. For another computer, choose a LAN IP and add that client's IP/CIDR.\nUse HTTPS on untrusted networks. Authentication: Bearer token, or Basic (user stel, password = token).\nOnly the token hash is saved. Copy the new token before closing Settings. Replacing it revokes the old token.");hint->setWordWrap(true);hint->setStyleSheet(uiStyleSheet("color:#526b7c;font-size:11px;"));layout->addWidget(hint);
    auto *state=new QLabel;state->setObjectName("apiServerState");state->setTextFormat(Qt::PlainText);state->setWordWrap(true);layout->addWidget(state);
    const auto refresh=[&api,state]{state->setText(api.isListening()?"Listening: "+api.endpoint()+"\nJSON: /api/v1/status    ·    Yealink: /servlet?key=number=1002":"Remote Control is stopped.");};refresh();connect(&api,&RemoteControlServer::stateChanged,this,refresh);
    auto *apply=new QPushButton("Apply Remote Control settings");apply->setObjectName("apiApply");apply->setEnabled(canSave);layout->addWidget(apply,0,Qt::AlignLeft);layout->addStretch();
    connect(apply,&QPushButton::clicked,this,[&api,&accounts,&prefs,&store,enabled,address,port,allowed,token,certificate,key,state]{
        const auto old=prefs;auto next=prefs;auto &c=next.remoteControl;c.enabled=enabled->isChecked();c.address=address->currentText().trimmed();c.port=port->value();c.allowedPeers=allowed->text().split(QRegularExpression("[,\\s]+"),Qt::SkipEmptyParts);c.certificate=certificate->text().trimmed();c.privateKey=key->text().trimmed();if(!token->text().isEmpty())c.tokenHash=RemoteControlConfig::hashToken(token->text());
        QString error=c.validate();if(!error.isEmpty()){state->setText(error);return;}
        if(!store.save(accounts,next,&error)){state->setText(error);return;}
        if(!api.configure(c,&error)){store.save(accounts,old);state->setText("Could not start Remote Control: "+error);return;}prefs=next;
        state->setText(api.isListening()?"Listening: "+api.endpoint()+"\nJSON: /api/v1/status    ·    Yealink: /servlet?key=number=1002":"Remote Control is stopped.");
    });
}
}
