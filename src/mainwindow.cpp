#include "mainwindow.h"
#include "calldialog.h"
#include "videoview.h"
#include "accountdialog.h"
#include "settingswidget.h"
#include "secretstore.h"
#include "branding.h"
#include "chatwidget.h"
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
    setWindowTitle("Stel • Softphone");setMinimumSize(790,620);resize(980,730);
    setWindowIcon(applicationIcon());
    auto *root=new QWidget;setCentralWidget(root);auto *layout=new QVBoxLayout(root);layout->setContentsMargins(22,18,22,12);layout->setSpacing(14);
    auto *heading=new QHBoxLayout;auto *brand=new QLabel;brand->setObjectName("brand");brand->setAccessibleName("Stel");brand->setPixmap(applicationWordmark(devicePixelRatioF()));heading->addWidget(brand);
    auto *tagline=new QLabel("YOUR DESK. CONNECTED.");tagline->setObjectName("muted");heading->addWidget(tagline);heading->addStretch();
    auto *dnd=new QCheckBox("Do not disturb");heading->addWidget(dnd);layout->addLayout(heading);
    auto *accountRow=new QHBoxLayout;accountRow->addWidget(new QLabel("Call using"));accountBox_=new QComboBox;accountBox_->setObjectName("callAccount");accountBox_->setMinimumWidth(250);accountRow->addWidget(accountBox_,1);
    auto *settingsButton=new QPushButton("Settings");settingsButton->setObjectName("openSettings");accountRow->addWidget(settingsButton);
    callAccountBar_=new QWidget;accountRow->setContentsMargins(0,0,0,0);callAccountBar_->setLayout(accountRow);layout->addWidget(callAccountBar_);
    status_=new QLabel("Starting call engine…");status_->setObjectName("accountStatus");layout->addWidget(status_);
    tabs_=new QTabWidget;tabs_->setObjectName("mainPages");layout->addWidget(tabs_,1);
    tabs_->addTab(createPhone(),"Phone");chat_=new ChatWidget(engine_,store_);tabs_->addTab(chat_,"Messages");tabs_->addTab(createContacts(),"Contacts");tabs_->addTab(createHistory(),"Recent calls");
    connect(chat_,&ChatWidget::settingsRequested,this,&MainWindow::showSettings);
    const auto updateUnread=[this](int count){tabs_->setTabText(tabs_->indexOf(chat_),count?"Messages ("+QString::number(count)+")":"Messages");};
    connect(chat_,&ChatWidget::unreadCountChanged,this,updateUnread);updateUnread(chat_->unreadCount());
    connect(tabs_,&QTabWidget::currentChanged,this,[this]{const bool phoneContext=tabs_->currentWidget()!=chat_;callAccountBar_->setVisible(phoneContext);status_->setVisible(phoneContext);});
    log_=new QPlainTextEdit;log_->setReadOnly(true);log_->setMaximumBlockCount(150);log_->setMaximumHeight(105);log_->setPlaceholderText("Registration and call events");log_->hide();
    auto *showLog=new QCheckBox("Show events");layout->addWidget(showLog);layout->addWidget(log_);connect(showLog,&QCheckBox::toggled,log_,&QWidget::setVisible);
    connect(&engine_,&SipEngine::errorOccurred,this,[this](const QString &s){appendLog(s);statusBar()->showMessage(s,15000);});
    connect(&engine_,&SipEngine::notification,this,[this](const QString &s){appendLog(s);statusBar()->showMessage(s,10000);});
    connect(&engine_,&SipEngine::accountState,this,[this](const QString &id,const QString &state,const QString &detail){
        accountStates_[id]=state;appendLog("Account "+id.left(8)+": "+state+" — "+detail);rebuildAccounts();
    });
    connect(&engine_,&SipEngine::callChanged,this,&MainWindow::updateCall);
    connect(&engine_,&SipEngine::callFinished,this,[this](const CallInfo &i){
        const auto old=callCache_.take(i.id);
        if(auto dialog=callDialogs_.value(i.id))dialog->finishCall(old.id.isEmpty()?i:old);
        store_.appendHistory({{"at",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},{"remote",i.remote},{"account",i.accountId},
            {"incoming",i.incoming},{"duration",qMax(i.duration,old.duration)},{"state",old.state},{"video",old.video}});
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
    settingsDialog_->setWindowFlag(Qt::WindowContextHelpButtonHint,false);settingsDialog_->resize(980,760);
    auto *settingsLayout=new QVBoxLayout(settingsDialog_);settingsLayout->setContentsMargins(10,10,10,10);settingsLayout->addWidget(createSettings());
    auto *settingsButtons=new QDialogButtonBox(QDialogButtonBox::Close);settingsButtons->setObjectName("settingsButtons");settingsLayout->addWidget(settingsButtons);
    connect(settingsButtons,&QDialogButtonBox::rejected,settingsDialog_,&QDialog::reject);
    connect(settingsDialog_,&QDialog::finished,this,[this]{if(!closing_){showNormal();raise();activateWindow();}});
    rebuildAccounts();refreshContacts();refreshHistory();
    if(QSystemTrayIcon::isSystemTrayAvailable()) {
        tray_=new QSystemTrayIcon(windowIcon(),this);tray_->setToolTip("Stel softphone");tray_->show();
        connect(tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason){showNormal();raise();activateWindow();});
    }
    auto *timer=new QTimer(this);timer->setInterval(1000);connect(timer,&QTimer::timeout,this,[this]{for(const auto &i:engine_.calls())updateCall(i);});timer->start();
    updateControls();statusBar()->showMessage(ready_?"liblinphone "+QString::fromUtf8(linphone_core_get_version())+"  •  SIP UDP / TCP / TLS":"Engine failed — see events");
    setStyleSheet(R"(
        QMainWindow, QDialog { background:#f2f5f7; color:#213344; }
        QWidget { font-family:'DejaVu Sans','Segoe UI'; font-size:12px; }
        QLabel#brand { font-size:34px; font-weight:700; letter-spacing:-2px; }
        QLabel#muted { color:#6d7d8c; font-size:10px; letter-spacing:2px; }
        QLabel#accountStatus { color:#087f74; background:#e1f2ee; border-radius:6px; padding:10px; }
        QPushButton { background:#fff; color:#213344; border:1px solid #d4dde4; border-radius:6px; padding:8px 12px; }
        QPushButton:hover { background:#e9f3f2; border-color:#18a999; }
        QPushButton:disabled { color:#a5afb8; background:#edf0f3; }
        QPushButton#primary { background:#138e81; color:white; border:0; font-weight:600; }
        QPushButton#danger { background:#bd4052; color:white; border:0; }
        QPushButton#primary:disabled, QPushButton#danger:disabled { color:#a5afb8; background:#edf0f3; border:1px solid #d4dde4; }
        QPushButton#key { font-size:22px; font-weight:500; min-height:36px; }
        QLineEdit, QComboBox, QSpinBox { background:white; border:1px solid #ced9e1; border-radius:5px; padding:8px; selection-background-color:#138e81; }
        QListWidget, QPlainTextEdit { background:white; border:1px solid #d7e0e7; border-radius:5px; padding:5px; }
        QListWidget::item { padding:9px; border-bottom:1px solid #edf1f4; }
        QListWidget::item:selected { background:#ddf2ee; color:#134d48; }
        QTabWidget::pane { border:1px solid #d7e0e7; background:#fafcfd; border-radius:5px; }
        QTabBar::tab { padding:10px 22px; color:#667888; }
        QTabBar::tab:selected { color:#087f74; border-bottom:3px solid #138e81; }
    )");
}
QWidget *MainWindow::createPhone() {
    auto *page=new QWidget;auto *layout=new QHBoxLayout(page);layout->setSpacing(20);layout->setContentsMargins(18,18,18,18);
    auto *dialer=new QWidget;dialer->setMaximumWidth(310);auto *left=new QVBoxLayout(dialer);left->setContentsMargins(0,0,0,0);
    auto *hint=new QLabel("MAKE A CALL");hint->setObjectName("muted");left->addWidget(hint);
    target_=new QLineEdit;target_->setObjectName("dialTarget");target_->setPlaceholderText("Number or SIP address");target_->setMinimumHeight(32);target_->setClearButtonEnabled(true);left->addWidget(target_);
    connect(target_,&QLineEdit::returnPressed,this,[this]{dial(false);});
    auto *keypad=new QGridLayout;const QString keys="123456789*0#";
    for(int n=0;n<keys.size();++n){const QChar ch=keys[n];auto *b=new QPushButton(QString(ch));b->setObjectName("key");keypad->addWidget(b,n/3,n%3);
        connect(b,&QPushButton::clicked,this,[this,ch]{target_->insert(ch);});}
    left->addLayout(keypad);auto *callRow=new QHBoxLayout;auto *audio=new QPushButton("Call");audio->setObjectName("primary");auto *video=new QPushButton("Video call");callRow->addWidget(audio);callRow->addWidget(video);left->addLayout(callRow);
    connect(audio,&QPushButton::clicked,this,[this]{dial(false);});connect(video,&QPushButton::clicked,this,[this]{dial(true);});
    auto *caption=new QLabel("Call controls and the DTMF keypad open in a separate window for each call.");caption->setWordWrap(true);caption->setObjectName("muted");left->addWidget(caption);left->addStretch();layout->addWidget(dialer,2);
    auto *right=new QVBoxLayout;auto *title=new QLabel("ACTIVE CALLS");title->setObjectName("muted");right->addWidget(title);
    calls_=new QListWidget;calls_->setObjectName("activeCalls");calls_->setMinimumHeight(130);right->addWidget(calls_,1);
    connect(calls_,&QListWidget::currentRowChanged,this,[this]{updateControls();});
    connect(calls_,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){showCall(item->data(Qt::UserRole).toString());});
    connect(calls_,&QListWidget::itemActivated,this,[this](QListWidgetItem *item){showCall(item->data(Qt::UserRole).toString());});
    detail_=new QLabel;detail_->setTextFormat(Qt::PlainText);detail_->setWordWrap(true);detail_->setMinimumHeight(48);right->addWidget(detail_);
    openCall_=new QPushButton("Open call window");openCall_->setObjectName("openCallWindow");right->addWidget(openCall_);
    connect(openCall_,&QPushButton::clicked,this,[this]{showCall(selectedCall());});layout->addLayout(right,3);return page;
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
    return settings_;
}
void MainWindow::showSettings(){if(closing_)return;settings_->openAccounts();settingsDialog_->show();settingsDialog_->raise();settingsDialog_->activateWindow();}
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
    const auto selected=accountBox_->currentData().toString();accountBox_->blockSignals(true);accountBox_->clear();
    for(const auto &a:accounts_)accountBox_->addItem((a.label.isEmpty()?a.username+'@'+a.domain:a.label)+"  ·  "+registrationLabel(accountStates_.value(a.id,a.enabled?"Unregistered":"Disabled")),a.id);
    accountBox_->setCurrentIndex(qMax(0,accountBox_->findData(selected)));accountBox_->blockSignals(false);
    if(accounts_.isEmpty())status_->setText("No accounts yet  •  Open Settings → Accounts to add one");
    else {const auto id=accountBox_->currentData().toString();for(const auto &a:accounts_)if(a.id==id)status_->setText((a.label.isEmpty()?a.username:a.label)+"  •  "+registrationLabel(accountStates_.value(id))+"  •  "+a.transport.toUpper()+" / "+mediaSecurityLabel(a.encryption));}
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
    item->setText((i.incoming?"↙  ":"↗  ")+i.remote+"\n"+localAccount+"  ·  "+callStateLabel(i.rawState)+"  ·  "+QString::number(i.duration)+"s"+(i.recording?"  ● REC":""));
    if(first || old.rawState!=i.rawState)appendLog(i.remote+"  "+i.state+"  "+i.message);
    auto dialog=callDialogs_.value(i.id);
    if(!dialog&&first){
        dialog=new CallDialog(engine_,i,this);callDialogs_[i.id]=dialog;
        connect(dialog,&QObject::destroyed,this,[this,id=i.id]{callDialogs_.remove(id);});
        connect(dialog,&CallDialog::newCallRequested,this,[this](const QString &accountId){
            accountBox_->setCurrentIndex(accountBox_->findData(accountId));tabs_->setCurrentIndex(0);showNormal();raise();activateWindow();target_->clear();target_->setFocus();
        });
        const auto screen=QApplication::primaryScreen();if(screen){const auto area=screen->availableGeometry();const int offset=24*((callDialogs_.size()-1)%5);dialog->move(qBound(area.left(),frameGeometry().center().x()-dialog->width()/2+offset,area.right()-dialog->width()),qBound(area.top(),area.center().y()-dialog->height()/2+offset,area.bottom()-dialog->height()));}
    }
    if(dialog){dialog->updateCall(i,accountDetails,transport);if(first)showCall(i.id);}
    if(first && i.incoming){QApplication::alert(dialog?static_cast<QWidget*>(dialog):this);if(tray_)tray_->showMessage("Incoming call",i.remote,QSystemTrayIcon::Information,10000);QApplication::beep();}
    updateControls();refreshCallDialogs();
}
void MainWindow::updateControls(){
    if(!openCall_||!detail_)return;
    const auto id=selectedCall();const auto i=callCache_.value(id);openCall_->setEnabled(!id.isEmpty()&&callDialogs_.value(id));
    detail_->setText(id.isEmpty()?"Ready when you are. Incoming and outgoing calls open their own window.":i.remote+"\n"+callStateLabel(i.rawState)+"  •  Media: "+(i.encryption.isEmpty()?"negotiating":i.encryption)+"  •  "+QString::number(i.duration)+"s"+(i.audioCodec.isEmpty()?QString():"\nAudio: "+i.audioCodec)+(i.videoCodec.isEmpty()?QString():"  ·  Video: "+i.videoCodec));
}
void MainWindow::refreshContacts(){contacts_->clear();for(const auto &o:store_.contacts()){auto *item=new QListWidgetItem(o["name"].toString()+"\n"+o["uri"].toString(),contacts_);item->setData(Qt::UserRole,o["uri"].toString());}}
void MainWindow::refreshHistory(){history_->clear();for(const auto &o:store_.history()){const auto date=QDateTime::fromString(o["at"].toString(),Qt::ISODate).toLocalTime();auto *item=new QListWidgetItem((o["incoming"].toBool()?"↙  ":"↗  ")+o["remote"].toString()+"\n"+date.toString("yyyy-MM-dd HH:mm")+"  ·  "+QString::number(o["duration"].toInt())+"s",history_);item->setData(Qt::UserRole,o["remote"].toString());}}
}
