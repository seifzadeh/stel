#pragma once
#include "sipengine.h"
#include <QDialog>
#include <QPointer>
class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;
class QGroupBox;
class QTabWidget;
class QTimer;
class QKeyEvent;
class QShowEvent;
class QCloseEvent;
class QMessageBox;
class QVBoxLayout;
class QHideEvent;
namespace stel {
class VideoView;
class InlineCallHost;
QString callStateLabel(LinphoneCallState);
bool callHasEnded(LinphoneCallState);
class CallDialog : public QDialog {
    Q_OBJECT
public:
    CallDialog(SipEngine &,const CallInfo &,QWidget *parent=nullptr);
    ~CallDialog() override;
    const CallInfo &callInfo() const {return info_;}
    void updateCall(const CallInfo &,const QString &accountDetails,const QString &transport);
    void refreshPeers(const QList<CallInfo> &);
    void finishCall(const CallInfo &);
    void reject() override;
    void setInlineHost(InlineCallHost *);
    void showInlineDetails();
    void sendDtmf(QChar digit){sendDigit(digit);}
    bool canSendDtmf() const;
    QString dtmfText() const;
    QString actionMessage() const;
    VideoView *remoteVideo() const {return remoteVideo_;}
    VideoView *previewVideo() const {return previewVideo_;}
    void enableDocking();
signals:
    void newCallRequested(const QString &accountId);
    void faxRequested(const QString &callId);
    void viewChanged();
    void dockRequested();
    void expandRequested();
    void actionMessageChanged(const QString &);
protected:
    void keyPressEvent(QKeyEvent *) override;
    void showEvent(QShowEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void hideEvent(QHideEvent *) override;
private:
    enum class RouteAction {Transfer,Forward};
    enum class PeerAction {Attended,Conference};
    SipEngine &engine_;
    CallInfo info_;
    FaxInfo faxInfo_;
    QList<CallInfo> peers_;
    QString accountDetails_,transport_;
    QLabel *direction_,*name_,*address_,*account_,*status_,*duration_,*message_,*mediaDetails_,*routeHint_,*peerHint_;
    QLineEdit *digits_,*routeTarget_;
    QPushButton *answer_,*answerVideo_,*ringTransfer_,*hangup_,*hold_,*mute_,*record_,*transfer_,*forward_,*attended_,*conference_,*addCall_,*routeApply_,*peerApply_;
    QPushButton *fax_;
    QList<QPushButton*> keypad_;
    QGroupBox *routePanel_,*peerPanel_;
    QComboBox *peerChoice_;
    QTabWidget *mediaTabs_;
    VideoView *remoteVideo_,*previewVideo_;
    QTimer *endTimer_;
    RouteAction routeAction_=RouteAction::Transfer;
    PeerAction peerAction_=PeerAction::Attended;
    bool endScheduled_=false,consultationPending_=false,videoBound_=false,videoOpened_=false;
    bool confirmingClose_=false;
    QMessageBox *closeQuestion_=nullptr;
    QPointer<InlineCallHost> inlineHost_;
    QVBoxLayout *bodyLayout_;
    QWidget *body_,*detailsPage_;
    QPushButton *dock_;
    QMetaObject::Connection inlineBack_,inlineWindow_;
    void closeInlinePanels();
    QString peerName() const;
    bool confirmClose();
    void sendDigit(QChar);
    void showError(const QString &);
    bool check(bool);
    void openRoute(RouteAction);
    void applyRoute();
    void openPeers(PeerAction);
    void applyPeer();
    void requestNewCall();
    void bindVideo();
};
}
