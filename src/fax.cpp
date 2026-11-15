#include "sipengine.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>
#include <algorithm>
#include <mediastreamer2/msfactory.h>
#ifdef STEL_HAS_T38
extern "C" int linphone_stel_fax_api_version();
extern "C" void linphone_stel_fax_refresh_devices(LinphoneCore *);
#endif
namespace stel {
bool SipEngine::t38Available(){
#ifdef STEL_HAS_T38
    return linphone_stel_fax_api_version()==1;
#else
    return false;
#endif
}
void SipEngine::loadFaxes(){
    QFile f(dataDirectory_+"/faxes/history.json");if(!f.open(QIODevice::ReadOnly))return;
    const auto document=QJsonDocument::fromJson(f.readAll());
    if(!document.isArray()){faxHistoryValid_=false;emit notification("Fax history is invalid and will not be overwritten. Repair or rename faxes/history.json before restarting.");return;}
    for(const auto &entry:document.array()){
        auto info=FaxInfo::fromJson(entry.toObject());if(info.id.isEmpty())continue;
        if(info.state=="sending"||info.state=="receiving"||info.state=="dialing"||info.state=="negotiating"){
            info.state="interrupted";info.detail="The application closed before the fax completed.";
        }
        faxes_.insert(info.id,FaxHandle{info,{},{},false,false});
    }
}
void SipEngine::persistFaxes(){
    if(!faxHistoryValid_)return;
    QJsonArray array;for(const auto &handle:faxes_)array.append(handle.info.toJson());
    QSaveFile file(dataDirectory_+"/faxes/history.json");
    if(!file.open(QIODevice::WriteOnly)){emit notification("Could not save fax history.");return;}
    file.setPermissions(QFile::ReadOwner|QFile::WriteOwner);const auto data=QJsonDocument(array).toJson();
    if(file.write(data)!=data.size()||!file.commit())emit notification("Could not save fax history.");
}
QList<FaxInfo> SipEngine::faxes() const {
    QList<FaxInfo> list;for(const auto &handle:faxes_)list<<handle.info;
    for(auto *child:certificateCores_)list.append(child->faxes());
    std::sort(list.begin(),list.end(),[](const FaxInfo &a,const FaxInfo &b){return a.timestamp>b.timestamp;});return list;
}
bool SipEngine::prepareFax(FaxHandle &h,bool incoming,const QString &protocol,const QString &document){
    if(protocol!="t38"&&protocol!="g711")return fail("Choose T.38 or G.711 pass-through.");
    if(!t38Available())return fail("This build needs the native fax SDK. See docs/FAX.md.");
    h.info.id=QUuid::createUuid().toString(QUuid::WithoutBraces);h.info.incoming=incoming;h.info.protocol=protocol;h.info.timestamp=QDateTime::currentDateTimeUtc();
    h.info.path=dataDirectory_+"/faxes/"+h.info.id+".tif";
    if(!incoming&&!prepareFaxDocument(document,h.info.path,&error_))return fail(error_);
    if(!incoming)QFile::setPermissions(h.info.path,QFile::ReadOwner|QFile::WriteOwner);
    h.media=std::make_shared<FaxMedia>(incoming,protocol,h.info.path);
    const QHostAddress bind=preferences_.mediaAddress.isEmpty()?QHostAddress(QHostAddress::AnyIPv4):QHostAddress(preferences_.mediaAddress);
    if(!h.media->start(bind)){QFile::remove(h.info.path);return fail(h.media->error());}
    {
        h.card=installFaxAudio(linphone_core_get_ms_factory(core_),h.media,h.info.id);
#ifdef STEL_HAS_T38
        linphone_stel_fax_refresh_devices(core_);
#endif
    }
    const auto id=h.info.id;
    connect(h.media.get(),&FaxMedia::progress,this,[this,id](int pages){auto &h=faxes_[id];h.info.pages=pages;emit faxChanged(h.info);});
    connect(h.media.get(),&FaxMedia::finished,this,[this,id](bool success,int pages,const QString &detail){
        if(!faxes_.contains(id))return;auto &h=faxes_[id];h.info.pages=pages;h.info.state=success?"completed":"failed";h.info.detail=detail;
        h.media->stop();persistFaxes();emit faxChanged(h.info);
        // The sending modem still needs to drain its final DCN/T.30 tail after
        // the receiving endpoint has confirmed the last page.
        const auto callId=h.info.callId;QTimer::singleShot(h.info.incoming?5000:1500,this,[this,callId]{if(call(callId))hangup(callId);});
    });
    return true;
}
void SipEngine::configureFax(LinphoneCallParams *params,const FaxHandle &h,bool t38){
    linphone_call_params_clear_custom_sdp_attributes(params);
    linphone_call_params_enable_video(params,FALSE);linphone_call_params_enable_realtime_text(params,FALSE);
    linphone_call_params_enable_avpf(params,FALSE);
    linphone_call_params_enable_audio(params,!t38);
    if(t38){
        linphone_call_params_set_media_encryption(params,LinphoneMediaEncryptionNone);
        linphone_call_params_enable_mandatory_media_encryption(params,FALSE);
        linphone_call_params_add_custom_sdp_attribute(params,"x-stel-fax-protocol","t38");
        linphone_call_params_add_custom_sdp_attribute(params,"x-stel-fax-port",QByteArray::number(h.media->port()).constData());
        if(!preferences_.mediaAddress.isEmpty())linphone_call_params_add_custom_sdp_attribute(params,"x-stel-fax-address",preferences_.mediaAddress.toUtf8().constData());
    }else if(!h.card.isEmpty()){
        linphone_call_params_add_custom_sdp_attribute(params,"x-stel-fax-protocol",h.info.protocol=="t38"?"t38-pending":"g711");
        auto *devices=linphone_core_get_extended_audio_devices(core_);
        for(auto *node=devices;node;node=node->next){auto *device=static_cast<LinphoneAudioDevice*>(node->data);if(QString::fromUtf8(linphone_audio_device_get_id(device))==h.card){linphone_call_params_set_input_audio_device(params,device);linphone_call_params_set_output_audio_device(params,device);}}
        bctbx_list_free_with_data(devices,reinterpret_cast<void(*)(void*)>(linphone_audio_device_unref));
    }
}
namespace {
class G711Offer {
    QList<QPair<LinphonePayloadType*,bool>> saved_;
public:
    explicit G711Offer(LinphoneCore *core){auto *list=linphone_core_get_audio_payload_types(core);for(const auto *n=list;n;n=n->next){auto *pt=static_cast<LinphonePayloadType*>(n->data);const auto mime=QByteArray(linphone_payload_type_get_mime_type(pt));saved_.append(qMakePair(pt,bool(linphone_payload_type_enabled(pt))));linphone_payload_type_enable(pt,mime=="PCMU"||mime=="PCMA");}bctbx_list_free(list);}
    ~G711Offer(){for(const auto &entry:saved_){linphone_payload_type_enable(entry.first,entry.second);linphone_payload_type_unref(entry.first);}}
};
}
void SipEngine::shareFaxResources(){if(!faxResourcesShared_){faxPreviousResources_=linphone_core_get_media_resource_mode(core_);faxResourcesShared_=true;}linphone_core_set_media_resource_mode(core_,LinphoneSharedMediaResources);}
void SipEngine::releaseFaxMedia(FaxHandle &h){
    if(h.media)h.media->stop();removeFaxAudio(linphone_core_get_ms_factory(core_),h.card);h.card.clear();h.media.reset();
#ifdef STEL_HAS_T38
    linphone_stel_fax_refresh_devices(core_);
#endif
}
bool SipEngine::sendFax(const QString &account,const QString &target,const QString &document,const QString &protocol){
    if(certificateCores_.contains(account))return certificateCores_[account]->sendFax(account,target,document,protocol);
    if(!core_||!accounts_.contains(account)||!accounts_[account].config.enabled)return fail("Choose an enabled account first.");
    if(protocol=="t38"&&accounts_[account].config.encryption!="none")return fail("T.38 uses unencrypted UDPTL. Use an RTP account for T.38, or select G.711 for an SRTP account.");
    auto *address=targetAddress(target,account);if(!address)return fail("Enter a valid fax destination.");
    FaxHandle h;h.info.accountId=account;char *remote=linphone_address_as_string_uri_only(address);h.info.remote=QString::fromUtf8(remote);bctbx_free(remote);
    if(!prepareFax(h,false,protocol,document)){linphone_address_unref(address);return false;}
    h.info.state="dialing";const auto id=h.info.id;faxes_.insert(id,h);pendingFax_=id;emit faxChanged(h.info);
    auto *params=linphone_core_create_call_params(core_,nullptr);configureCall(params,account,false);configureFax(params,h,false);
    linphone_core_set_use_files(core_,FALSE);
    shareFaxResources();
    LinphoneCall *c;{G711Offer offer(core_);c=linphone_core_invite_address_with_params(core_,address,params);}
    linphone_call_params_unref(params);linphone_address_unref(address);pendingFax_.clear();
    if(!c){auto &job=faxes_[id];job.info.state="failed";job.info.detail="The fax call could not be started.";releaseFaxMedia(job);persistFaxes();emit faxChanged(job.info);return fail(job.info.detail);}
    faxes_[id].info.callId=callIds_.value(c);linphone_call_enable_echo_cancellation(c,FALSE);persistFaxes();emit faxChanged(faxes_[id].info);return true;
}
bool SipEngine::receiveFax(const QString &id,const QString &protocol){
    if(auto *e=owner(id);e&&e!=this)return e->receiveFax(id,protocol);
    auto *c=call(id);if(!c)return fail("Call no longer exists.");
    const auto state=linphone_call_get_state(c);
    if(state!=LinphoneCallIncomingReceived&&state!=LinphoneCallStreamsRunning&&state!=LinphoneCallUpdatedByRemote)return fail("Fax can be received on a ringing or connected call.");
    for(const auto &h:faxes_)if(h.info.callId==id&&h.media)return fail("This call already has a fax session.");
    const auto account=accountId(c);if(protocol=="t38"&&accounts_.value(account).config.encryption!="none")return fail("T.38 uses unencrypted UDPTL. Select G.711 or use an RTP account.");
    const auto *remote=linphone_call_get_remote_params(c);const bool offered=remote&&linphone_call_params_get_custom_sdp_attribute(remote,"x-stel-remote-t38-port");
    if(accounts_.value(account).config.encryption=="srtp"&&remote&&linphone_call_params_get_media_encryption(remote)!=LinphoneMediaEncryptionSRTP)return fail("This account requires SRTP, but the caller did not offer it.");
    if(offered&&protocol!="t38")return fail("The caller offered T.38. Select T.38 to receive this fax.");
    FaxHandle h;h.info.callId=id;h.info.accountId=account;h.info.remote=info(c).remote;
    if(!prepareFax(h,true,protocol,{}))return false;
    h.info.state="negotiating";faxes_.insert(h.info.id,h);emit faxChanged(h.info);
    auto *params=linphone_core_create_call_params(core_,c);configureCall(params,account,false);configureFax(params,h,offered);
    linphone_core_set_use_files(core_,FALSE);linphone_call_enable_echo_cancellation(c,FALSE);
    shareFaxResources();
    if(!h.card.isEmpty()){
        auto *devices=linphone_core_get_extended_audio_devices(core_);
        for(auto *node=devices;node;node=node->next){auto *device=static_cast<LinphoneAudioDevice*>(node->data);if(QString::fromUtf8(linphone_audio_device_get_id(device))==h.card){linphone_call_set_input_audio_device(c,device);linphone_call_set_output_audio_device(c,device);}}
        bctbx_list_free_with_data(devices,reinterpret_cast<void(*)(void*)>(linphone_audio_device_unref));
    }
    int result;
    {G711Offer offer(core_);result=state==LinphoneCallIncomingReceived?linphone_call_accept_with_params(c,params):state==LinphoneCallUpdatedByRemote?linphone_call_accept_update(c,params):linphone_call_update(c,params);}
    linphone_call_params_unref(params);
    if(result){auto &job=faxes_[h.info.id];job.info.state="failed";job.info.detail="Could not answer the fax call.";releaseFaxMedia(job);emit faxChanged(job.info);persistFaxes();return fail(job.info.detail);}
    persistFaxes();return true;
}
void SipEngine::faxCallState(LinphoneCall *c,LinphoneCallState state){
    const auto id=callIds_.value(c);
    if(!pendingFax_.isEmpty()&&state==LinphoneCallOutgoingInit&&faxes_.contains(pendingFax_))faxes_[pendingFax_].info.callId=id;
    FaxHandle *h=nullptr;for(auto it=faxes_.begin();it!=faxes_.end();++it)if(it->info.callId==id&&it->media){h=&it.value();break;}
    if(!h&&state!=LinphoneCallUpdatedByRemote)return;
    const auto *remote=state==LinphoneCallOutgoingInit||state==LinphoneCallReleased?nullptr:linphone_call_get_remote_params(c);
    const char *remotePort=remote?linphone_call_params_get_custom_sdp_attribute(remote,"x-stel-remote-t38-port"):nullptr;
    if(state==LinphoneCallUpdatedByRemote){
        if(remotePort&&(!h||h->info.protocol=="t38")){linphone_call_defer_update(c);
            if(h&&h->info.protocol=="t38")QTimer::singleShot(0,this,[this,id]{auto *c=call(id);if(!c)return;for(const auto &h:faxes_)if(h.info.callId==id&&h.media){auto *params=linphone_core_create_call_params(core_,c);configureFax(params,h,true);linphone_call_accept_update(c,params);linphone_call_params_unref(params);break;}});
        }
    }
    if(!h)return;
    if(state==LinphoneCallStreamsRunning&&h->connected&&h->info.protocol=="t38"&&remotePort){
        const auto address=QString::fromUtf8(linphone_call_params_get_custom_sdp_attribute(remote,"x-stel-remote-t38-address"));
        h->media->connectPeer(QHostAddress(address),quint16(QByteArray(remotePort).toUInt()));
    }
    if(state==LinphoneCallStreamsRunning&&!h->connected){
        if(h->info.protocol=="t38"&&!remotePort){
            if(!h->requestedT38){h->requestedT38=true;QTimer::singleShot(h->info.incoming?500:2000,this,[this,id]{auto *c=call(id);if(!c)return;for(const auto &h:faxes_)if(h.info.callId==id&&h.media&&!h.connected){auto *params=linphone_core_create_call_params(core_,c);configureFax(params,h,true);linphone_call_update(c,params);linphone_call_params_unref(params);break;}});
                QTimer::singleShot(12000,this,[this,id]{for(auto &h:faxes_)if(h.info.callId==id&&h.media&&!h.connected){h.info.state="failed";h.info.detail="T.38 was not negotiated. Check PBX support or start a new fax using G.711.";h.media->stop();persistFaxes();emit faxChanged(h.info);if(call(id))hangup(id);break;}});
            }return;
        }
        if(h->info.protocol=="g711"){
            const auto *pt=linphone_call_params_get_used_audio_payload_type(linphone_call_get_current_params(c));const QByteArray mime=pt?linphone_payload_type_get_mime_type(pt):"";
            if(mime!="PCMU"&&mime!="PCMA"){h->info.state="failed";h->info.detail="Fax pass-through requires G.711 (PCMU or PCMA).";h->media->stop();emit faxChanged(h->info);hangup(id);return;}
            h->media->connectPeer(QHostAddress::LocalHost,9);
        }else{
            const auto address=QString::fromUtf8(linphone_call_params_get_custom_sdp_attribute(remote,"x-stel-remote-t38-address"));
            const auto port=QByteArray(remotePort).toUInt();
            if(!port||port>65535||!h->media->connectPeer(QHostAddress(address),quint16(port))){h->info.state="failed";h->info.detail="Invalid T.38 media address from the peer.";emit faxChanged(h->info);hangup(id);return;}
        }
        h->connected=true;h->info.state=h->info.incoming?"receiving":"sending";emit faxChanged(h->info);persistFaxes();
    }
    if(state==LinphoneCallEnd||state==LinphoneCallError||state==LinphoneCallReleased){
        if(h->info.state=="dialing"||h->info.state=="sending"||h->info.state=="receiving"||h->info.state=="negotiating"){
            h->info.state="failed";h->info.detail="The call ended before the fax was confirmed.";
            emit faxChanged(h->info);
        }
        h->media->stop();persistFaxes();
        bool active=false;for(const auto &job:faxes_)if(job.media){auto *other=call(job.info.callId);if(other&&linphone_call_get_state(other)!=LinphoneCallEnd&&linphone_call_get_state(other)!=LinphoneCallError&&linphone_call_get_state(other)!=LinphoneCallReleased)active=true;}
        if(!active&&faxResourcesShared_){faxResourcesShared_=false;if(localPeerCalls_.isEmpty())linphone_core_set_media_resource_mode(core_,faxPreviousResources_);}
        if(state==LinphoneCallReleased)releaseFaxMedia(*h);
    }
}
}
