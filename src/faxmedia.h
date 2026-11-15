#pragma once
#include <QObject>
#include <QDateTime>
#include <QJsonObject>
#include <QHostAddress>
#include <QMutex>
#include <QTimer>
#include <QUdpSocket>
#include <memory>
#include <cstdint>

struct fax_state_s;
struct t38_terminal_state_s;
struct t38_core_state_s;
struct t30_state_s;
struct _MSFactory;

namespace stel {
struct FaxInfo {
    QString id, callId, accountId, remote, path, protocol="t38", state, detail;
    QDateTime timestamp;
    int pages=0;
    bool incoming=false;
    QJsonObject toJson() const;
    static FaxInfo fromJson(const QJsonObject &);
};
// UDPTL's primary IFP followed by at most three redundant older IFPs. The
// bounded decoder rejects fragmented PER lengths and unsupported FEC.
QByteArray faxUdptlPacket(quint16 sequence,const QByteArray &,const QList<QByteArray> &older);
bool faxUdptlDecode(const QByteArray &,quint16 &,QList<QByteArray> &newestFirst);
bool prepareFaxDocument(const QString &source,const QString &destination,QString *error);

class FaxMedia : public QObject {
    Q_OBJECT
public:
    FaxMedia(bool incoming,QString protocol,QString path,QObject *parent=nullptr);
    ~FaxMedia() override;
    bool start(const QHostAddress &bind=QHostAddress::AnyIPv4);
    bool connectPeer(const QHostAddress &,quint16 port);
    void stop();
    void readAudio(int16_t *,int count);
    void writeAudio(const int16_t *,int count);
    quint16 port() const {return socket_.localPort();}
    QString error() const {return error_;}
    bool completed() const {return completed_;}
    QString protocol() const {return protocol_;}
signals:
    void progress(int pages);
    void finished(bool success,int pages,const QString &detail);
private:
    fax_state_s *audio_=nullptr;
    t38_terminal_state_s *terminal_=nullptr;
    t30_state_s *t30_=nullptr;
    QMutex mutex_;
    QUdpSocket socket_;
    QTimer timer_;
    QHostAddress peer_;
    quint16 peerPort_=0,sequence_=0,expected_=0;
    QList<QByteArray> older_;
    QString protocol_,path_,error_;
    bool incoming_,started_=false,done_=false,completed_=false,received_=false;
    int code_=0,pages_=0,lastPages_=-1,ticks_=0;
    quint64 audioSamples_=0;
    static int sendIfp(t38_core_state_s *,void *,const uint8_t *,int,int);
    static void phaseE(t30_state_s *,void *,int);
    void poll();
    void receive();
};
// A unique sound card per fax prevents concurrent accounts from sharing a
// modem. The factory owns the card; filter references keep the modem alive.
QString installFaxAudio(_MSFactory *,const std::shared_ptr<FaxMedia> &,const QString &id);
void removeFaxAudio(_MSFactory *,const QString &id);
}
Q_DECLARE_METATYPE(stel::FaxInfo)
