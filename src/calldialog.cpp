#include "calldialog.h"
#include "branding.h"
#include "videoview.h"
#include <QApplication>
#include <QComboBox>
#include <QCloseEvent>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace stel {
bool callHasEnded(LinphoneCallState s){return s==LinphoneCallEnd||s==LinphoneCallError||s==LinphoneCallReleased;}
QString callStateLabel(LinphoneCallState s) {
    switch(s){
    case LinphoneCallIncomingReceived:return "Incoming call";
    case LinphoneCallOutgoingInit:return "Calling";
    case LinphoneCallOutgoingProgress:return "Connecting";
    case LinphoneCallOutgoingRinging:return "Ringing";
    case LinphoneCallOutgoingEarlyMedia:return "Early media";
    case LinphoneCallIncomingEarlyMedia:return "Incoming early media";
    case LinphoneCallConnected:case LinphoneCallStreamsRunning:return "Connected";
    case LinphoneCallPausing:return "Putting call on hold";
    case LinphoneCallPaused:return "On hold";
    case LinphoneCallPausedByRemote:return "Held by the other party";
    case LinphoneCallResuming:return "Resuming";
    case LinphoneCallRefered:return "Transferring";
    case LinphoneCallUpdating:case LinphoneCallUpdatedByRemote:return "Updating call";
    case LinphoneCallError:return "Call failed";
    case LinphoneCallEnd:case LinphoneCallReleased:return "Call ended";
    default:return "Preparing call";
    }
}
static QLabel *plainLabel(const QString &name={}) {
    auto *label=new QLabel;label->setObjectName(name);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);return label;
}
static QString mediaEncryption(QString value) {
    value.remove("LinphoneMediaEncryption");if(value.isEmpty())return "Negotiating";
    if(value=="None")return "RTP";
    if(value=="DTLS")return "DTLS-SRTP";
    return value;
}
CallDialog::CallDialog(SipEngine &engine,const CallInfo &info,QWidget *parent):QDialog(parent),engine_(engine),info_(info) {
    setObjectName("callDialog");setProperty("callId",info.id);setWindowIcon(applicationIcon());setModal(false);
    setAttribute(Qt::WA_QuitOnClose,false);setWindowFlag(Qt::WindowContextHelpButtonHint,false);setWindowFlag(Qt::WindowMinimizeButtonHint,true);
    const int available=QApplication::primaryScreen()?QApplication::primaryScreen()->availableGeometry().height()-60:740;
    setMinimumSize(480,qMin(580,qMax(480,available)));resize(550,qMin(740,qMax(480,available)));
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(12,12,12,12);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);outer->addWidget(scroll,1);
    auto *body=new QWidget;body->setObjectName("callBody");scroll->setWidget(body);auto *layout=new QVBoxLayout(body);layout->setContentsMargins(8,8,8,8);layout->setSpacing(12);
    auto *top=new QHBoxLayout;auto *logo=new QLabel;logo->setPixmap(applicationIcon().pixmap(28,28));top->addWidget(logo);
    direction_=plainLabel("callDirection");top->addWidget(direction_);top->addStretch();duration_=plainLabel("callDuration");duration_->setStyleSheet("font-size:20px;font-weight:600;color:#132c3b;");top->addWidget(duration_);layout->addLayout(top);
    auto *card=new QFrame;card->setObjectName("callIdentity");auto *identity=new QVBoxLayout(card);identity->setSpacing(5);
    name_=plainLabel("callerName");name_->setStyleSheet("font-size:24px;font-weight:700;color:#132c3b;");identity->addWidget(name_);
    address_=plainLabel("callerAddress");address_->setTextInteractionFlags(Qt::TextSelectableByMouse);address_->setStyleSheet("color:#607688;");identity->addWidget(address_);
    account_=plainLabel("callAccountDetails");identity->addWidget(account_);status_=plainLabel("callStatus");identity->addWidget(status_);layout->addWidget(card);
    auto *primary=new QHBoxLayout;
    auto button=[&](const QString &text,const QString &object){auto *b=new QPushButton(text);b->setObjectName(object);b->setAutoDefault(false);b->setMinimumHeight(34);return b;};
    answer_=button("Answer","callAnswer");answerVideo_=button("Answer with video","callAnswerVideo");hangup_=button("Hang up","callHangup");ringTransfer_=button("Transfer","callIncomingTransfer");
    for(auto *b:{answer_,answerVideo_,hangup_,ringTransfer_})primary->addWidget(b);
    layout->addLayout(primary);
    mediaTabs_=new QTabWidget;mediaTabs_->setObjectName("callMediaTabs");layout->addWidget(mediaTabs_);
    auto *keypad=new QWidget;auto *keyLayout=new QVBoxLayout(keypad);keyLayout->setContentsMargins(10,10,10,10);digits_=new QLineEdit;digits_->setObjectName("callDtmfDisplay");digits_->setReadOnly(true);digits_->setPlaceholderText("DTMF sent on this call");digits_->setMaxLength(40);keyLayout->addWidget(digits_);
    auto *grid=new QGridLayout;grid->setSpacing(6);const QString keys="123456789*0#";
    for(int n=0;n<keys.size();++n){const auto digit=keys[n];auto *b=button(QString(digit),"callDtmf_"+(digit=='*'?"star":digit=='#'?"hash":QString(digit)));b->setProperty("digit",QString(digit));b->setMinimumHeight(30);b->setMaximumHeight(36);b->setStyleSheet("font-size:21px;font-weight:500;padding:2px 8px;");grid->addWidget(b,n/3,n%3);keypad_<<b;connect(b,&QPushButton::clicked,this,[this,digit]{sendDigit(digit);});}
    keyLayout->addLayout(grid);mediaTabs_->addTab(keypad,"Keypad");
    auto *video=new QWidget;auto *videoLayout=new QVBoxLayout(video);remoteVideo_=new VideoView;remoteVideo_->setObjectName("callRemoteVideo");remoteVideo_->setMinimumSize(300,180);videoLayout->addWidget(remoteVideo_,1);
    previewVideo_=new VideoView;previewVideo_->setObjectName("callPreviewVideo");previewVideo_->setMinimumSize(120,80);previewVideo_->setMaximumSize(150,90);videoLayout->addWidget(previewVideo_,0,Qt::AlignRight);mediaTabs_->addTab(video,"Video");
    auto *details=new QWidget;auto *detailsLayout=new QVBoxLayout(details);mediaDetails_=plainLabel("callMediaDetails");mediaDetails_->setTextInteractionFlags(Qt::TextSelectableByMouse);detailsLayout->addWidget(mediaDetails_);detailsLayout->addStretch();mediaTabs_->addTab(details,"Details");
    auto *controls=new QGridLayout;controls->setSpacing(6);
    hold_=button("Hold","callHold");mute_=button("Mute mic","callMute");mute_->setCheckable(true);record_=button("Record","callRecord");record_->setCheckable(true);
    transfer_=button("Transfer","callTransfer");forward_=button("Forward","callForward");attended_=button("Attended transfer","callAttendedTransfer");conference_=button("Conference","callConference");addCall_=button("Add call","callAddCall");
    const QList<QPushButton*> actions{hold_,mute_,record_,transfer_,forward_,attended_,conference_,addCall_};for(int n=0;n<actions.size();++n)controls->addWidget(actions[n],n/3,n%3);layout->addLayout(controls);
    routePanel_=new QGroupBox;routePanel_->setObjectName("callRoutePanel");auto *route=new QVBoxLayout(routePanel_);routeHint_=plainLabel("callRouteHint");route->addWidget(routeHint_);routeTarget_=new QLineEdit;routeTarget_->setObjectName("callRouteTarget");routeTarget_->setPlaceholderText("Extension or SIP address");route->addWidget(routeTarget_);auto *routeButtons=new QHBoxLayout;routeApply_=button("Transfer","callRouteApply");auto *cancelRoute=button("Cancel","callRouteCancel");routeButtons->addWidget(routeApply_);routeButtons->addWidget(cancelRoute);route->addLayout(routeButtons);layout->addWidget(routePanel_);routePanel_->hide();
    peerPanel_=new QGroupBox;peerPanel_->setObjectName("callPeerPanel");auto *peer=new QVBoxLayout(peerPanel_);peerHint_=plainLabel("callPeerHint");peer->addWidget(peerHint_);peerChoice_=new QComboBox;peerChoice_->setObjectName("callPeerChoice");peer->addWidget(peerChoice_);auto *peerButtons=new QHBoxLayout;peerApply_=button("Apply","callPeerApply");auto *cancelPeer=button("Cancel","callPeerCancel");peerButtons->addWidget(peerApply_);peerButtons->addWidget(cancelPeer);peer->addLayout(peerButtons);layout->addWidget(peerPanel_);peerPanel_->hide();
    message_=plainLabel("callActionResult");message_->setStyleSheet("color:#b33748;");message_->hide();layout->addWidget(message_);layout->addStretch();
    auto *footer=new QHBoxLayout;auto *hint=plainLabel();hint->setText("Closing an active call asks before disconnecting.");hint->setStyleSheet("font-size:10px;color:#607688;");footer->addWidget(hint,1);auto *closeButton=button("Close window","callClose");footer->addWidget(closeButton);outer->addLayout(footer);
    endTimer_=new QTimer(this);endTimer_->setSingleShot(true);connect(endTimer_,&QTimer::timeout,this,[this]{close();});
    connect(closeButton,&QPushButton::clicked,this,&QDialog::close);
    connect(answer_,&QPushButton::clicked,this,[this]{check(engine_.answer(info_.id,false));});connect(answerVideo_,&QPushButton::clicked,this,[this]{check(engine_.answer(info_.id,true));});
    connect(hangup_,&QPushButton::clicked,this,[this]{check(engine_.hangup(info_.id));});
    connect(hold_,&QPushButton::clicked,this,[this]{check(engine_.hold(info_.id,info_.rawState!=LinphoneCallPaused));});
    connect(mute_,&QPushButton::clicked,this,[this](bool checked){if(!check(engine_.muteCall(info_.id,checked))){const QSignalBlocker block(mute_);mute_->setChecked(info_.muted);}});
    connect(record_,&QPushButton::clicked,this,[this](bool checked){if(!check(engine_.record(info_.id,checked))){const QSignalBlocker block(record_);record_->setChecked(info_.recording);}});
    connect(transfer_,&QPushButton::clicked,this,[this,scroll]{openRoute(RouteAction::Transfer);QTimer::singleShot(0,this,[this,scroll]{scroll->ensureWidgetVisible(routePanel_);});});
    connect(ringTransfer_,&QPushButton::clicked,this,[this,scroll]{openRoute(RouteAction::Transfer);QTimer::singleShot(0,this,[this,scroll]{scroll->ensureWidgetVisible(routePanel_);});});
    connect(forward_,&QPushButton::clicked,this,[this,scroll]{openRoute(RouteAction::Forward);QTimer::singleShot(0,this,[this,scroll]{scroll->ensureWidgetVisible(routePanel_);});});
    connect(routeApply_,&QPushButton::clicked,this,&CallDialog::applyRoute);connect(routeTarget_,&QLineEdit::returnPressed,this,&CallDialog::applyRoute);connect(cancelRoute,&QPushButton::clicked,routePanel_,&QWidget::hide);
    connect(attended_,&QPushButton::clicked,this,[this,scroll]{openPeers(PeerAction::Attended);QTimer::singleShot(0,this,[this,scroll]{scroll->ensureWidgetVisible(peerPanel_);});});
    connect(conference_,&QPushButton::clicked,this,[this,scroll]{openPeers(PeerAction::Conference);QTimer::singleShot(0,this,[this,scroll]{scroll->ensureWidgetVisible(peerPanel_);});});
    connect(peerApply_,&QPushButton::clicked,this,&CallDialog::applyPeer);connect(cancelPeer,&QPushButton::clicked,peerPanel_,&QWidget::hide);connect(addCall_,&QPushButton::clicked,this,&CallDialog::requestNewCall);
    connect(mediaTabs_,&QTabWidget::currentChanged,this,[this](int tab){if(tab==1)bindVideo();});
    setStyleSheet("QWidget#callBody{background:#f2f5f7;} QFrame#callIdentity{background:white;border:1px solid #d7e0e7;border-radius:8px;} QPushButton#callAnswer,QPushButton#callAnswerVideo{background:#138e81;color:white;border:0;} QPushButton#callHangup{background:#bd4052;color:white;border:0;} QPushButton:checked{background:#ddf2ee;border-color:#138e81;color:#134d48;} QLabel#callDirection{color:#607688;font-size:11px;font-weight:600;}");
    updateCall(info,info.accountName,{});
    if(info.incoming&&info.rawState==LinphoneCallIncomingReceived)mediaTabs_->setCurrentIndex(2);
}
void CallDialog::showError(const QString &text){message_->setText(text);message_->setVisible(!text.isEmpty());}
bool CallDialog::confirmClose(){
    if(callHasEnded(info_.rawState))return true;
    if(confirmingClose_)return false;
    const QScopedValueRollback<bool> guard(confirmingClose_,true);
    QMessageBox question(QMessageBox::Question,"End call?","End this call and close its window?",QMessageBox::Yes|QMessageBox::No,this);
    question.setObjectName("confirmCloseCall");question.setTextFormat(Qt::PlainText);
    question.setInformativeText((info_.remoteName.isEmpty()?info_.remote:info_.remoteName)+"\nAccount: "+accountDetails_);
    question.setDefaultButton(QMessageBox::No);question.setEscapeButton(QMessageBox::No);
    closeQuestion_=&question;const auto answer=question.exec();closeQuestion_=nullptr;
    if(callHasEnded(info_.rawState))return true;
    if(answer!=QMessageBox::Yes)return false;
    return check(engine_.hangup(info_.id))||callHasEnded(info_.rawState);
}
void CallDialog::reject(){if(confirmClose())QDialog::reject();}
void CallDialog::closeEvent(QCloseEvent *event){
    if(!confirmClose()){event->ignore();return;}
    event->accept();QDialog::reject();
}
bool CallDialog::check(bool success){if(success)showError({});else{showError(engine_.lastError().isEmpty()?"The action could not complete. The call may have ended.":engine_.lastError());if(callHasEnded(info_.rawState))endTimer_->start(6000);}return success;}
void CallDialog::sendDigit(QChar digit){if(keypad_.isEmpty()||!keypad_[0]->isEnabled())return;if(check(engine_.dtmf(info_.id,digit.toLatin1()))){if(digits_->text().size()>=40)digits_->clear();digits_->setText(digits_->text()+digit);}}
void CallDialog::keyPressEvent(QKeyEvent *event){const auto text=event->text();if(text.size()==1&&QString("0123456789*#").contains(text[0])&&!keypad_.isEmpty()&&keypad_[0]->isEnabled()){sendDigit(text[0]);event->accept();return;}QDialog::keyPressEvent(event);}
void CallDialog::showEvent(QShowEvent *event){QDialog::showEvent(event);if(info_.video)bindVideo();}
void CallDialog::bindVideo(){if(!info_.video||callHasEnded(info_.rawState))return;engine_.setCallVideoWindows(info_.id,remoteVideo_->surfaceId(),previewVideo_->surfaceId());videoBound_=true;}
void CallDialog::updateCall(const CallInfo &info,const QString &accountDetails,const QString &transport) {
    if(info.id!=info_.id)return;
    const bool justAnswered=info_.rawState==LinphoneCallIncomingReceived&&info.rawState!=LinphoneCallIncomingReceived&&!callHasEnded(info.rawState);
    info_=info;accountDetails_=accountDetails;transport_=transport;
    const auto state=info.rawState;const bool ended=callHasEnded(state),incoming=info.incoming&&state==LinphoneCallIncomingReceived;
    if(ended&&closeQuestion_)closeQuestion_->done(QMessageBox::No);
    const bool connected=state==LinphoneCallStreamsRunning,held=state==LinphoneCallPaused||state==LinphoneCallPausedByRemote,established=connected||held;
    const auto peer=info.remoteName.isEmpty()?(info.remoteUsername.isEmpty()?info.remote:info.remoteUsername):info.remoteName;
    setWindowTitle((incoming?"Incoming call":info.incoming?"Call from":"Call to")+QString(" · ")+peer+" · Stel");
    direction_->setText(info.incoming?"INCOMING CALL":"OUTGOING CALL");name_->setText(peer);address_->setText(info.remote);account_->setText("Account: "+accountDetails);
    duration_->setText(QString("%1:%2").arg(info.duration/60,2,10,QChar('0')).arg(info.duration%60,2,10,QChar('0')));
    status_->setText(!ended&&info.inConference?"Conference · "+QString::number(info.conferenceParticipants)+" participants":callStateLabel(state));status_->setStyleSheet("font-weight:600;color:"+QString(state==LinphoneCallError?"#b33748":connected?"#087f74":incoming?"#a16a16":"#526b7c")+";");
    answer_->setVisible(incoming);answerVideo_->setVisible(incoming&&info.video);hangup_->setText(incoming?"Reject":established?"Hang up":ended?"Call ended":"Cancel call");hangup_->setEnabled(!ended);
    ringTransfer_->setVisible(incoming);if(justAnswered)mediaTabs_->setCurrentIndex(0);
    hold_->setEnabled(established&&!info.inConference);hold_->setText(state==LinphoneCallPaused?"Resume":"Hold");
    mute_->setEnabled(established&&(!info.inConference||connected));record_->setEnabled(connected);{const QSignalBlocker block(mute_);mute_->setChecked(info.muted);}{const QSignalBlocker block(record_);record_->setChecked(info.recording);}record_->setText(info.recording?"Stop recording":"Record");
    transfer_->setEnabled(incoming||established);forward_->setEnabled(incoming);attended_->setEnabled(established&&!info.inConference);conference_->setEnabled(established);addCall_->setEnabled(established&&!info.inConference);
    for(auto *key:keypad_)key->setEnabled(connected||state==LinphoneCallOutgoingEarlyMedia||state==LinphoneCallIncomingEarlyMedia);
    mediaTabs_->setTabEnabled(1,info.video&&!ended);if(info.video&&!videoBound_)bindVideo();
    if(info.video&&connected&&!videoOpened_){videoOpened_=true;mediaTabs_->setCurrentIndex(1);}
    mediaDetails_->setText("Remote address: "+info.remote+"\nLocal account: "+accountDetails+"\nSignalling: "+(transport.isEmpty()?QString("—"):transport.toUpper())+"\nMedia security: "+mediaEncryption(info.encryption)+"\nAudio codec: "+(info.audioCodec.isEmpty()?QString("Negotiating"):info.audioCodec)+"\nVideo codec: "+(info.videoCodec.isEmpty()?QString("—"):info.videoCodec));
    if(routePanel_->isVisible()){const bool canRoute=routeAction_==RouteAction::Forward?incoming:(incoming||established);routeApply_->setEnabled(canRoute);routeHint_->setText(incoming?"Send this ringing call to another destination without answering.":"Transfer this connected call to another destination.");}
    refreshPeers(peers_);
    if(consultationPending_&&state==LinphoneCallPaused){consultationPending_=false;QTimer::singleShot(0,this,[this]{emit newCallRequested(info_.accountId);});}
    if(ended&&!endScheduled_){endScheduled_=true;consultationPending_=false;digits_->clear();routePanel_->hide();peerPanel_->hide();setAttribute(Qt::WA_DeleteOnClose,true);if(state==LinphoneCallError)showError(info.error.isEmpty()?info.message:info.error);endTimer_->start(state==LinphoneCallError?6000:2500);}
}
void CallDialog::finishCall(const CallInfo &info){auto final=info;if(final.rawState!=LinphoneCallError)final.rawState=LinphoneCallEnd;updateCall(final,accountDetails_,transport_);if(!isVisible())deleteLater();}
void CallDialog::openRoute(RouteAction action){routeAction_=action;peerPanel_->hide();routePanel_->setTitle(action==RouteAction::Forward?"Forward call":"Transfer call");routeApply_->setText(action==RouteAction::Forward?"Forward":"Transfer");routePanel_->show();updateCall(info_,accountDetails_,transport_);routeTarget_->setFocus();}
void CallDialog::applyRoute(){if(!routeApply_->isEnabled())return;const auto target=routeTarget_->text().trimmed();if(target.isEmpty()){showError("Enter a destination.");return;}const bool ringing=info_.rawState==LinphoneCallIncomingReceived;const bool result=ringing?engine_.redirect(info_.id,target):engine_.transfer(info_.id,target);if(check(result))routePanel_->hide();}
void CallDialog::openPeers(PeerAction action){peerAction_=action;routePanel_->hide();peerPanel_->setTitle(action==PeerAction::Attended?"Attended transfer":"Join conference");peerApply_->setText(action==PeerAction::Attended?"Complete transfer":"Join calls");peerPanel_->show();refreshPeers(peers_);}
void CallDialog::refreshPeers(const QList<CallInfo> &calls) {
    peers_=calls;if(!peerPanel_->isVisible())return;const auto selected=peerChoice_->currentData().toString();const QSignalBlocker block(peerChoice_);peerChoice_->clear();
    for(const auto &peer:peers_)if(peer.id!=info_.id&&!callHasEnded(peer.rawState)) {
        const bool eligible=peer.rawState==LinphoneCallStreamsRunning||peer.rawState==LinphoneCallPaused||(peerAction_==PeerAction::Conference&&peer.rawState==LinphoneCallPausedByRemote);
        if(eligible)peerChoice_->addItem((peer.remoteName.isEmpty()?peer.remote:peer.remoteName)+" · "+peer.accountName+" · "+callStateLabel(peer.rawState),peer.id);
    }
    peerChoice_->setCurrentIndex(qMax(0,peerChoice_->findData(selected)));
    const bool held=info_.rawState==LinphoneCallPaused;peerApply_->setEnabled(peerChoice_->count()>0&&!callHasEnded(info_.rawState)&&(peerAction_==PeerAction::Conference||held));
    peerHint_->setText(peerAction_==PeerAction::Conference?"Choose the call to join. Only this call and the selected call are added.":peerChoice_->count()==0?"Use Add call to dial a consultation, then select it here.":!held?"Put this call on Hold before completing the transfer.":"Choose the connected consultation call to complete the transfer.");
}
void CallDialog::applyPeer(){const auto other=peerChoice_->currentData().toString();if(other.isEmpty()||!peerApply_->isEnabled())return;const bool result=peerAction_==PeerAction::Attended?engine_.attendedTransfer(info_.id,other):engine_.conference(info_.id,other);if(check(result))peerPanel_->hide();}
void CallDialog::requestNewCall(){if(info_.rawState==LinphoneCallPaused){emit newCallRequested(info_.accountId);return;}consultationPending_=true;if(!check(engine_.hold(info_.id,true)))consultationPending_=false;}
}
