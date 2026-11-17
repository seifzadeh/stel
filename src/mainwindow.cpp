#include "mainwindow.h"
#include "deskphone.h"
#include <QShortcut>
#include "calldialog.h"
#include "videoview.h"
#include "accountdialog.h"
#include "settingswidget.h"
#include "secretstore.h"
#include "branding.h"
#include "chatwidget.h"
#include "faxwidget.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QScreen>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace stel {
MainWindow::MainWindow(const QString &directory,QWidget *parent):QMainWindow(parent),store_(directory),engine_(this) {
    setLayoutDirection(Qt::LeftToRight);
    setWindowTitle("Stel • Softphone");setWindowFlag(Qt::FramelessWindowHint);setMinimumSize(1000,750);resize(1200,900);
    setWindowIcon(applicationIcon());
    desk_=new DeskPhone;setCentralWidget(desk_);
    accountBox_=desk_->account;target_=desk_->target;calls_=desk_->calls;detail_=desk_->detail;status_=desk_->status;openCall_=desk_->openCall;tabs_=desk_->pages;
    auto *settingsButton=desk_->settings;
    auto *dnd=new QCheckBox("Do not disturb",this);dnd->hide();
    chat_=new ChatWidget(engine_,store_);chat_->setDeskMode();tabs_->addTab(chat_,"Messages");
    auto *faxPage=new QWidget;auto *faxLayout=new QVBoxLayout(faxPage);faxLayout->setContentsMargins(18,10,18,10);auto *faxTitle=new QLabel("Fax");faxTitle->setStyleSheet("color:white;font-size:28px;font-weight:bold;");faxLayout->addWidget(faxTitle);auto *faxHint=new QLabel("PDF, text and images · standard A4 pages\nT.38 or G.711 pass-through, chosen for each fax.");faxHint->setStyleSheet("color:#d6e9ff;font-size:15px;");faxHint->setWordWrap(true);faxLayout->addWidget(faxHint);auto *faxSummary=new QLabel;faxSummary->setObjectName("deskFaxSummary");faxSummary->setStyleSheet("color:#e9f4ff;font-size:16px;");faxLayout->addWidget(faxSummary);auto *faxActions=new QHBoxLayout;for(int n=0;n<3;++n){auto *button=new QPushButton(QStringList{"Send Fax","Inbox","Outbox"}[n]);button->setStyleSheet("background:#164c81;color:white;border:1px solid #479de7;padding:8px;");faxActions->addWidget(button);connect(button,&QPushButton::clicked,this,[this,n]{if(auto *tab=fax_->findChild<QPushButton*>("faxDeskTab"+QString::number(n)))tab->click();});}faxLayout->addLayout(faxActions);tabs_->addTab(faxPage,"Fax");tabs_->addTab(createContacts(),"Contacts");tabs_->addTab(createHistory(),"Recent calls");
    fax_=new FaxWidget(engine_,desk_,true);desk_->dockFax(fax_);
    connect(fax_,&FaxWidget::contactRequested,this,[this]{const auto contacts=store_.contacts();QStringList names;for(const auto &contact:contacts)names<<contact["name"].toString()+"  ·  "+contact["uri"].toString();if(names.isEmpty()){tabs_->setCurrentIndex(3);return;}bool ok=false;const auto name=QInputDialog::getItem(this,"Fax contact","Send fax to",names,0,false,&ok);if(ok)fax_->setDestination(contacts[names.indexOf(name)]["uri"].toString());});
    connect(chat_,&ChatWidget::settingsRequested,this,&MainWindow::showSettings);
    const auto updateUnread=[this](int count){tabs_->setTabText(1,count?"Messages ("+QString::number(count)+")":"Messages");updateControls();};
    connect(chat_,&ChatWidget::unreadCountChanged,this,updateUnread);
    connect(tabs_,&QTabWidget::currentChanged,this,[this](int index){if(index==2){fax_->setFocus();desk_->target->clearFocus();}});
    log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(150);log_->setPlaceholderText("Registration and call events");
    auto *events=new QDialog(this);events->setWindowTitle("Stel • Events");events->resize(800,420);auto *eventsLayout=new QVBoxLayout(events);eventsLayout->addWidget(log_);
    auto *eventShortcut=new QShortcut(QKeySequence("Ctrl+L"),this);connect(eventShortcut,&QShortcut::activated,events,&QDialog::show);
    connect(target_,&QLineEdit::returnPressed,this,[this]{dial(false);});
    connect(calls_,&QListWidget::currentRowChanged,this,[this]{updateControls();});
    connect(calls_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){showCall(item->data(Qt::UserRole).toString());});
    connect(openCall_,&QPushButton::clicked,this,[this]{showCall(selectedCall());});
    connect(desk_,&DeskPhone::digitPressed,this,[this](QChar digit){const auto id=selectedCall();const auto call=callCache_.value(id);if(!id.isEmpty()&&(call.rawState==LinphoneCallStreamsRunning||call.rawState==LinphoneCallConnected)){if(auto dialog=callDialogs_.value(id)){const auto name="callDtmf_"+(digit=='*'?QString("star"):digit=='#'?QString("hash"):QString(digit));if(auto *button=dialog->findChild<QPushButton*>(name))button->click();}}else{target_->insert(digit);tabs_->setCurrentIndex(0);}});
    connect(desk_,&DeskPhone::expansionConfigureRequested,this,&MainWindow::configureExpansion);
    connect(desk_,&DeskPhone::expansionPressed,this,[this](int index){
        const auto key=prefs_.expansionKeys.value(index);if(key.type=="disabled"){configureExpansion(index);return;}
        const int account=accountBox_->findData(key.accountId);
        if(account<0){statusBar()->showMessage("Choose an existing account for this expansion key.",10000);configureExpansion(index);return;}
        accountBox_->setCurrentIndex(account);target_->setText(key.target);tabs_->setCurrentIndex(0);dial(false);
    });
    connect(&engine_,&SipEngine::blfChanged,desk_,&DeskPhone::setExpansionState);
    connect(desk_->audioCall,&QPushButton::clicked,this,[this]{const auto call=callCache_.value(selectedCall());if(call.incoming&&(call.rawState==LinphoneCallIncomingReceived||call.rawState==LinphoneCallIncomingEarlyMedia))engine_.answer(call.id,false);else dial(false);});
    connect(desk_->videoCall,&QPushButton::clicked,this,[this]{dial(true);});
    connect(desk_->hangup,&QPushButton::clicked,this,[this]{if(!selectedCall().isEmpty())engine_.hangup(selectedCall());});
    connect(desk_->hold,&QPushButton::clicked,this,[this]{const auto call=callCache_.value(selectedCall());if(!call.id.isEmpty())engine_.hold(call.id,call.rawState!=LinphoneCallPaused&&call.rawState!=LinphoneCallPausing);});
    connect(desk_->mute,&QPushButton::clicked,this,[this]{const auto call=callCache_.value(selectedCall());if(!call.id.isEmpty())engine_.muteCall(call.id,!call.muted);});
    const auto action=[this](const char *name){const auto id=selectedCall();showCall(id);if(auto dialog=callDialogs_.value(id))if(auto *button=dialog->findChild<QPushButton*>(name))button->click();};
    connect(desk_->transfer,&QPushButton::clicked,this,[action]{action("callTransfer");});
    connect(desk_->conference,&QPushButton::clicked,this,[action]{action("callConference");});
    connect(desk_->record,&QPushButton::clicked,this,[action]{action("callRecord");});
    connect(desk_->addCall,&QPushButton::clicked,this,[this]{tabs_->setCurrentIndex(0);calls_->clearSelection();calls_->setCurrentRow(-1);updateControls();target_->clear();target_->setFocus();});
    connect(desk_->redial,&QPushButton::clicked,this,[this]{if(history_->count()){target_->setText(history_->item(0)->data(Qt::UserRole).toString());dial(false);}});
    connect(desk_->speaker,&QPushButton::clicked,this,[this]{
        auto *menu=new QMenu(this);menu->setAttribute(Qt::WA_DeleteOnClose);const auto devices=engine_.soundDevices(false,true);
        for(const auto &device:devices){auto *option=menu->addAction(device);option->setCheckable(true);option->setChecked(device==prefs_.outputDevice);connect(option,&QAction::triggered,this,[this,device]{auto updated=prefs_;updated.outputDevice=device;if(engine_.applyDevices(updated)){prefs_=updated;save();desk_->speaker->setChecked(true);desk_->speaker->setToolTip(device);}});}
        if(devices.isEmpty()){auto *empty=menu->addAction("No playback device available");empty->setEnabled(false);}menu->popup(desk_->speaker->mapToGlobal(QPoint(0,0)));
    });
    connect(&engine_,&SipEngine::errorOccurred,this,[this](const QString &s){appendLog(s);statusBar()->showMessage(s,15000);});
    connect(&engine_,&SipEngine::notification,this,[this](const QString &s){appendLog(s);statusBar()->showMessage(s,10000);});
    connect(&engine_,&SipEngine::accountState,this,[this](const QString &id,const QString &state,const QString &detail){
        accountStates_[id]=state;appendLog("Account "+id.left(8)+": "+state+" — "+detail);rebuildAccounts();
    });
    connect(&engine_,&SipEngine::callChanged,this,&MainWindow::updateCall);
    connect(&engine_,&SipEngine::faxChanged,this,[this]{updateControls();});
    connect(&engine_,&SipEngine::callFinished,this,[this](const CallInfo &i){
        const auto old=callCache_.take(i.id);
        if(auto dialog=callDialogs_.value(i.id))dialog->finishCall(old.id.isEmpty()?i:old);
        store_.appendHistory({{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"remote",i.remote},{"account",i.accountId},
            {"incoming",i.incoming},{"paging",i.paging},{"pagingAutoAnswered",i.pagingAutoAnswered},{"duration",qMax(i.duration,old.duration)},{"state",old.state},{"video",old.video}});
        for(int row=0;row<calls_->count();++row)if(calls_->item(row)->data(Qt::UserRole).toString()==i.id){delete calls_->takeItem(row);break;}
        refreshHistory();updateControls();refreshCallDialogs();
    });
    connect(settingsButton,&QPushButton::clicked,this,&MainWindow::showSettings);
    connect(accountBox_,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{
        const QString id=accountBox_->currentData().toString();status_->setText(accountStates_.value(id,"No account configured"));
        for(const auto &a:accounts_)if(a.id==id)status_->setText(a.label+"  •  "+registrationLabel(accountStates_.value(id))+"  •  "+a.transport.toUpper()+" / "+mediaSecurityLabel(a.encryption));
    });
    QString loadError;settingsValid_=store_.load(accounts_,prefs_,&loadError);if(!loadError.isEmpty())appendLog(loadError);
    dnd->setChecked(prefs_.dnd);connect(dnd,&QCheckBox::toggled,this,[this](bool on){prefs_.dnd=on;engine_.setDnd(on);save();});
    engine_.setVideoRendererInstaller(installQtVideoRenderer);ready_=engine_.start(prefs_,store_.directory());
    if(ready_)for(const auto &a:accounts_) {
        auto runtime=a;if(a.credentialsRequired)runtime.registerEnabled=false;
        engine_.addAccount(runtime);if(a.credentialsRequired)accountStates_[a.id]="CredentialsRequired";
    }
    settingsDialog_=new QDialog(this);settingsDialog_->setObjectName("settingsDialog");settingsDialog_->setWindowTitle("Stel • Settings");
    settingsDialog_->setWindowFlag(Qt::WindowContextHelpButtonHint,false);settingsDialog_->resize(1080,850);
    auto *settingsLayout=new QVBoxLayout(settingsDialog_);settingsLayout->setContentsMargins(10,10,10,10);settingsLayout->addWidget(createSettings());settingsLayout->addWidget(dnd);dnd->show();
    auto *eventsButton=new QPushButton("Call and registration events…");settingsLayout->addWidget(eventsButton);connect(eventsButton,&QPushButton::clicked,events,&QDialog::show);
    auto *settingsButtons=new QDialogButtonBox(QDialogButtonBox::Close);settingsButtons->setObjectName("settingsButtons");settingsLayout->addWidget(settingsButtons);
    connect(settingsButtons,&QDialogButtonBox::rejected,settingsDialog_,&QDialog::reject);
    connect(settingsDialog_,&QDialog::finished,this,[this]{if(!closing_){if(isMinimized())showNormal();else show();raise();activateWindow();}});
    rebuildAccounts();refreshContacts();refreshHistory();refreshExpansion();
    if(QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_=new QSystemTrayIcon(windowIcon(),this);tray_->setToolTip("Stel softphone");tray_->show();
        connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason){showNormal();raise();activateWindow();});
    }
    auto *timer=new QTimer(this);timer->setInterval(1000);connect(timer,&QTimer::timeout,this,[this]{for(const auto &i:engine_.calls())updateCall(i);});timer->start();
    updateControls();statusBar()->hide();statusBar()->showMessage(ready_?"liblinphone "+QString::fromUtf8(linphone_core_get_version())+"  •  SIP UDP / TCP / TLS":"Engine failed — see events");
    setStyleSheet(R"(
        QMainWindow, QDialog { background:#e1e5eb; color:#213344; }
        QWidget { font-family:'DejaVu Sans','Segoe UI'; }
        QLabel#brand { font-size:34px; font-weight:700; letter-spacing:-2px; }
        QLabel#muted { color:#6d7d8c; font-size:10px; letter-spacing:2px; }
        QLabel#accountStatus { color:#086bb2; background:#ddebf8; border-radius:6px; padding:10px; }
        QPushButton { background:#fff; color:#213344; border:1px solid #d4dde4; border-radius:6px; padding:8px 12px; }
        QPushButton:hover { background:#e7f1fa; border-color:#168fdf; }
        QPushButton:disabled { color:#a5afb8; background:#edf0f3; }
        QPushButton#primary { background:#087bd2; color:white; border:0; font-weight:600; }
        QPushButton#danger { background:#bd4052; color:white; border:0; }
        QPushButton#primary:disabled, QPushButton#danger:disabled { color:#a5afb8; background:#edf0f3; border:1px solid #d4dde4; }
        QPushButton#key { font-size:22px; font-weight:500; min-height:36px; }
        QLineEdit, QComboBox, QSpinBox { background:white; border:1px solid #ced9e1; border-radius:5px; padding:8px; selection-background-color:#087bd2; }
        QListWidget, QPlainTextEdit { background:white; border:1px solid #d7e0e7; border-radius:5px; padding:5px; }
        QListWidget::item { padding:9px; border-bottom:1px solid #edf1f4; }
        QListWidget::item:selected { background:#dcecfb; color:#164a79; }
        QTabWidget::pane { border:1px solid #d7e0e7; background:#fafcfd; border-radius:5px; }
        QTabBar::tab { padding:10px 22px; color:#667888; }
        QTabBar::tab:selected { color:#086bb2; border-bottom:3px solid #087bd2; }
    )");
}
QWidget *MainWindow::createContacts() {
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);contacts_=new QListWidget;layout->addWidget(contacts_,1);auto *row=new QHBoxLayout;auto *add=new QPushButton("+ Add contact");auto *remove=new QPushButton("Remove");auto *call=new QPushButton("Call selected");row->addWidget(add);row->addWidget(remove);row->addStretch();row->addWidget(call);layout->addLayout(row);
    connect(add,&QPushButton::clicked,this,[this]{bool ok;auto name=QInputDialog::getText(this,"Add contact","Name",QLineEdit::Normal,{},&ok);if(!ok||name.isEmpty())return;auto uri=QInputDialog::getText(this,"Add contact","Extension or SIP address",QLineEdit::Normal,{},&ok);if(!ok||uri.isEmpty())return;auto all=store_.contacts();all.append({{"name",name},{"uri",uri}});store_.saveContacts(all);refreshContacts();});
    connect(remove,&QPushButton::clicked,this,[this]{int row=contacts_->currentRow();if(row<0)return;auto all=store_.contacts();all.removeAt(row);store_.saveContacts(all);refreshContacts();});
    auto callContact=[this]{auto *item=contacts_->currentItem();if(item){target_->setText(item->data(Qt::UserRole).toString());tabs_->setCurrentIndex(0);dial(false);}};
    connect(call,&QPushButton::clicked,this,callContact);connect(contacts_,&QListWidget::itemDoubleClicked,this,[callContact](QListWidgetItem*){callContact();});return page;
}
QWidget *MainWindow::createHistory() {
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);history_=new QListWidget;layout->addWidget(history_);auto *hint=new QLabel("Double-click a recent call to copy its address to the dialer.");layout->addWidget(hint);
    connect(history_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem *item){target_->setText(item->data(Qt::UserRole).toString());tabs_->setCurrentIndex(0);});return page;
}
QWidget *MainWindow::createSettings() {
    settings_=new SettingsWidget(engine_,accounts_,prefs_,store_,settingsValid_,settingsDialog_);
    connect(settings_,&SettingsWidget::accountsChanged,this,&MainWindow::rebuildAccounts);
    connect(settings_,&SettingsWidget::expansionChanged,this,&MainWindow::refreshExpansion);
    return settings_;
}
void MainWindow::showSettings(){if(closing_)return;settings_->openAccounts();settingsDialog_->show();settingsDialog_->raise();settingsDialog_->activateWindow();}
void MainWindow::configureExpansion(int key){if(closing_)return;settings_->openExpansion(key);settingsDialog_->show();settingsDialog_->raise();settingsDialog_->activateWindow();}
void MainWindow::refreshExpansion(){desk_->setExpansionKeys(prefs_.expansionKeys);engine_.setExpansionKeys(prefs_.expansionKeys);for(int n=0;n<ExpansionKeyCount;++n)desk_->setExpansionState(engine_.blfInfo(n));}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(credentialOperationActive()){event->ignore();return;}
    if(closing_){event->accept();return;}
    if(confirmingExit_){event->ignore();return;}
    confirmingExit_=true;
    QMessageBox question(QMessageBox::Question,"Close Stel?","Close Stel and disconnect all accounts?",QMessageBox::Yes|QMessageBox::Cancel,this);
    question.setObjectName("confirmExit");question.setDefaultButton(QMessageBox::Cancel);question.setEscapeButton(QMessageBox::Cancel);
    const bool confirmed=question.exec()==QMessageBox::Yes;confirmingExit_=false;
    if(!confirmed){event->ignore();return;}
    closing_=true;settingsDialog_->hide();for(auto dialog:callDialogs_)if(dialog)dialog->hide();if(tray_)tray_->hide();
    setEnabled(false);engine_.shutdown();callCache_.clear();accountStates_.clear();calls_->clear();
    for(auto &a:accounts_){a.password.clear();a.keyPassword.clear();a.turnPassword.clear();}
    event->accept();
}
void MainWindow::appendLog(const QString &s){if(log_)log_->appendPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+s);}
QString MainWindow::selectedCall()const {auto *item=calls_->currentItem();return item?item->data(Qt::UserRole).toString():QString();}
void MainWindow::save(){QString error;if(!settingsValid_){statusBar()->showMessage("Settings were not overwritten because the original file is invalid.");return;}if(!store_.save(accounts_,prefs_,&error))appendLog(error);}
void MainWindow::rebuildAccounts(){
    chat_->setAccounts(accounts_);
    fax_->setAccounts(accounts_);
    const auto selected=accountBox_->currentData().toString();accountBox_->blockSignals(true);accountBox_->clear();
    for(const auto &a:accounts_)accountBox_->addItem((a.label.isEmpty()?a.username+'@'+a.domain:a.label)+"  ·  "+registrationLabel(accountStates_.value(a.id,a.enabled?"Unregistered":"Disabled")),a.id);
    accountBox_->setCurrentIndex(qMax(0,accountBox_->findData(selected)));accountBox_->blockSignals(false);
    if(accounts_.isEmpty())status_->setText("No accounts yet  •  Open Settings → Accounts to add one");
    else {const auto id=accountBox_->currentData().toString();for(const auto &a:accounts_)if(a.id==id)status_->setText((a.label.isEmpty()?a.username:a.label)+"  •  "+registrationLabel(accountStates_.value(id))+"  •  "+a.transport.toUpper()+" / "+mediaSecurityLabel(a.encryption));}
    updateControls();
}
void MainWindow::dial(bool video){
    if(!engine_.dial(accountBox_->currentData().toString(),target_->text(),video))QMessageBox::warning(this,"Call",engine_.lastError());

}
MainWindow::~MainWindow() {
    closing_=true;engine_.shutdown();
    const auto dialogs=callDialogs_;callDialogs_.clear();
    for(auto dialog:dialogs)if(dialog){disconnect(dialog,nullptr,this,nullptr);delete dialog;}
}
void MainWindow::showCall(const QString &id) {
    if(auto dialog=callDialogs_.value(id)){dialog->showNormal();dialog->raise();dialog->activateWindow();}
}
void MainWindow::refreshCallDialogs() {
    const auto peers=engine_.calls();for(auto dialog:callDialogs_)if(dialog)dialog->refreshPeers(peers);
}
void MainWindow::updateCall(const CallInfo &i){
    // Released is an SDK cleanup notification; retain the real final state for history.
    if(i.rawState==LinphoneCallReleased||closing_)return;
    const bool first=!callCache_.contains(i.id);const auto old=callCache_.value(i.id);callCache_[i.id]=i;
    QListWidgetItem *item=nullptr;for(int row=0;row<calls_->count();++row)if(calls_->item(row)->data(Qt::UserRole).toString()==i.id)item=calls_->item(row);
    if(!item){item=new QListWidgetItem(calls_);item->setData(Qt::UserRole,i.id);calls_->setCurrentItem(item);}
    QString localAccount=i.accountName,accountDetails=i.accountName,transport;
    for(const auto &a:accounts_)if(a.id==i.accountId){localAccount=a.label.isEmpty()?a.username:a.label;accountDetails=localAccount+" · "+a.username+"@"+a.domain;transport=a.transport;break;}
    item->setData(Qt::UserRole+1,i.accountId);item->setData(Qt::UserRole+2,i.incoming);item->setData(Qt::UserRole+3,int(i.rawState));
    item->setText("Line "+QString::number(calls_->row(item)+1)+"\n"+(i.rawState==LinphoneCallStreamsRunning?(i.paging?QString("Paging"):QString("In Call")):callStateLabel(i.rawState))+" – "+QTime(0,0).addSecs(i.duration).toString("hh:mm:ss"));
    item->setToolTip(localAccount+"\n"+i.remote);
    if(first || old.rawState!=i.rawState)appendLog(i.remote+"  "+i.state+"  "+i.message);
    auto dialog=callDialogs_.value(i.id);
    if(!dialog&&first){
        dialog=new CallDialog(engine_,i,this);callDialogs_[i.id]=dialog;
        connect(dialog,&CallDialog::faxRequested,this,[this](const QString &id){for(const auto &call:engine_.calls())if(call.id==id){if(fax_->receiveCall(call))tabs_->setCurrentIndex(2);break;}});
        connect(dialog,&QObject::destroyed,this,[this,id=i.id]{callDialogs_.remove(id);});
        connect(dialog,&CallDialog::newCallRequested,this,[this](const QString &accountId){
            accountBox_->setCurrentIndex(accountBox_->findData(accountId));tabs_->setCurrentIndex(0);showNormal();raise();activateWindow();target_->clear();target_->setFocus();
        });
        const auto screen=QApplication::primaryScreen();if(screen){const auto area=screen->availableGeometry();const int offset=24*((callDialogs_.size()-1)%5);dialog->move(qBound(area.left(),frameGeometry().center().x()-dialog->width()/2+offset,area.right()-dialog->width()),qBound(area.top(),area.center().y()-dialog->height()/2+offset,area.bottom()-dialog->height()));}
    }
    if(dialog){dialog->updateCall(i,accountDetails,transport);if(first)showCall(i.id);}
    if(first && i.incoming){QApplication::alert(dialog?static_cast<QWidget*>(dialog):this);if(tray_)tray_->showMessage(i.paging?"Incoming page":"Incoming call",i.remote,QSystemTrayIcon::Information,10000);if(!i.paging)QApplication::beep();}
    updateControls();refreshCallDialogs();
}
void MainWindow::updateControls(){
    if(!openCall_||!detail_)return;
    const auto id=selectedCall();const auto i=callCache_.value(id);openCall_->setEnabled(!id.isEmpty()&&callDialogs_.value(id));
    detail_->setText(id.isEmpty()?"Ready when you are. Incoming and outgoing calls open their own window.":i.remote+"\n"+callStateLabel(i.rawState)+"  •  Media: "+(i.encryption.isEmpty()?"negotiating":i.encryption)+"  •  "+QString::number(i.duration)+"s"+(i.audioCodec.isEmpty()?QString():"\nAudio: "+i.audioCodec)+(i.videoCodec.isEmpty()?QString():"  ·  Video: "+i.videoCodec));
    if(desk_){int registered=0,received=0;bool busy=false;for(const auto &a:accounts_)registered+=registrationLabel(accountStates_.value(a.id))=="Registered";for(const auto &fax:engine_.faxes()){received+=fax.incoming;busy|=fax.state=="dialing"||fax.state=="negotiating"||fax.state=="sending"||fax.state=="receiving";}
        desk_->refresh(i,calls_->count(),registered,accounts_.size(),chat_?chat_->unreadCount():0,received,busy);
        if(auto dialog=callDialogs_.value(id)){
            const QPair<DeskButton*,const char*> controls[]={{desk_->hold,"callHold"},{desk_->mute,"callMute"},{desk_->record,"callRecord"},{desk_->transfer,"callTransfer"},{desk_->conference,"callConference"}};
            for(const auto &control:controls)if(auto *button=dialog->findChild<QPushButton*>(control.second))control.first->setEnabled(button->isEnabled());
            desk_->findChild<DeskButton*>("deskAction0")->setEnabled(desk_->hold->isEnabled());desk_->findChild<DeskButton*>("deskAction1")->setEnabled(desk_->transfer->isEnabled());
        }
        if(auto *summary=findChild<QLabel*>("deskFaxSummary"))summary->setText(QString("%1 received · %2 sent").arg(received).arg(engine_.faxes().size()-received));}

}
void MainWindow::refreshContacts(){contacts_->clear();for(const auto &o:store_.contacts()){auto *item=new QListWidgetItem(o["name"].toString()+"\n"+o["uri"].toString(),contacts_);item->setData(Qt::UserRole,o["uri"].toString());}}
void MainWindow::refreshHistory(){history_->clear();for(const auto &o:store_.history()){const auto date=QDateTime::fromString(o["at"].toString(),Qt::ISODate).toLocalTime();auto *item=new QListWidgetItem((o["paging"].toBool()?QString("Page  ·  "):(o["incoming"].toBool()?QString("↙  "):QString("↗  ")))+o["remote"].toString()+"\n"+date.toString("yyyy-MM-dd HH:mm")+"  ·  "+QString::number(o["duration"].toInt())+"s",history_);item->setData(Qt::UserRole,o["remote"].toString());}}
}
