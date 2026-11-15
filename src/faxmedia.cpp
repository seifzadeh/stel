#include "faxmedia.h"
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonObject>
#include <QMutexLocker>
#include <QPainter>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <cmath>
extern "C" {
#include <spandsp.h>
#include <tiffio.h>
}
#include <mediastreamer2/msfactory.h>
#include <mediastreamer2/mssndcard.h>
#include <mediastreamer2/msticker.h>

namespace stel {
QJsonObject FaxInfo::toJson() const {return {{"id",id},{"callId",callId},{"accountId",accountId},{"remote",remote},{"path",path},{"protocol",protocol},{"state",state},{"detail",detail},{"timestamp",timestamp.toString(Qt::ISODateWithMs)},{"pages",pages},{"incoming",incoming}};}
FaxInfo FaxInfo::fromJson(const QJsonObject &o){FaxInfo f;f.id=o["id"].toString();f.callId=o["callId"].toString();f.accountId=o["accountId"].toString();f.remote=o["remote"].toString();f.path=o["path"].toString();f.protocol=o["protocol"].toString("t38");f.state=o["state"].toString();f.detail=o["detail"].toString();f.timestamp=QDateTime::fromString(o["timestamp"].toString(),Qt::ISODateWithMs);f.pages=o["pages"].toInt();f.incoming=o["incoming"].toBool();return f;}
namespace {
void length(QByteArray &b,int n){if(n<128)b.append(char(n));else{b.append(char(0x80|(n>>8)));b.append(char(n));}}
bool take(const QByteArray &b,int &offset,QByteArray &out){
    if(offset>=b.size())return false;int n=quint8(b[offset++]);
    if(n&0x80){if((n&0x40)||offset>=b.size())return false;n=((n&0x3f)<<8)|quint8(b[offset++]);}
    if(n<1||n>4096||n>b.size()-offset)return false;
    out=b.mid(offset,n);offset+=n;return true;
}
}
QByteArray faxUdptlPacket(quint16 seq,const QByteArray &ifp,const QList<QByteArray> &older){
    if(ifp.isEmpty()||ifp.size()>4096)return {};
    QByteArray b; b.append(char(seq>>8));b.append(char(seq));length(b,ifp.size());b+=ifp;
    b.append(char(0));const int count=qMin(3,int(older.size()));b.append(char(count));
    for(int i=0;i<count;++i){length(b,older[i].size());b+=older[i];}return b;
}
bool faxUdptlDecode(const QByteArray &b,quint16 &seq,QList<QByteArray> &packets){
    packets.clear();if(b.size()<5||b.size()>16384)return false;
    seq=(quint8(b[0])<<8)|quint8(b[1]);int offset=2;QByteArray primary;
    if(!take(b,offset,primary)||offset+2>b.size()||quint8(b[offset++])!=0)return false;
    const int count=quint8(b[offset++]);if(count>16)return false;packets<<primary;
    for(int i=0;i<count;++i){QByteArray secondary;if(!take(b,offset,secondary)){packets.clear();return false;}packets<<secondary;}
    if(offset!=b.size()){packets.clear();return false;}return true;
}
FaxMedia::FaxMedia(bool incoming,QString protocol,QString path,QObject *parent):QObject(parent),protocol_(std::move(protocol)),path_(std::move(path)),incoming_(incoming){
    timer_.setInterval(20);timer_.setTimerType(Qt::PreciseTimer);connect(&timer_,&QTimer::timeout,this,&FaxMedia::poll);connect(&socket_,&QUdpSocket::readyRead,this,&FaxMedia::receive);
}
FaxMedia::~FaxMedia(){stop();if(audio_)fax_free(audio_);if(terminal_)t38_terminal_free(terminal_);}
bool FaxMedia::start(const QHostAddress &bind){
    if(started_)return false;
    if(protocol_=="t38"){
        if(!socket_.bind(bind,0)){error_=socket_.errorString();return false;}
        terminal_=t38_terminal_init(nullptr,!incoming_,sendIfp,this);if(!terminal_){error_="Cannot create the T.38 terminal.";return false;}
        t30_=t38_terminal_get_t30_state(terminal_);auto *core=t38_terminal_get_t38_core_state(terminal_);t38_set_t38_version(core,0);t38_set_max_datagram_size(core,300);
    }else{
        audio_=fax_init(nullptr,!incoming_);if(!audio_){error_="Cannot create the fax modem.";return false;}
        fax_set_transmit_on_idle(audio_,true);t30_=fax_get_t30_state(audio_);
    }
    t30_set_tx_ident(t30_,"Stel");t30_set_ecm_capability(t30_,true);t30_set_supported_modems(t30_,T30_SUPPORT_V27TER|T30_SUPPORT_V29|T30_SUPPORT_V17);
    if(incoming_)t30_set_rx_file(t30_,path_.toUtf8().constData(),-1);else t30_set_tx_file(t30_,path_.toUtf8().constData(),-1,-1);
    t30_set_phase_e_handler(t30_,phaseE,this);started_=true;return true;
}
bool FaxMedia::connectPeer(const QHostAddress &address,quint16 port){
    if(!started_||completed_||address.isNull()||!port)return false;
    peer_=address;peerPort_=port;
    if(qEnvironmentVariableIsSet("STEL_FAX_TRACE")){
        qInfo()<<"Fax media"<<incoming_<<protocol_<<socket_.localAddress()<<socket_.localPort()<<"peer"<<peer_<<peerPort_;
        span_log_set_level(t30_get_logging_state(t30_),SPAN_LOG_FLOW|SPAN_LOG_SHOW_PROTOCOL);
        span_log_set_tag(t30_get_logging_state(t30_),incoming_?"RX":"TX");
    }
    timer_.start();return true;
}
void FaxMedia::stop(){timer_.stop();QMutexLocker lock(&mutex_);started_=false;socket_.close();}
void FaxMedia::readAudio(int16_t *samples,int count){QMutexLocker lock(&mutex_);std::fill(samples,samples+count,0);if(audio_&&started_&&!done_){const int n=fax_tx(audio_,samples,count);std::fill(samples+n,samples+count,0);}else if(terminal_&&started_&&!peerPort_){for(int n=0;n<count;++n,++audioSamples_){const bool tone=incoming_?audioSamples_<24000:audioSamples_%28000<4000;if(tone)samples[n]=int16_t(6000*std::sin(2*3.141592653589793*(incoming_?2100:1100)*double(audioSamples_)/8000));}}}
void FaxMedia::writeAudio(const int16_t *samples,int count){QMutexLocker lock(&mutex_);if(audio_&&started_&&!done_)fax_rx(audio_,const_cast<int16_t*>(samples),count);}
void FaxMedia::phaseE(t30_state_t *state,void *context,int code){auto *self=static_cast<FaxMedia*>(context);t30_stats_t stats{};t30_get_transfer_statistics(state,&stats);self->pages_=self->incoming_?stats.pages_rx:stats.pages_tx;self->done_=true;self->code_=code;}
int FaxMedia::sendIfp(t38_core_state_t *,void *context,const uint8_t *data,int len,int count){
    auto *self=static_cast<FaxMedia*>(context);if(self->peer_.isNull()||!self->peerPort_)return 0;
    const QByteArray ifp(reinterpret_cast<const char*>(data),len);auto redundancy=self->older_;
    auto packet=faxUdptlPacket(self->sequence_,ifp,redundancy);while(packet.size()>400&&!redundancy.isEmpty()){redundancy.removeLast();packet=faxUdptlPacket(self->sequence_,ifp,redundancy);}++self->sequence_;
    self->older_.prepend(ifp);while(self->older_.size()>3)self->older_.removeLast();
    for(int n=0;n<qBound(1,count,3);++n)self->socket_.writeDatagram(packet,self->peer_,self->peerPort_);return 0;
}
void FaxMedia::receive(){
    while(socket_.hasPendingDatagrams()){
        QByteArray packet;packet.resize(int(qMin<qint64>(socket_.pendingDatagramSize(),16385)));QHostAddress address;quint16 port=0;
        socket_.readDatagram(packet.data(),packet.size(),&address,&port);
        if(!terminal_||!started_||address!=peer_||port!=peerPort_)continue;
        quint16 sequence;QList<QByteArray> frames;if(!faxUdptlDecode(packet,sequence,frames))continue;
        QMutexLocker lock(&mutex_);auto *core=t38_terminal_get_t38_core_state(terminal_);
        for(int i=frames.size()-1;i>=0;--i){const quint16 n=quint16(sequence-i);if(received_&&qint16(n-expected_)<0)continue;t38_core_rx_ifp_packet(core,reinterpret_cast<const uint8_t*>(frames[i].constData()),frames[i].size(),n);received_=true;expected_=quint16(n+1);}
    }
}
void FaxMedia::poll(){
    int pages;bool finished;int code;
    {QMutexLocker lock(&mutex_);if(!started_||completed_)return;
        if(terminal_)t38_terminal_send_timeout(terminal_,160);
        t30_stats_t stats{};t30_get_transfer_statistics(t30_,&stats);pages_=incoming_?stats.pages_rx:stats.pages_tx;
        if(++ticks_>=45000&&!done_){done_=true;code_=T30_ERR_CALLDROPPED;}
        pages=pages_;finished=done_;code=code_;
    }
    if(pages!=lastPages_){lastPages_=pages;emit progress(pages);}
    if(finished){completed_=true;timer_.stop();const bool ok=code==T30_ERR_OK&&pages>0;emit this->finished(ok,pages,QString::fromUtf8(t30_completion_code_to_str(code)));}
}

namespace {
struct CardState{std::shared_ptr<FaxMedia> media;};
void cardDestroy(MSSndCard *card){delete static_cast<CardState*>(card->data);}
void filterInit(MSFilter *f){f->data=new CardState;}
void filterUninit(MSFilter *f){delete static_cast<CardState*>(f->data);}
int rate(MSFilter *,void *v){return *static_cast<int*>(v)==8000?0:-1;}
int getRate(MSFilter *,void *v){*static_cast<int*>(v)=8000;return 0;}
int channels(MSFilter *,void *v){return *static_cast<int*>(v)==1?0:-1;}
int getChannels(MSFilter *,void *v){*static_cast<int*>(v)=1;return 0;}
MSFilterMethod methods[]={{MS_FILTER_SET_SAMPLE_RATE,rate},{MS_FILTER_GET_SAMPLE_RATE,getRate},{MS_FILTER_SET_NCHANNELS,channels},{MS_FILTER_GET_NCHANNELS,getChannels},{0,nullptr}};
void produce(MSFilter *f){const int n=8000*f->ticker->interval/1000;auto *b=allocb(n*2,0);if(!b)return;static_cast<CardState*>(f->data)->media->readAudio(reinterpret_cast<int16_t*>(b->b_wptr),n);b->b_wptr+=n*2;ms_queue_put(f->outputs[0],b);}
void consume(MSFilter *f){while(auto *b=ms_queue_get(f->inputs[0])){msgpullup(b,size_t(-1));static_cast<CardState*>(f->data)->media->writeAudio(reinterpret_cast<const int16_t*>(b->b_rptr),int(b->b_wptr-b->b_rptr)/2);freemsg(b);}}
MSFilterDesc reader={MS_FILTER_PLUGIN_ID,"StelFaxModemTx","T.30 fax modem source",MS_FILTER_OTHER,nullptr,0,1,filterInit,nullptr,produce,nullptr,filterUninit,methods,0};
MSFilterDesc writer={MS_FILTER_PLUGIN_ID,"StelFaxModemRx","T.30 fax modem sink",MS_FILTER_OTHER,nullptr,1,0,filterInit,nullptr,consume,nullptr,filterUninit,methods,0};
MSFilter *makeFilter(MSSndCard *card,MSFilterDesc *desc){auto *f=ms_factory_create_filter_from_desc(card->sndcardmanager->factory,desc);if(f)*static_cast<CardState*>(f->data)=*static_cast<CardState*>(card->data);return f;}
MSFilter *readCard(MSSndCard *card){return makeFilter(card,&reader);}
MSFilter *writeCard(MSSndCard *card){return makeFilter(card,&writer);}
MSSndCardDesc descriptor{};
}
QString installFaxAudio(MSFactory *factory,const std::shared_ptr<FaxMedia> &media,const QString &id){
    static const bool configured=[] {descriptor.driver_type="StelFax";descriptor.uninit=cardDestroy;descriptor.create_reader=readCard;descriptor.create_writer=writeCard;return true;}();Q_UNUSED(configured);
    auto *manager=ms_factory_get_snd_card_manager(factory);auto *card=ms_snd_card_new(&descriptor);card->name=ms_strdup(("Fax "+id).toUtf8().constData());card->capabilities=MS_SND_CARD_CAP_CAPTURE|MS_SND_CARD_CAP_PLAYBACK;card->preferred_sample_rate=8000;card->data=new CardState{media};ms_snd_card_manager_add_card(manager,card);return QString::fromUtf8(ms_snd_card_get_string_id(card));
}
void removeFaxAudio(MSFactory *factory,const QString &id){
    auto *manager=ms_factory_get_snd_card_manager(factory);
    for(auto *node=ms_snd_card_manager_get_list(manager);node;node=node->next){auto *card=static_cast<MSSndCard*>(node->data);if(card->desc==&descriptor&&QString::fromUtf8(ms_snd_card_get_string_id(card))==id){ms_snd_card_manager_remove_card(manager,card);return;}}
}
}
