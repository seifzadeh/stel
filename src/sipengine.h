#pragma once
#include "model.h"
#include "dummydevices.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <linphone/linphonecore.h>

namespace stel {
struct CodecInfo {
    QString id, mime, fmtp, encoder;
    int clockRate = 0, channels = 0, defaultPriority = 0;
    bool enabled = false, defaultEnabled = false, available = false;
    QString label() const;
};
struct CallInfo {
    QString id, accountId, remote, state, message, encryption;
    QString remoteName, remoteUsername, accountName, error;
    QString audioCodec, videoCodec;
    LinphoneCallState rawState = LinphoneCallIdle;
    bool incoming = false, video = false, recording = false;
    bool muted = false, inConference = false;
    int sipStatus = 0, conferenceParticipants = 0;
    int duration = 0;
    float audioDownKbps = 0, videoDownKbps = 0;
};
class SipEngine : public QObject {
    Q_OBJECT
public:
    explicit SipEngine(QObject *parent = nullptr);
    ~SipEngine() override;
    void shutdown();
    bool start(const Preferences &, const QString &dataDirectory, bool testMode = false, int port = -2);
    void setVideoRendererInstaller(void (*installer)(LinphoneCore *)){videoRendererInstaller_=installer;}
    bool addAccount(const Account &);
    bool removeAccount(const QString &);
    bool setRegistration(const QString &id, bool enabled);
    QString registrationState(const QString &id) const;
    QString networkDescription(const QString &accountId = {}) const;
    bool applyNetwork(const Preferences &);
    bool dial(const QString &accountId, const QString &target, bool video = false);
    QString messageTarget(const QString &accountId,const QString &target) const;
    QString sendMessage(const QString &accountId,const QString &target,const QString &text);
    bool answer(const QString &, bool video);
    bool hangup(const QString &);
    bool hold(const QString &, bool held);
    bool transfer(const QString &, const QString &target);
    bool attendedTransfer(const QString &, const QString &consultation);
    bool redirect(const QString &, const QString &target);
    bool dtmf(const QString &, char);
    bool record(const QString &, bool start, const QString &path = {});
    bool mergeCalls();
    bool conference(const QString &, const QString &);
    bool muteCall(const QString &, bool);
    void setCallVideoWindows(const QString &, quintptr remote, quintptr preview);
    void mute(bool);
    void setDnd(bool enabled);
    void refreshRegistrations();
    bool applyDevices(const Preferences &);
    void setVideoWindows(quintptr remote, quintptr preview);
    QStringList soundDevices(bool captureOnly = false, bool playbackOnly = false) const;
    QStringList videoDevices() const;
    QStringList codecs(bool video) const;
    QVector<CodecInfo> availableCodecs(bool video) const;
    bool applyCodecSelection(bool video, const QStringList &ids, bool custom = true);
    QList<CallInfo> calls() const;
    QString lastError() const { return error_; }
    LinphoneCore *native() const { return core_; }
    static void configureResources(const QString &prefix);
signals:
    void accountState(const QString &id, const QString &state, const QString &detail);
    void callChanged(const stel::CallInfo &info);
    void callFinished(const stel::CallInfo &info);
    void errorOccurred(const QString &message);
    void notification(const QString &message);
    void messageChanged(const stel::MessageInfo &message);
private:
    struct AccountHandle { Account config; LinphoneAccount *account = nullptr; LinphoneAuthInfo *auth = nullptr; LinphoneAuthInfo *turnAuth = nullptr; };
    LinphoneCore *core_ = nullptr;
    void (*videoRendererInstaller_)(LinphoneCore *) = nullptr;
    QHash<QString,AccountHandle> accounts_;
    QHash<QString,LinphoneCall*> calls_;
    QHash<LinphoneCall*,QString> callIds_;
    QSet<LinphoneCall*> localPeerCalls_;
    QHash<QString,SipEngine*> certificateCores_;
    struct PendingMessage { MessageInfo info; LinphoneChatMessageCbs *callbacks=nullptr; };
    QHash<LinphoneChatMessage*,PendingMessage> messages_;
    bool certificateLeaf_ = false, testMode_ = false;
    bool muted_ = false;
    quintptr remoteWindow_ = 0, previewWindow_ = 0;
    Preferences preferences_;
    DummyAudioDevices dummyAudio_;
    QString defaultInput_, defaultOutput_, defaultVideo_;
    QVector<CodecInfo> defaultAudioCodecs_, defaultVideoCodecs_;
    int isolatedSlot_ = 0;
    QTimer iterate_;
    QString error_;
    QString dataDirectory_;
    bool dnd_ = false, stopping_ = false;
    bool fail(const QString &);
    bool status(int value, const QString &operation);
    bool configureNetwork(const Preferences &);
    bool configureCodecs(bool video, const QStringList &ids, bool custom);
    QString accountId(LinphoneCall *) const;
    CallInfo info(LinphoneCall *) const;
    LinphoneCall *call(const QString &) const;
    SipEngine *owner(const QString &id);
    LinphoneAddress *targetAddress(const QString &, const QString &accountId) const;
    void configureCall(LinphoneCallParams *, const QString &, bool);
    static void registrationCb(LinphoneCore *, LinphoneAccount *, LinphoneRegistrationState, const char *);
    static void callCb(LinphoneCore *, LinphoneCall *, LinphoneCallState, const char *);
    static void transferCb(LinphoneCore *, LinphoneCall *, LinphoneCallState);
    static void messageReceivedCb(LinphoneCore *,LinphoneChatRoom *,LinphoneChatMessage *);
    static void messageStateCb(LinphoneChatMessage *,LinphoneChatMessageState);
    void releaseMessage(LinphoneChatMessage *);
};
}
Q_DECLARE_METATYPE(stel::CallInfo)
Q_DECLARE_METATYPE(stel::MessageInfo)
