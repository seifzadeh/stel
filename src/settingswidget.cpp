#include "settingswidget.h"
#include "codecselector.h"
#include "branding.h"
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

namespace stel {
QString registrationLabel(const QString &s) {
    if(s=="LinphoneRegistrationOk")return "Registered";
    if(s=="LinphoneRegistrationProgress"||s=="LinphoneRegistrationRefreshing"||s=="Registering")return "Registering…";
    if(s=="LinphoneRegistrationFailed")return "Registration failed";
    if(s=="Disabled")return "Disabled";
    if(s=="CredentialsRequired")return "Password required";
    if(s=="Unregistering")return "Unregistering…";
    return "Unregistered";
}
static QColor stateColor(const QString &state) {
    if(state=="LinphoneRegistrationOk")return QColor("#12a075");
    if(state=="LinphoneRegistrationFailed")return QColor("#c84a56");
    if(state.contains("Progress")||state.contains("Refreshing")||state=="Registering"||state=="Unregistering")return QColor("#d69625");
    return QColor("#9aa8b4");
}
static QIcon dotIcon(const QString &state) {
    QPixmap pixmap(18,18);pixmap.fill(Qt::transparent);QPainter p(&pixmap);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);p.setBrush(stateColor(state));p.drawEllipse(4,4,10,10);return QIcon(pixmap);
}
static QLabel *note(const QString &text) {auto *label=new QLabel(text);label->setWordWrap(true);label->setStyleSheet("color:#687b8a; font-size:11px;");return label;}
SettingsWidget::SettingsWidget(SipEngine &engine,QVector<Account> &accounts,Preferences &prefs,Store &store,bool canSave,QWidget *parent)
    :QWidget(parent),engine_(engine),accounts_(accounts),prefs_(prefs),store_(store),canSave_(canSave) {
    setObjectName("settingsWidget");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(12,12,12,12);
    pages_=new QTabWidget;pages_->setObjectName("settingsPages");layout->addWidget(pages_);
    pages_->addTab(accountsPage(),navigationIcon(NavigationIcon::Accounts),"Accounts");pages_->addTab(audioPage(),navigationIcon(NavigationIcon::Audio),"Audio");pages_->addTab(videoPage(),navigationIcon(NavigationIcon::Video),"Video");pages_->addTab(networkPage(),navigationIcon(NavigationIcon::Network),"Network");pages_->addTab(securityPage(),navigationIcon(NavigationIcon::Security),"Security");pages_->addTab(aboutPage(),navigationIcon(NavigationIcon::Info),"About");
    styleNavigationTabs(pages_,NavigationLevel::Settings);
    for(const auto &a:accounts_) {
        states_[a.id]=a.credentialsRequired?"CredentialsRequired":engine_.registrationState(a.id);
        if(a.credentialsRequired)details_[a.id]="No saved password was found. Enter the password once and press Register; Remember passwords is enabled by default.";
    }
    connect(&engine_,&SipEngine::accountState,this,[this](const QString &id,const QString &state,const QString &detail){
        states_[id]=state;details_[id]=QTime::currentTime().toString("HH:mm:ss")+"  "+detail;
        rebuildList();if(id==editingId_)updateAccountStatus();
    });
    connect(&engine_,&SipEngine::errorOccurred,this,[this](const QString &s){if(pages_->currentIndex()==0)report(s,true);});
    rebuildList();if(list_->count())selectAccount(list_->item(0)->data(Qt::UserRole).toString());else updateAccountStatus();
}
void SettingsWidget::openAccounts(){pages_->setCurrentIndex(0);}
QWidget *SettingsWidget::accountsPage() {
    auto *page=new QWidget;auto *layout=new QHBoxLayout(page);layout->setSpacing(14);
    auto *left=new QVBoxLayout;left->addWidget(new QLabel("SIP accounts"));list_=new QListWidget;list_->setObjectName("settingsAccounts");list_->setMinimumWidth(185);list_->setMaximumWidth(235);left->addWidget(list_,1);
    auto *add=new QPushButton("+ Add account");add->setObjectName("addAccount");remove_=new QPushButton("Remove account");remove_->setObjectName("removeAccount");left->addWidget(add);left->addWidget(remove_);layout->addLayout(left);
    auto *right=new QVBoxLayout;auto *stateRow=new QHBoxLayout;registrationDot_=new QLabel;registrationDot_->setFixedSize(16,16);registrationDot_->setObjectName("registrationDot");registrationText_=new QLabel;registrationText_->setObjectName("registrationStatus");stateRow->addWidget(registrationDot_);stateRow->addWidget(registrationText_);stateRow->addStretch();right->addLayout(stateRow);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setStyleSheet("QScrollArea, QScrollArea > QWidget > QWidget {background:#fafcfd;}");editor_=new AccountEditor;editor_->setObjectName("accountEditor");scroll->setWidget(editor_);right->addWidget(scroll,1);
    auto *actions=new QHBoxLayout;saveAccount_=new QPushButton("Save account");saveAccount_->setObjectName("saveAccount");register_=new QPushButton("Register");register_->setObjectName("registerAccount");unregister_=new QPushButton("Unregister");unregister_->setObjectName("unregisterAccount");
    actions->addWidget(saveAccount_);actions->addStretch();actions->addWidget(register_);actions->addWidget(unregister_);right->addLayout(actions);
    accountNetwork_=note("");accountNetwork_->setObjectName("accountNetwork");right->addWidget(accountNetwork_);
    response_=new QPlainTextEdit;response_->setObjectName("registrationResponse");response_->setReadOnly(true);response_->setMaximumHeight(65);response_->setPlaceholderText("Registration result and server response appear here.");right->addWidget(response_);layout->addLayout(right,1);
    connect(add,&QPushButton::clicked,this,&SettingsWidget::addAccount);connect(remove_,&QPushButton::clicked,this,&SettingsWidget::removeAccount);
    connect(saveAccount_,&QPushButton::clicked,this,[this]{commitAccount(false);});connect(register_,&QPushButton::clicked,this,[this]{commitAccount(true);});
    connect(unregister_,&QPushButton::clicked,this,[this]{
        for(auto &a:accounts_)if(a.id==editingId_) {
            if(!engine_.setRegistration(a.id,false)){report(engine_.lastError(),true);return;}
            a.registerEnabled=false;QString error;if(!persist(&error))report(error,true);else emit accountsChanged();return;
        }
    });
    connect(list_,&QListWidget::currentRowChanged,this,[this](int row){if(row>=0)selectAccount(list_->item(row)->data(Qt::UserRole).toString());});
    return page;
}
void SettingsWidget::rebuildList(const QString &select) {
    const QString selected=select.isEmpty()?editingId_:select;const QSignalBlocker blocker(list_);list_->clear();
    auto append=[&](const Account &a,bool unsaved){auto *item=new QListWidgetItem(dotIcon(states_.value(a.id)),(a.label.isEmpty()?a.username:a.label)+(unsaved?" *":"")+"\n"+(a.username.isEmpty()?"New account":a.username+'@'+a.domain),list_);item->setData(Qt::UserRole,a.id);item->setToolTip(registrationLabel(states_.value(a.id)));if(a.id==selected)list_->setCurrentItem(item);};
    QSet<QString> saved;for(const auto &a:accounts_){append(a,false);saved.insert(a.id);}for(const auto &a:drafts_)if(!saved.contains(a.id))append(a,true);
}
void SettingsWidget::selectAccount(const QString &id) {
    if(id==editingId_)return;
    if(!editingId_.isEmpty()&&editor_->dirty())drafts_[editingId_]=editor_->account();
    editingId_=id;Account a;
    if(drafts_.contains(id))a=drafts_[id];else for(const auto &account:accounts_)if(account.id==id){a=account;break;}
    editor_->setAccount(a);updateAccountStatus();
    const QSignalBlocker blocker(list_);for(int n=0;n<list_->count();++n)if(list_->item(n)->data(Qt::UserRole).toString()==id)list_->setCurrentRow(n);
}
void SettingsWidget::updateAccountStatus() {
    const bool selected=!editingId_.isEmpty();editor_->setEnabled(selected);saveAccount_->setEnabled(selected&&canSave_);remove_->setEnabled(selected&&canSave_);
    const auto state=states_.value(editingId_);const auto label=registrationLabel(state);registrationText_->setText(selected?label:"Add an account to get started");registrationDot_->setStyleSheet("background:"+stateColor(state).name()+";border-radius:8px;");
    registrationDot_->setProperty("registered",state=="LinphoneRegistrationOk");
    const bool pending=state=="Registering"||state=="Unregistering"||state=="LinphoneRegistrationProgress"||state=="LinphoneRegistrationRefreshing";
    register_->setEnabled(selected&&canSave_&&!pending);unregister_->setEnabled(selected&&canSave_&&!pending&&state!="LinphoneRegistrationNone"&&state!="LinphoneRegistrationCleared"&&state!="Unregistered"&&state!="Disabled");
    register_->setStyleSheet(state=="LinphoneRegistrationOk"?"QPushButton {background:#138e81;color:white;border:0;}":QString());
    accountNetwork_->setText(selected?engine_.networkDescription(editingId_):QString());response_->setStyleSheet(state=="LinphoneRegistrationFailed"?"color:#b33748;":QString());response_->setPlainText(details_.value(editingId_));
    if(!canSave_)report("The settings file is invalid; it has been preserved. Repair it before saving.",true);
}
void SettingsWidget::addAccount() {
    if(!editingId_.isEmpty()&&editor_->dirty())drafts_[editingId_]=editor_->account();
    Account a;a.id=QUuid::createUuid().toString(QUuid::WithoutBraces);a.registerEnabled=false;drafts_[a.id]=a;
    editingId_.clear();rebuildList(a.id);selectAccount(a.id);
    editor_->findChild<QTabWidget*>("accountTabs")->setCurrentIndex(0);
}
void SettingsWidget::removeAccount() {
    if(editingId_.isEmpty())return;
    int index=-1;for(int n=0;n<accounts_.size();++n)if(accounts_[n].id==editingId_)index=n;
    if(index>=0) {
        if(!engine_.removeAccount(editingId_)){report(engine_.lastError(),true);return;}
        const auto old=accounts_.takeAt(index);QString error;
        if(!persist(&error)){accounts_.insert(index,old);engine_.addAccount(old);report(error,true);return;}
    }
    drafts_.remove(editingId_);states_.remove(editingId_);details_.remove(editingId_);editingId_.clear();rebuildList();
    if(list_->count())selectAccount(list_->item(0)->data(Qt::UserRole).toString());else{editor_->setAccount({});updateAccountStatus();}
    emit accountsChanged();
}
bool SettingsWidget::persist(QString *error) {
    if(!canSave_){if(error)*error="Original settings file is invalid and was not overwritten.";return false;}
    return store_.save(accounts_,prefs_,error);
}
void SettingsWidget::report(const QString &message,bool error) {
    response_->setStyleSheet(error?"color:#b33748;":QString());response_->setPlainText(QTime::currentTime().toString("HH:mm:ss")+"  "+message);
}
bool SettingsWidget::commitAccount(bool registerAfter) {
    auto a=editor_->account();int index=-1;for(int n=0;n<accounts_.size();++n)if(accounts_[n].id==a.id)index=n;
    if(registerAfter&&a.credentialsRequired&&a.password.isEmpty()){report("Enter the account password and press Register. The previous version did not save session passwords.",true);return false;}
    const Account old=index>=0?accounts_[index]:Account{};
    a.registerEnabled=index>=0?old.registerEnabled:false;
    if(registerAfter)a.registerEnabled=true;
    const auto validation=a.validate();if(!validation.isEmpty()){report(validation,true);return false;}
    const bool replace=index<0||editor_->dirty()||drafts_.contains(a.id);
    if(replace) {
        if(index>=0&&!engine_.removeAccount(a.id)){report(engine_.lastError(),true);return false;}
        if(!engine_.addAccount(a)){const auto error=engine_.lastError();if(index>=0)engine_.addAccount(old);report(error,true);return false;}
    } else if(registerAfter && !engine_.setRegistration(a.id,true)){report(engine_.lastError(),true);return false;}
    if(index<0)accounts_.append(a);else accounts_[index]=a;
    QString error;if(!persist(&error)) {
        engine_.removeAccount(a.id);if(index>=0){accounts_[index]=old;engine_.addAccount(old);}else accounts_.removeLast();report(error,true);return false;
    }
    drafts_.remove(a.id);editor_->setAccount(a);rebuildList();updateAccountStatus();emit accountsChanged();
    if(!registerAfter)report(a.registerEnabled?"Account saved.":"Account saved. Use Register to connect.");
    return true;
}
QWidget *SettingsWidget::audioPage() {
    auto *page=new QWidget;auto *form=new QFormLayout(page);form->setContentsMargins(20,20,20,20);form->setSpacing(14);
    auto *input=new QComboBox;input->setObjectName("microphoneDevice");auto *output=new QComboBox;output->setObjectName("speakerDevice");
    input->addItem("System default","");input->addItem("Dummy mic — silence",dummyMicSilence());input->addItem("Dummy mic — 440 Hz test tone",dummyMicTone());
    output->addItem("System default","");output->addItem("Dummy speaker — discard audio",dummySpeaker());
    for(const auto &id:engine_.soundDevices(true))if(!id.startsWith("Stel:"))input->addItem(id,id);
    for(const auto &id:engine_.soundDevices(false,true))if(!id.startsWith("Stel:"))output->addItem(id,id);
    input->setCurrentIndex(qMax(0,input->findData(prefs_.inputDevice)));output->setCurrentIndex(qMax(0,output->findData(prefs_.outputDevice)));
    form->addRow("Microphone",input);form->addRow("Speaker / ringer",output);form->addRow(note("Dummy mic runs inside Stel and needs no recording device. Choose silence or a continuous 440 Hz test tone. Receiving audio can still use your real speaker."));
    auto *button=new QPushButton("Apply audio settings");button->setObjectName("applyAudio");auto *result=note("");result->setObjectName("audioResult");form->addRow(button);form->addRow(result);
    connect(button,&QPushButton::clicked,this,[=]{auto next=prefs_;next.inputDevice=input->currentData().toString();next.outputDevice=output->currentData().toString();
        if(!engine_.applyDevices(next)){result->setText(engine_.lastError());return;}prefs_=next;QString error;result->setText(persist(&error)?"Audio settings applied.":error);});
    form->addRow(codecSettings(false));return page;
}
QWidget *SettingsWidget::videoPage() {
    auto *page=new QWidget;auto *form=new QFormLayout(page);form->setContentsMargins(20,20,20,20);form->setSpacing(14);
    auto *camera=new QComboBox;camera->setObjectName("cameraDevice");camera->addItem("System default","");camera->addItem("Dummy Camera — internal colour-bar chart",dummyCamera());
    for(const auto &id:engine_.videoDevices())if(!id.contains("Static",Qt::CaseInsensitive))camera->addItem(id,id);
    camera->setCurrentIndex(qMax(0,camera->findData(prefs_.videoDevice)));form->addRow("Camera",camera);form->addRow(note("Dummy Camera sends a built-in 640 × 480 test chart during video calls. No webcam or external image file is needed."));
    auto *button=new QPushButton("Apply video settings");button->setObjectName("applyVideo");auto *result=note("");result->setObjectName("videoResult");form->addRow(button);form->addRow(result);
    connect(button,&QPushButton::clicked,this,[=]{auto next=prefs_;next.videoDevice=camera->currentData().toString();if(!engine_.applyDevices(next)){result->setText(engine_.lastError());return;}prefs_=next;QString error;result->setText(persist(&error)?"Video settings applied.":error);});form->addRow(codecSettings(true));return page;
}
QWidget *SettingsWidget::codecSettings(bool video) {
    auto *widget=new QWidget;auto *layout=new QVBoxLayout(widget);layout->setContentsMargins(0,0,0,0);
    auto *selector=new CodecSelector(engine_.availableCodecs(video),video,video?prefs_.customVideoCodecs:prefs_.customAudioCodecs,video?prefs_.videoCodecs:prefs_.audioCodecs);layout->addWidget(selector);
    const QString prefix=video?"video":"audio";auto *apply=new QPushButton("Apply "+prefix+" codecs");apply->setObjectName(prefix+"ApplyCodecs");apply->setEnabled(canSave_);layout->addWidget(apply);
    auto *result=note("");result->setObjectName(prefix+"CodecResult");layout->addWidget(result);
    connect(apply,&QPushButton::clicked,this,[=]{
        const auto ids=selector->selectedIds();const bool custom=selector->custom();
        if(!engine_.applyCodecSelection(video,ids,custom)){result->setText(engine_.lastError());return;}
        if(video){prefs_.videoCodecs=custom?ids:QStringList();prefs_.customVideoCodecs=custom;}
        else {prefs_.audioCodecs=custom?ids:QStringList();prefs_.customAudioCodecs=custom;}
        QString error;result->setText(persist(&error)?"Saved. Next calls offer: "+engine_.codecs(video).join(", "):"Applied for this session; could not save: "+error);
    });return widget;
}
QWidget *SettingsWidget::networkPage() {
    auto *page=new QWidget;auto *layout=new QVBoxLayout(page);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *content=new QWidget;auto *form=new QFormLayout(content);form->setContentsMargins(18,18,18,18);form->setSpacing(12);scroll->setWidget(content);layout->addWidget(scroll);
    auto *signal=new QComboBox;signal->setObjectName("signallingAddress");auto *media=new QComboBox;media->setObjectName("mediaAddress");auto *refresh=new QPushButton("Refresh IP addresses");
    auto refreshIps=[=]{const auto oldSignal=signal->count()?signal->currentData().toString():prefs_.signallingAddress;const auto oldMedia=media->count()?media->currentData().toString():prefs_.mediaAddress;
        for(auto *combo:{signal,media}){const QSignalBlocker block(combo);const auto selected=combo==signal?oldSignal:oldMedia;combo->clear();combo->addItem("Automatic — OS routing","");for(const auto &local:localAddresses())combo->addItem(local.address+"  ·  "+local.interfaceName,local.address);if(!selected.isEmpty()&&combo->findData(selected)<0)combo->addItem(selected+" (unavailable)",selected);combo->setCurrentIndex(qMax(0,combo->findData(selected)));}};
    refreshIps();connect(refresh,&QPushButton::clicked,this,refreshIps);form->addRow("Signalling IP",signal);form->addRow("Media IP",media);form->addRow(refresh);
    auto port=[&](const QString &name,const QString &label,int value){auto *spin=new QSpinBox;spin->setObjectName(name);spin->setRange(-1,65535);spin->setSpecialValueText("Automatic");spin->setValue(value);form->addRow(label,spin);return spin;};
    auto *udp=port("sipUdpPort","Local SIP UDP base port",prefs_.sipUdpPort);auto *tcp=port("sipTcpPort","Local SIP TCP base port",prefs_.sipTcpPort);auto *tls=port("sipTlsPort","Local SIP TLS base port",prefs_.sipTlsPort);
    form->addRow(note("Automatic chooses a free local port; 0 disables that listener. Isolated mTLS accounts use base + 2, + 4, …; their actual ports are shown in Accounts. These are local ports, not the remote PBX port."));
    auto range=[&](const QString &key,const QString &label,int lo,int hi){auto *row=new QWidget;auto *h=new QHBoxLayout(row);h->setContentsMargins(0,0,0,0);auto *a=new QSpinBox;auto *b=new QSpinBox;for(auto *s:{a,b}){s->setRange(1024,65534);s->setSingleStep(2);}a->setObjectName(key+"Min");b->setObjectName(key+"Max");a->setValue(lo);b->setValue(hi);h->addWidget(a);h->addWidget(new QLabel("to"));h->addWidget(b);form->addRow(label,row);return qMakePair(a,b);};
    auto audio=range("audioPort","Audio RTP range",prefs_.audioPortMin,prefs_.audioPortMax);auto video=range("videoPort","Video RTP range",prefs_.videoPortMin,prefs_.videoPortMax);
    form->addRow(note("Use even RTP ports; RTCP uses the next port. A selected media IP binds RTP and its SDP address to that IP and disables automatic ICE/STUN discovery. Use Automatic media IP for WebRTC/TURN."));
    auto *apply=new QPushButton("Apply network settings");apply->setObjectName("applyNetwork");auto *result=note(engine_.networkDescription());result->setObjectName("networkResult");form->addRow(apply);form->addRow(result);
    connect(apply,&QPushButton::clicked,this,[=]{auto next=prefs_;next.signallingAddress=signal->currentData().toString();next.mediaAddress=media->currentData().toString();next.sipUdpPort=udp->value();next.sipTcpPort=tcp->value();next.sipTlsPort=tls->value();next.audioPortMin=audio.first->value();next.audioPortMax=audio.second->value();next.videoPortMin=video.first->value();next.videoPortMax=video.second->value();
        if(!engine_.applyNetwork(next)){result->setText(engine_.lastError());return;}prefs_=next;QString error;result->setText(persist(&error)?"Applied. "+engine_.networkDescription():error);updateAccountStatus();});return page;
}
QWidget *SettingsWidget::aboutPage() {
    auto *page=new QWidget;page->setObjectName("aboutPage");auto *layout=new QVBoxLayout(page);layout->setContentsMargins(20,20,20,20);layout->setSpacing(14);
    auto *heading=new QHBoxLayout;auto *logo=new QLabel;logo->setPixmap(applicationIcon().pixmap(64,64));heading->addWidget(logo);
    auto *version=new QLabel("Stel "+QString::fromLatin1(STEL_VERSION));version->setObjectName("aboutVersion");version->setStyleSheet("font-size:24px; font-weight:600;");heading->addWidget(version);heading->addStretch();layout->addLayout(heading);
    auto *details=new QTextBrowser;details->setObjectName("aboutDetails");details->setOpenExternalLinks(true);details->setFrameShape(QFrame::NoFrame);
    details->setHtml(QString::fromUtf8(R"HTML(
<p>A compact, open-source, multi-account SIP softphone for desktop computers.<br>
Voice and video calls, TLS, SRTP, call transfer, conferencing and developer-friendly test devices.</p>
<h3>Principal developer</h3>
<p><b>Mehrdad Seifzadeh</b><br>
<a href="mailto:mehrdad.seifzadeh@gmail.com">mehrdad.seifzadeh@gmail.com</a><br>
<a href="https://github.com/seifzadeh">github.com/seifzadeh</a></p>
<p><a href="https://github.com/seifzadeh/stel">Source code &amp; releases</a> &nbsp;·&nbsp;
<a href="https://seifzadeh.github.io/stel/">Stel website</a></p>
<h3>Free software</h3>
<p>Copyright © 2026 Mehrdad Seifzadeh. Stel's original code and artwork are licensed under
<b>GNU GPL v3 or later</b>. You may use, study, modify and redistribute them under that license.
Stel comes without warranty.</p>
<p>Third-party components retain their own licenses. The bundled Linphone SDK and the
Mediastreamer2 filter sources include AGPLv3 code; its terms also apply to the combined distribution.
Matching source code, build instructions and third-party notices accompany releases.</p>
<h3>Open-source components</h3>
<ul>
<li><a href="https://www.linphone.org/en/liblinphone-voip-sdk/">Linphone SDK</a> %1:
liblinphone, belle-sip, Mediastreamer2, oRTP, bctoolbox, belr and related components — upstream GPL/AGPL licenses.</li>
<li><a href="https://www.qt.io/">Qt</a> %2 — desktop interface and networking; LGPL/GPL, depending on module.</li>
<li><a href="https://github.com/frankosterfeld/qtkeychain">QtKeychain</a> — secure credential storage; BSD-3-Clause.</li>
<li>Codec and media backends (depending on the build): bcg729, Opus, Speex, libvpx,
OpenH264, libaom, dav1d, FFmpeg and libyuv. See the release's third-party notices for exact versions and licenses.</li>
<li>Crypto and compression backends (depending on the build): Mbed TLS, OpenSSL, libsrtp, bzrtp and zlib.</li>
</ul>
<p>Built with C++, CMake, Ninja and GCC/Clang or MSVC. Python, OpenSSL and Qt Test support
packaging and automated tests. GitHub Actions builds releases.</p>
<p><a href="https://github.com/seifzadeh/stel/blob/main/THIRD_PARTY.md">Third-party notices</a> &nbsp;·&nbsp;
<a href="https://github.com/seifzadeh/stel/blob/main/LICENSE">GPL license</a> &nbsp;·&nbsp;
<a href="https://github.com/seifzadeh/stel/issues">Report an issue</a></p>
)HTML").arg(QString::fromUtf8(linphone_core_get_version()).toHtmlEscaped(),QString::fromLatin1(qVersion()).toHtmlEscaped()));
    layout->addWidget(details,1);
    auto *licenseTabs=new QTabWidget;licenseTabs->setObjectName("aboutLicenses");licenseTabs->setMaximumHeight(160);
    for(const auto &license:QStringList{"GPL-3.0","AGPL-3.0"}) {
        auto *text=new QTextBrowser;text->setObjectName("license"+license);QFile file(":/licenses/"+license+".txt");
        if(file.open(QIODevice::ReadOnly))text->setPlainText(QString::fromUtf8(file.readAll()));
        licenseTabs->addTab(text,navigationIcon(NavigationIcon::Document),license);
    }
    styleNavigationTabs(licenseTabs,NavigationLevel::Compact);
    layout->addWidget(licenseTabs);return page;
}
QWidget *SettingsWidget::securityPage() {
    auto *page=new QWidget;auto *form=new QFormLayout(page);form->setContentsMargins(20,20,20,20);form->setSpacing(14);
    auto *ca=new QLineEdit(prefs_.caFile);ca->setObjectName("trustedCa");ca->setPlaceholderText("Blank = system / bundled trust store");auto *row=new QWidget;auto *h=new QHBoxLayout(row);h->setContentsMargins(0,0,0,0);h->addWidget(ca);auto *browse=new QPushButton("Browse…");h->addWidget(browse);form->addRow("Trusted root CA bundle",row);
    connect(browse,&QPushButton::clicked,this,[=]{auto path=QFileDialog::getOpenFileName(this,"Select trusted CA bundle");if(!path.isEmpty())ca->setText(path);});form->addRow(note("TLS certificate and hostname checks are always enabled. Set each account's client certificate and media encryption in Accounts → Security. Restart Stel after changing the trusted CA bundle."));
    auto *apply=new QPushButton("Save security settings");auto *result=note("");form->addRow(apply);form->addRow(result);connect(apply,&QPushButton::clicked,this,[=]{const auto path=ca->text().trimmed();if(!path.isEmpty()&&!QFile::exists(path)){result->setText("CA file does not exist.");return;}prefs_.caFile=path;QString error;result->setText(persist(&error)?"Saved. Restart Stel to apply the CA bundle.":error);});
    auto *open=new QPushButton("Open configuration folder");form->addRow(open);connect(open,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(store_.directory()));});return page;
}
}
