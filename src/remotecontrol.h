#pragma once
#include "sipengine.h"
#include <QTcpServer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QPointer>
#include <functional>
class QTcpSocket;
class QUrl;
namespace stel {
class ChatWidget;
struct FaxConversionControl;
struct ApiTlsConfig;
struct ApiTlsSession;
struct ApiReply {
    int status=200;
    QJsonObject data;
    QByteArray body,contentType="application/json; charset=utf-8";
    static ApiReply error(int,const QString &code,const QString &message);
};
class RemoteControlServer : public QTcpServer {
    Q_OBJECT
public:
    RemoteControlServer(SipEngine &,Store &,QVector<Account> &,Preferences &,ChatWidget &,QObject *parent=nullptr);
    ~RemoteControlServer() override;
    bool configure(const RemoteControlConfig &,QString *error=nullptr);
    void stop();
    QString endpoint() const;
    void setSelectionProviders(std::function<QString()> call,std::function<QString()> account);
    void setScreenshotProvider(std::function<QByteArray()> provider){screenshot_=std::move(provider);}
    void setConfigurationGuard(std::function<bool()> guard){canConfigure_=std::move(guard);}
    static QJsonObject callJson(const CallInfo &);
    static QJsonObject faxJson(const FaxInfo &);
    QJsonObject status() const;
signals:
    void resourceChanged(const QString &resource);
    void stateChanged();
protected:
    void incomingConnection(qintptr) override;
private:
    struct Client {QByteArray buffer;bool answered=false,authenticated=false;int headerSize=0; qint64 length=0;QTimer *timeout=nullptr;std::shared_ptr<ApiTlsSession> tls;};
    struct Document {QJsonObject info;QString output,source;std::shared_ptr<FaxConversionControl> control;};
    SipEngine &engine_;Store &store_;QVector<Account> &accounts_;Preferences &prefs_;ChatWidget &chat_;
    RemoteControlConfig config_;
    std::shared_ptr<ApiTlsConfig> tls_;
    QHash<QTcpSocket*,Client> clients_;
    QHash<QString,Document> documents_;
    struct Transfer {QString id,original,target,consultation,state,error;};
    QHash<QString,Transfer> transfers_;
    ApiReply startAttended(const QString &,const QString &);
    void advanceTransfers(const CallInfo &,bool finished=false);
    QJsonArray events_;
    quint64 eventId_=0;
    QElapsedTimer uptime_;
    bool dispatching_=false;
    std::function<QString()> selectedCall_,selectedAccount_;
    std::function<bool()> canConfigure_;
    std::function<QByteArray()> screenshot_;
    void read(QTcpSocket *);
    void send(QTcpSocket *,const ApiReply &);
    bool authenticate(const QByteArray &) const;
    ApiReply dispatch(const QByteArray &method,const QUrl &,const QJsonObject &);
    ApiReply yealink(const QUrl &);
    ApiReply callAction(const QString &,const QString &,const QJsonObject &);
    ApiReply dial(const QJsonObject &);
    ApiReply preferences(const QJsonObject &);
    ApiReply accountWrite(const QString &,const QJsonObject &,bool remove=false);
    ApiReply uploadDocument(const QJsonObject &);
    bool configurationAllowed(QString &error) const;
    QString accountId(const QString &) const;
    QString callId(const QString &requested={},bool incoming=false) const;
    void event(const QString &,const QJsonObject &);
    QJsonArray accountsJson() const;
    QJsonArray blfJson() const;
};
}
