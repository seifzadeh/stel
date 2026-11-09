#include "sipengine.h"
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSet>
#include <QUuid>
#include <belr/grammarbuilder.h>
#include <mediastreamer2/msfactory.h>
#ifdef STEL_AV1_ADDON
extern MSFilterDesc ms_Av1Encoder_desc;
extern MSFilterDesc ms_Av1Decoder_desc;
#endif
#ifdef STEL_LEGACY_VIDEO_ADDON
extern "C" {
extern MSFilterDesc stel_h263_enc_desc, stel_h263_old_enc_desc, stel_mpeg4_enc_desc;
extern MSFilterDesc stel_h263_dec_desc, stel_h263_old_dec_desc, stel_mpeg4_dec_desc;
}
#endif

namespace stel {
static QString str(const char *s) { return QString::fromUtf8(s ? s : ""); }
static QString codecId(const LinphonePayloadType *pt) {
    return str(linphone_payload_type_get_mime_type(pt)).toLower()+"/"+
        QString::number(linphone_payload_type_get_clock_rate(pt))+"/"+
        QString::number(linphone_payload_type_get_channels(pt))+";"+str(linphone_payload_type_get_recv_fmtp(pt));
}
QString CodecInfo::label() const {
    QString name=mime;
    if(mime.compare("G729",Qt::CaseInsensitive)==0)name="G.729";
    else if(mime=="PCMU")name="G.711 μ-law (PCMU)";
    else if(mime=="PCMA")name="G.711 A-law (PCMA)";
    else if(mime=="G722")name="G.722 wideband";
    else if(mime=="H264")name="H.264 / AVC";
    else if(mime=="H265")name="H.265 / HEVC";
    QString detail=QString::number(clockRate/1000.0,'g',5)+" kHz";
    if(channels>0)detail+=channels==1?" · mono":channels==2?" · stereo":" · "+QString::number(channels)+" channels";
    return name+"  ·  "+detail+(fmtp.isEmpty()?QString():"  ·  "+fmtp);
}
static LinphoneMediaEncryption encryption(const QString &s) {
    if(s=="srtp")return LinphoneMediaEncryptionSRTP;
    if(s=="dtls")return LinphoneMediaEncryptionDTLS;
    if(s=="zrtp")return LinphoneMediaEncryptionZRTP;
    return LinphoneMediaEncryptionNone;
}
SipEngine::SipEngine(QObject *parent) : QObject(parent) {
    qRegisterMetaType<stel::CallInfo>();
    iterate_.setInterval(20);
    connect(&iterate_,&QTimer::timeout,this,[this]{ if(core_)linphone_core_iterate(core_); });
}
SipEngine::~SipEngine() { shutdown(); }
void SipEngine::shutdown() {
    stopping_=true; iterate_.stop();
    // Children must stop while the composite engine and its signal state live.
    const auto children=certificateCores_;certificateCores_.clear();for(auto *child:children)delete child;
    if(core_) {
        linphone_core_stop(core_);
        for(auto *c:calls_)linphone_call_unref(c);
        calls_.clear(); callIds_.clear();localPeerCalls_.clear();
        for(const auto &h:accounts_) {
            linphone_account_unref(h.account);
            if(h.auth)linphone_auth_info_unref(h.auth);
            if(h.turnAuth)linphone_auth_info_unref(h.turnAuth);
        }
        accounts_.clear(); linphone_core_unref(core_);core_=nullptr;
    }
}
void SipEngine::configureResources(const QString &prefix) {
    if(prefix.isEmpty())return;
    auto *f=linphone_factory_get();
    auto path=[&](const QString &s){return QDir(prefix).filePath(s).toUtf8();};
    linphone_factory_set_top_resources_dir(f,prefix.toUtf8().constData());
    linphone_factory_set_data_resources_dir(f,path("share/linphone").constData());
    linphone_factory_set_sound_resources_dir(f,path("share/sounds/linphone").constData());
    linphone_factory_set_ring_resources_dir(f,path("share/sounds/linphone/rings").constData());
    const auto images=QDir(prefix+"/share/mediastreamer2/images").exists()?"share/mediastreamer2/images":"share/linphone/images";
    linphone_factory_set_image_resources_dir(f,path(images).constData());
    linphone_factory_set_msplugins_dir(f,path("lib/mediastreamer/plugins").constData());
    linphone_factory_set_liblinphone_plugins_dir(f,path("lib/linphone/plugins").constData());
    belr::GrammarLoader::get().addPath(QDir(prefix).filePath("share/belr/grammars").toStdString());
}
bool SipEngine::fail(const QString &s) { error_=s; emit errorOccurred(s); return false; }
bool SipEngine::status(int value,const QString &operation) { return value==0 || fail(operation+" failed ("+QString::number(value)+")."); }
bool SipEngine::start(const Preferences &prefs,const QString &directory,bool testMode,int port) {
    if(core_)return fail("Engine is already running.");
    if(!prefs.validate().isEmpty())return fail(prefs.validate());
    for(const auto &ip:{prefs.signallingAddress,prefs.mediaAddress})if(!ip.isEmpty()) {
        bool found=false;for(const auto &local:localAddresses())found|=QHostAddress(local.address)==QHostAddress(ip);
        if(!found)return fail("Local IP is not available: "+ip);
    }
    preferences_=prefs;testMode_=testMode;
    dataDirectory_=directory; QDir().mkpath(directory+"/certificates"); QDir().mkpath(directory+"/recordings");
    QDir().mkpath(directory+"/cache");
    if(!prefs.caFile.isEmpty() && !QFile::exists(prefs.caFile))return fail("CA file does not exist.");
    auto *config=linphone_config_new(nullptr); // Memory only: never persist SIP or TURN secrets in linphonerc.
    linphone_config_set_int(config,"sip","store_auth_info",0);
    linphone_config_set_int(config,"sip","accounts_channel_isolation",1);
    // SDK 5.3's offer/answer retry can replace SRTP with RTP despite the
    // per-call mandatory flag. Keep the media type explicitly chosen by the
    // user, without changing the core-wide incoming negotiation policy.
    linphone_config_set_int(config,"sip","retry_invite_after_offeranswer_failure",0);
    if(!prefs.signallingAddress.isEmpty())linphone_config_set_string(config,"sip","bind_address",prefs.signallingAddress.toUtf8().constData());
    if(!prefs.mediaAddress.isEmpty())linphone_config_set_string(config,"rtp","bind_address",prefs.mediaAddress.toUtf8().constData());
    if(QHostAddress(prefs.signallingAddress).protocol()==QAbstractSocket::IPv4Protocol)linphone_config_set_int(config,"sip","use_ipv6",0);
    linphone_config_set_int(config,"misc","max_calls",8);
    linphone_config_set_int(config,"misc","history_max_size",0);
    linphone_config_set_string(config,"storage","uri","");
    linphone_config_set_string(config,"storage","call_logs_db_uri","");
    linphone_config_set_int(config,"video","automatically_accept",0);
    linphone_config_set_int(config,"video","automatically_initiate",0);
    // Include legacy formats when the SDK has their filters. Decoder-only
    // formats stay visible but cannot be offered for a two-way video call.
    int codecIndex=0;
    for(const auto &mime:{"H264","VP8","AV1","H265","VP9","theora","H263","H263-1998","MP4V-ES"}) {
        const auto section=QString("video_codec_%1").arg(codecIndex++).toUtf8();
        linphone_config_set_string(config,section.constData(),"mime",mime);
        linphone_config_set_int(config,section.constData(),"rate",90000);
        linphone_config_set_int(config,section.constData(),"enabled",QString(mime)=="H264"||QString(mime)=="VP8");
        if(QString(mime)=="H264")linphone_config_set_string(config,section.constData(),"recv_fmtp","profile-level-id=42801F");
    }
    if(testMode)linphone_config_set_int(config,"net","monitor",0);
    auto *factory=linphone_factory_get();
    linphone_factory_set_data_dir(factory,directory.toUtf8().constData());
    linphone_factory_set_cache_dir(factory,(directory+"/cache").toUtf8().constData());
    linphone_factory_set_config_dir(factory,directory.toUtf8().constData());
    core_=linphone_factory_create_core_with_config_3(factory,config,nullptr);
    linphone_config_unref(config);
    if(!core_)return fail("Could not create liblinphone core.");
    auto *msFactory=linphone_core_get_ms_factory(core_);
#ifdef STEL_AV1_ADDON
    if(!ms_factory_has_encoder(msFactory,"AV1"))ms_factory_register_filter(msFactory,&ms_Av1Encoder_desc);
    if(!ms_factory_has_decoder(msFactory,"AV1"))ms_factory_register_filter(msFactory,&ms_Av1Decoder_desc);
#endif
#ifdef STEL_LEGACY_VIDEO_ADDON
    ms_factory_register_filter(msFactory,&stel_h263_dec_desc);
    ms_factory_register_filter(msFactory,&stel_h263_old_dec_desc);
    ms_factory_register_filter(msFactory,&stel_mpeg4_dec_desc);
    if(!ms_factory_has_encoder(msFactory,"H263-1998"))ms_factory_register_filter(msFactory,&stel_h263_enc_desc);
    if(!ms_factory_has_encoder(msFactory,"H263"))ms_factory_register_filter(msFactory,&stel_h263_old_enc_desc);
    if(!ms_factory_has_encoder(msFactory,"MP4V-ES"))ms_factory_register_filter(msFactory,&stel_mpeg4_enc_desc);
#endif
    Q_UNUSED(msFactory);
    linphone_core_reload_ms_plugins(core_,nullptr);
    dummyAudio_=installDummyAudio(linphone_core_get_ms_factory(core_));
    linphone_core_reload_sound_devices(core_);
    linphone_core_set_user_data(core_,this);
    auto *cb=linphone_factory_create_core_cbs(linphone_factory_get());
    linphone_core_cbs_set_account_registration_state_changed(cb,registrationCb);
    linphone_core_cbs_set_call_state_changed(cb,callCb);
    linphone_core_cbs_set_transfer_state_changed(cb,transferCb);
    linphone_core_add_callbacks(core_,cb); linphone_core_cbs_unref(cb);
    linphone_core_set_user_agent(core_,"Stel",STEL_VERSION);
    linphone_core_verify_server_certificates(core_,TRUE);
    linphone_core_verify_server_cn(core_,TRUE);
    if(!prefs.caFile.isEmpty())linphone_core_set_root_ca(core_,prefs.caFile.toUtf8().constData());
    else if(QFile::exists(qEnvironmentVariable("STEL_CA_BUNDLE")))
        linphone_core_set_root_ca(core_,qEnvironmentVariable("STEL_CA_BUNDLE").toUtf8().constData());
    else {
        // Qt's platform trust roots also cover Windows, where /etc/ssl does not exist.
        QByteArray roots;
        for(const auto &cert:QSslConfiguration::defaultConfiguration().caCertificates())roots+=cert.toPem();
        if(!roots.isEmpty())linphone_core_set_root_ca_data(core_,roots.constData());
    }
    linphone_core_set_user_certificates_path(core_,(directory+"/certificates").toUtf8().constData());
    QString resources=qEnvironmentVariable("STEL_RESOURCE_PREFIX");
#ifndef Q_OS_WIN
    if(resources.isEmpty())resources="/usr";
#endif
    const auto picture=resources+"/share/mediastreamer2/images/nowebcamCIF.jpg";
    if(QFile::exists(picture))linphone_core_set_static_picture(core_,picture.toUtf8().constData());
    if(!testMode) {
        const auto ring=resources+"/share/sounds/linphone/rings/oldphone-mono.wav";
        const auto ringback=resources+"/share/sounds/linphone/ringback.wav";
        if(QFile::exists(ring))linphone_core_set_ring(core_,ring.toUtf8().constData());
        if(QFile::exists(ringback))linphone_core_set_ringback(core_,ringback.toUtf8().constData());
    }
    linphone_core_enable_video_capture(core_,TRUE);
    if(videoRendererInstaller_)videoRendererInstaller_(core_);
    linphone_core_enable_video_display(core_,!testMode);
    linphone_core_enable_video_preview(core_,FALSE);
    // Each call dialog supplies its own native surface before starting media.
    linphone_core_set_native_video_window_id(core_,LINPHONE_VIDEO_DISPLAY_NONE);
    linphone_core_set_native_preview_window_id(core_,LINPHONE_VIDEO_DISPLAY_NONE);
    auto *transports=linphone_factory_create_transports(linphone_factory_get());
    linphone_transports_set_udp_port(transports,port==-2?prefs.sipUdpPort:port);
    linphone_transports_set_tcp_port(transports,port==-2?prefs.sipTcpPort:port);
    linphone_transports_set_tls_port(transports,prefs.sipTlsPort);
    const int ret=linphone_core_set_transports(core_,transports);
    linphone_transports_unref(transports);
    if(ret!=0)return fail("Could not bind SIP transports.");
    linphone_core_set_audio_port_range(core_,prefs.audioPortMin,prefs.audioPortMax);
    linphone_core_set_video_port_range(core_,prefs.videoPortMin,prefs.videoPortMax);
    if(testMode) {
        linphone_core_set_use_files(core_,TRUE);
        const auto testAudio=qEnvironmentVariable("STEL_TEST_WAV_FILE");
        if(!testAudio.isEmpty())linphone_core_set_play_file(core_,testAudio.toUtf8().constData());
        linphone_core_set_ring(core_,nullptr); linphone_core_set_ringback(core_,nullptr);
        for(const auto &device:videoDevices())if(device.contains("Static",Qt::CaseInsensitive)) {
            linphone_core_set_video_device(core_,device.toUtf8().constData()); break;
        }
    }
    if(linphone_core_start(core_)!=0)return fail("Could not start liblinphone.");
    defaultInput_=str(linphone_core_get_capture_device(core_));defaultOutput_=str(linphone_core_get_playback_device(core_));defaultVideo_=str(linphone_core_get_video_device(core_));
    if(testMode)linphone_core_set_network_reachable(core_,TRUE);
    defaultAudioCodecs_=availableCodecs(false);defaultVideoCodecs_=availableCodecs(true);
    // A settings file can be moved to an SDK with different codecs. Keep the
    // application usable so Settings can display and repair unavailable choices.
    if(!configureCodecs(false,prefs.audioCodecs,prefs.customAudioCodecs))emit notification(error_);
    if(!configureCodecs(true,prefs.videoCodecs,prefs.customVideoCodecs))emit notification(error_);
    dnd_=prefs.dnd;if(!applyDevices(prefs))return false;iterate_.start();return true;
}
bool SipEngine::addAccount(const Account &a) {
    if(!core_)return fail("Engine is not running.");
    const QString validation=a.validate(); if(!validation.isEmpty())return fail(validation);
    if(!preferences_.mediaAddress.isEmpty() && (a.webrtc||a.turn))return fail("WebRTC/TURN requires Automatic media IP. Change Network settings first.");
    if(accounts_.contains(a.id)||certificateCores_.contains(a.id))return fail("Remove the existing account before replacing it.");
    for(const auto &h:accounts_)if(h.config.username==a.username && h.config.domain==a.domain)
        return fail("This SIP identity already exists. Edit its account instead of adding duplicate credentials.");
    for(const auto *child:certificateCores_)for(const auto &h:child->accounts_)
        if(h.config.username==a.username && h.config.domain==a.domain)return fail("This SIP identity already exists.");
    if(!certificateLeaf_ && !a.certificate.isEmpty()) {
        // liblinphone 5.3's TLS auth fallback can select the first certificate
        // in a core. One certificate identity per core prevents cross-account
        // credential reuse even when registrar/domain/realm are identical.
        auto *child=new SipEngine(this);child->certificateLeaf_=true;child->videoRendererInstaller_=videoRendererInstaller_;
        int slot=1;for(;;++slot){bool used=false;for(auto *e:certificateCores_)used|=e->isolatedSlot_==slot;if(!used)break;}
        child->isolatedSlot_=slot;
        auto childPrefs=preferences_;
        for(auto *p:{&childPrefs.sipUdpPort,&childPrefs.sipTcpPort,&childPrefs.sipTlsPort})if(*p>0)*p+=slot*2;
        connect(child,&SipEngine::accountState,this,&SipEngine::accountState);
        connect(child,&SipEngine::callChanged,this,&SipEngine::callChanged);
        connect(child,&SipEngine::callFinished,this,&SipEngine::callFinished);
        connect(child,&SipEngine::notification,this,&SipEngine::notification);
        connect(child,&SipEngine::errorOccurred,this,[this](const QString &s){fail(s);});
        if(!child->start(childPrefs,dataDirectory_+"/accounts/"+a.id,testMode_) || !child->addAccount(a)) {
            const auto message=child->lastError();delete child;return fail(message);
        }
        child->mute(muted_);
        if(remoteWindow_)child->setVideoWindows(remoteWindow_,previewWindow_);
        certificateCores_.insert(a.id,child);return true;
    }
    if(!linphone_core_media_encryption_supported(core_,encryption(a.encryption)))return fail("SDK does not support selected media encryption: "+a.encryption);
    auto *identity=linphone_factory_create_address(linphone_factory_get(),("sip:"+a.username+'@'+a.domain).toUtf8().constData());
    QString server=a.server.isEmpty()?"sip:"+a.domain:a.server;
    if(!server.startsWith("sip:")&&!server.startsWith("sips:"))server.prepend("sip:");
    auto *proxy=linphone_factory_create_address(linphone_factory_get(),server.toUtf8().constData());
    if(!identity || !proxy) {
        if(identity)linphone_address_unref(identity);
        if(proxy)linphone_address_unref(proxy);
        return fail("Invalid SIP identity or server URI.");
    }
    const auto transport=a.transport=="udp"?LinphoneTransportUdp:a.transport=="tcp"?LinphoneTransportTcp:LinphoneTransportTls;
    linphone_address_set_transport(proxy,transport);
    if(!a.callerIdName.isEmpty())linphone_address_set_display_name(identity,a.callerIdName.toUtf8().constData());
    auto *params=linphone_core_create_account_params(core_);
    int result=linphone_account_params_set_identity_address(params,identity);
    result|=linphone_account_params_set_server_address(params,proxy);
    // Route all dialog traffic through the selected registrar/outbound proxy as well.
    if(a.registerEnabled) {
        bctbx_list_t *routes=bctbx_list_append(nullptr,proxy);
        result|=linphone_account_params_set_routes_addresses(params,routes); bctbx_list_free(routes);
    }
    linphone_account_params_enable_register(params,a.enabled && a.registerEnabled);
    linphone_account_params_enable_outbound_proxy(params,a.registerEnabled);
    linphone_account_params_set_expires(params,a.expires);
    linphone_account_params_set_idkey(params,a.id.toUtf8().constData());
    linphone_account_params_enable_rtp_bundle(params,a.webrtc);
    auto *nat=linphone_core_create_nat_policy(core_);
    linphone_nat_policy_enable_ice(nat,a.ice && preferences_.mediaAddress.isEmpty());
    linphone_nat_policy_enable_stun(nat,!a.stunServer.isEmpty() && preferences_.mediaAddress.isEmpty());
    linphone_nat_policy_enable_turn(nat,a.turn);
    if(!a.stunServer.isEmpty())linphone_nat_policy_set_stun_server(nat,a.stunServer.toUtf8().constData());
    if(a.turn)linphone_nat_policy_set_stun_server_username(nat,a.turnUser.toUtf8().constData());
    linphone_account_params_set_nat_policy(params,nat); linphone_nat_policy_unref(nat);
    linphone_address_unref(identity); linphone_address_unref(proxy);
    if(result!=0) { linphone_account_params_unref(params); return fail("Invalid account routing parameters."); }
    auto *account=linphone_core_create_account(core_,params); linphone_account_params_unref(params);
    if(!account)return fail("Could not create SIP account.");
    auto *auth=linphone_auth_info_new(a.username.toUtf8().constData(),a.authUser.isEmpty()?nullptr:a.authUser.toUtf8().constData(),
        a.password.isEmpty()?nullptr:a.password.toUtf8().constData(),nullptr,nullptr,a.domain.toUtf8().constData());
    if(!a.certificate.isEmpty()) {
        linphone_auth_info_set_tls_cert_path(auth,a.certificate.toUtf8().constData());
        linphone_auth_info_set_tls_key_path(auth,a.privateKey.toUtf8().constData());
        if(!a.keyPassword.isEmpty())linphone_auth_info_set_tls_key_password(auth,a.keyPassword.toUtf8().constData());
    }
    if(!a.password.isEmpty() || !a.certificate.isEmpty())linphone_core_add_auth_info(core_,auth);
    LinphoneAuthInfo *turnAuth=nullptr;
    if(a.turn) {
        turnAuth=linphone_auth_info_new(a.turnUser.toUtf8().constData(),nullptr,a.turnPassword.toUtf8().constData(),nullptr,nullptr,nullptr);
        linphone_core_add_auth_info(core_,turnAuth);
    }
    accounts_.insert(a.id,{a,account,auth,turnAuth});
    if(linphone_core_add_account(core_,account)!=0) {
        removeAccount(a.id); return fail("Could not add SIP account.");
    }
    if(!a.registerEnabled) {
        // Without REGISTER, liblinphone has no registrar-generated contact yet.
        auto *contact=linphone_core_create_primary_contact_parsed(core_);
        if(contact) {
            linphone_address_set_username(contact,a.username.toUtf8().constData());
            linphone_address_set_transport(contact,transport);
            auto *used=linphone_core_get_transports_used(core_);
            const int localPort=transport==LinphoneTransportUdp?linphone_transports_get_udp_port(used):
                transport==LinphoneTransportTcp?linphone_transports_get_tcp_port(used):linphone_transports_get_tls_port(used);
            linphone_address_set_port(contact,localPort);
            if(a.domain=="127.0.0.1" || a.domain=="localhost")linphone_address_set_domain(contact,"127.0.0.1");
            linphone_account_set_contact_address(account,contact);
            linphone_transports_unref(used);linphone_address_unref(contact);
        }
    }
    emit accountState(a.id,a.enabled?(a.registerEnabled?"Registering":"Unregistered") : "Disabled",networkDescription());return true;
}
bool SipEngine::setRegistration(const QString &id,bool enabled) {
    if(certificateCores_.contains(id))return certificateCores_[id]->setRegistration(id,enabled);
    if(!accounts_.contains(id))return fail("Select an account first.");
    auto &h=accounts_[id];if(enabled && !h.config.enabled)return fail("Enable the account before registering.");
    if(!enabled && !h.config.registerEnabled){emit accountState(id,"Unregistered",networkDescription());return true;}
    const auto oldState=linphone_account_get_state(h.account);
    auto *params=linphone_account_params_clone(linphone_account_get_params(h.account));
    linphone_account_params_enable_register(params,enabled);
    linphone_account_params_enable_outbound_proxy(params,enabled);
    const int result=linphone_account_set_params(h.account,params);linphone_account_params_unref(params);
    if(result!=0)return fail("Could not update registration.");
    h.config.registerEnabled=enabled;
    if(!enabled && oldState!=LinphoneRegistrationOk && oldState!=LinphoneRegistrationProgress && oldState!=LinphoneRegistrationRefreshing)
        emit accountState(id,"Unregistered","Registration is off.");
    else emit accountState(id,enabled?"Registering":"Unregistering",enabled?"Sending REGISTER…":"Sending REGISTER with Expires: 0…");
    if(enabled)linphone_account_refresh_register(h.account);
    return true;
}
QString SipEngine::registrationState(const QString &id) const {
    if(certificateCores_.contains(id))return certificateCores_[id]->registrationState(id);
    if(!accounts_.contains(id))return "Unregistered";
    const auto &h=accounts_[id];
    if(!h.config.enabled)return "Disabled";
    return str(linphone_registration_state_to_string(linphone_account_get_state(h.account)));
}
QString SipEngine::networkDescription(const QString &id) const {
    if(certificateCores_.contains(id))return certificateCores_[id]->networkDescription();
    if(!core_)return {};
    auto *ports=linphone_core_get_transports_used(core_);
    const auto text=QString("SIP %1 • UDP %2 / TCP %3 / TLS %4 • Media %5")
        .arg(preferences_.signallingAddress.isEmpty()?"Automatic":preferences_.signallingAddress)
        .arg(linphone_transports_get_udp_port(ports)).arg(linphone_transports_get_tcp_port(ports)).arg(linphone_transports_get_tls_port(ports))
        .arg(preferences_.mediaAddress.isEmpty()?"Automatic":preferences_.mediaAddress);
    linphone_transports_unref(ports);return text;
}
bool SipEngine::removeAccount(const QString &id) {
    if(certificateCores_.contains(id)) {
        auto *child=certificateCores_[id];
        if(!child->calls().isEmpty())return fail("End this account's calls before changing it.");
        certificateCores_.remove(id);delete child;return true;
    }
    if(!accounts_.contains(id))return false;
    for(auto *c:calls_)if(accountId(c)==id)return fail("End this account's calls before changing it.");
    const auto h=accounts_.take(id);
    linphone_core_remove_account(core_,h.account);
    linphone_account_unref(h.account);
    if(h.auth){linphone_core_remove_auth_info(core_,h.auth);linphone_auth_info_unref(h.auth);}
    if(h.turnAuth){linphone_core_remove_auth_info(core_,h.turnAuth);linphone_auth_info_unref(h.turnAuth);}
    return true;
}
void SipEngine::configureCall(LinphoneCallParams *params,const QString &id,bool video) {
    const auto h=accounts_.value(id);
    if(h.account) {
        linphone_call_params_set_account(params,h.account);
        linphone_call_params_set_media_encryption(params,encryption(h.config.encryption));
        linphone_call_params_enable_mandatory_media_encryption(params,h.config.encryption!="none");
    }
    linphone_call_params_enable_video(params,video);
    const QString file=dataDirectory_+"/recordings/"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".wav";
    linphone_call_params_set_record_file(params,file.toUtf8().constData());
}
LinphoneAddress *SipEngine::targetAddress(const QString &s,const QString &id) const {
    const QString target=normalizeTarget(s,accounts_.value(id).config.domain);
    return target.isEmpty()?nullptr:linphone_factory_create_address(linphone_factory_get(),target.toUtf8().constData());
}
bool SipEngine::dial(const QString &id,const QString &target,bool video) {
    if(certificateCores_.contains(id))return certificateCores_[id]->dial(id,target,video);
    if(!accounts_.contains(id)||!accounts_[id].config.enabled)return fail("Select an enabled account first.");
    auto *address=targetAddress(target,id); if(!address)return fail("Invalid destination.");
    auto *params=linphone_core_create_call_params(core_,nullptr); configureCall(params,id,video);
    const bool ok=linphone_core_invite_address_with_params(core_,address,params)!=nullptr;
    linphone_call_params_unref(params); linphone_address_unref(address);
    return ok || fail("Call could not be started.");
}
LinphoneCall *SipEngine::call(const QString &id) const { return calls_.value(id,nullptr); }
SipEngine *SipEngine::owner(const QString &id) {
    if(calls_.contains(id))return this;
    for(auto *child:certificateCores_)if(child->calls_.contains(id))return child;
    return nullptr;
}
bool SipEngine::answer(const QString &id,bool video) {
    if(auto *e=owner(id);e && e!=this)return e->answer(id,video);
    auto *c=call(id); if(!c)return fail("Call no longer exists.");
    const auto policy=accounts_.value(accountId(c)).config.encryption;
    const auto *offered=linphone_call_get_remote_params(c);
    // SDES can be checked before answering; DTLS is established by its later
    // handshake and is enforced by the SDK's DTLS policy instead.
    if(offered&&policy=="srtp"&&linphone_call_params_get_media_encryption(offered)!=LinphoneMediaEncryptionSRTP) {
        const auto message="This account requires "+mediaSecurityLabel(policy)+", but the caller/PBX did not offer it. Select RTP in the account settings or enable matching media encryption on the PBX.";
        auto *error=linphone_error_info_new();linphone_error_info_set(error,"SIP",LinphoneReasonNotAcceptable,488,"Required media encryption was not offered",nullptr);
        linphone_call_decline_with_error_info(c,error);linphone_error_info_unref(error);return fail(message);
    }
    const auto callee=accountId(c);LinphoneCall *peer=nullptr;
    if(linphone_call_get_dir(c)==LinphoneCallIncoming && accounts_.contains(callee)) {
        const auto &identity=accounts_[callee].config;
        for(auto *other:calls_)if(other!=c && linphone_call_get_dir(other)==LinphoneCallOutgoing && accountId(other)!=callee) {
            const auto state=linphone_call_get_state(other);if(state==LinphoneCallEnd||state==LinphoneCallError||state==LinphoneCallReleased)continue;
            const auto *remote=linphone_call_get_remote_address(other);
            if(remote&&str(linphone_address_get_username(remote))==identity.username&&str(linphone_address_get_domain(remote)).compare(identity.domain,Qt::CaseInsensitive)==0){peer=other;break;}
        }
    }
    const auto oldMode=linphone_core_get_media_resource_mode(core_);const auto oldPeers=localPeerCalls_;
    if(peer) {
        // These are the two ends of one call through the PBX. Preempting the
        // outgoing leg in order to answer its incoming leg cancels that call.
        localPeerCalls_.insert(c);localPeerCalls_.insert(peer);
        linphone_core_set_media_resource_mode(core_,LinphoneSharedMediaResources);
    } else linphone_core_set_media_resource_mode(core_,LinphoneExclusiveMediaResources);
    auto *params=linphone_core_create_call_params(core_,c); configureCall(params,accountId(c),video);
    const int ret=linphone_call_accept_with_params(c,params); linphone_call_params_unref(params);
    if(ret!=0){localPeerCalls_=oldPeers;linphone_core_set_media_resource_mode(core_,oldMode);}
    return status(ret,"Answer");
}
bool SipEngine::hangup(const QString &id) {
    if(auto *e=owner(id);e && e!=this)return e->hangup(id);
    auto *c=call(id); if(!c)return fail("Call no longer exists.");
    return status(linphone_call_get_state(c)==LinphoneCallIncomingReceived?linphone_call_decline(c,LinphoneReasonDeclined):linphone_call_terminate(c),"Hangup");
}
bool SipEngine::hold(const QString &id,bool held) {
    if(auto *e=owner(id);e && e!=this)return e->hold(id,held);
    auto *c=call(id);if(!c)return fail("Call no longer exists.");return status(held?linphone_call_pause(c):linphone_call_resume(c),held?"Hold":"Resume");
}
bool SipEngine::transfer(const QString &id,const QString &target) {
    if(auto *e=owner(id);e && e!=this)return e->transfer(id,target);
    auto *c=call(id); if(!c)return fail("Call no longer exists.");
    auto *a=targetAddress(target,accountId(c)); if(!a)return fail("Invalid transfer destination.");
    const int ret=linphone_call_transfer_to(c,a); linphone_address_unref(a); return status(ret,"Transfer");
}
bool SipEngine::attendedTransfer(const QString &id,const QString &consultation) {
    auto *source=owner(id);auto *destination=owner(consultation);
    if(source && destination && source!=destination)return fail("Attended transfer across isolated TLS accounts requires PBX-side bridging. Use two calls on the same account.");
    if(source && source!=this)return source->attendedTransfer(id,consultation);
    auto *c=call(id); auto *dest=call(consultation);
    if(!c||!dest||c==dest)return fail("Select two different calls.");
    if(linphone_call_get_state(c)!=LinphoneCallPaused)return fail("Hold the original call before completing an attended transfer.");
    const auto state=linphone_call_get_state(dest);
    if(state!=LinphoneCallStreamsRunning && state!=LinphoneCallPaused)return fail("Wait for the consultation call to connect.");
    return status(linphone_call_transfer_to_another(c,dest),"Attended transfer");
}
bool SipEngine::redirect(const QString &id,const QString &target) {
    if(auto *e=owner(id);e && e!=this)return e->redirect(id,target);
    auto *c=call(id); if(!c)return fail("Call no longer exists.");
    auto *a=targetAddress(target,accountId(c)); if(!a)return fail("Invalid forwarding destination.");
    const int ret=linphone_call_redirect_to(c,a); linphone_address_unref(a); return status(ret,"Forward");
}
bool SipEngine::dtmf(const QString &id,char digit) {
    if(auto *e=owner(id);e && e!=this)return e->dtmf(id,digit);
    auto *c=call(id);if(!c)return fail("Call no longer exists.");return status(linphone_call_send_dtmf(c,digit),"DTMF");
}
bool SipEngine::record(const QString &id,bool start,const QString &path) {
    if(auto *e=owner(id);e && e!=this)return e->record(id,start,path);
    auto *c=call(id); if(!c)return fail("Call no longer exists.");
    const auto *params=linphone_call_get_params(c);
    if(auto *conference=linphone_call_get_conference(c)) {
        const auto file=path.isEmpty()?str(linphone_call_params_get_record_file(params)):path;
        const auto result=start?linphone_conference_start_recording(conference,file.toUtf8().constData()):linphone_conference_stop_recording(conference);
        if(!status(result,"Conference recording"))return false;
        if(start)emit notification("Recording: "+file);
        for(auto *member:calls_)if(linphone_call_get_conference(member)==conference)emit callChanged(info(member));
        return true;
    }
    if(start) {
        if(!path.isEmpty() && path!=str(linphone_call_params_get_record_file(params)))return fail("Recording path is assigned at call setup.");
        linphone_call_start_recording(c);
        if(!linphone_call_params_is_recording(linphone_call_get_params(c)))return fail("Recording could not be started.");
        emit notification("Recording: "+str(linphone_call_params_get_record_file(params)));
    } else linphone_call_stop_recording(c);
    emit callChanged(info(c)); return true;
}
bool SipEngine::muteCall(const QString &id,bool muted) {
    if(auto *e=owner(id);e&&e!=this)return e->muteCall(id,muted);
    auto *c=call(id);if(!c)return fail("Call no longer exists.");
    if(auto *conference=linphone_call_get_conference(c)) {
        linphone_conference_set_microphone_muted(conference,muted);
        if(bool(linphone_conference_get_microphone_muted(conference))!=muted)return fail("Wait for conference audio to connect before changing the microphone.");
        for(auto *member:calls_)if(linphone_call_get_conference(member)==conference)emit callChanged(info(member));
    } else {linphone_call_set_microphone_muted(c,muted);emit callChanged(info(c));}
    return true;
}
bool SipEngine::conference(const QString &id,const QString &other) {
    auto *first=owner(id),*second=owner(other);
    if(!first||!second||id==other)return fail("Choose two different connected calls.");
    if(first!=second)return fail("These accounts use isolated TLS cores. Use a PBX conference room to join them.");
    if(first!=this)return first->conference(id,other);
    auto *a=call(id),*b=call(other);
    for(auto *c:{a,b}){const auto state=linphone_call_get_state(c);if(state!=LinphoneCallStreamsRunning&&state!=LinphoneCallPaused&&state!=LinphoneCallPausedByRemote)return fail("Wait for both calls to connect before conferencing.");}
    auto *group=linphone_call_get_conference(a),*otherGroup=linphone_call_get_conference(b);
    if(group&&otherGroup&&group!=otherGroup)return fail("Joining two existing conferences is not available. Select a single call to add.");
    if(!group)group=otherGroup;
    if(!group) {
        auto *params=linphone_core_create_conference_params_2(core_,nullptr);
        linphone_conference_params_set_account(params,accounts_.value(accountId(a)).account);
        linphone_conference_params_enable_audio(params,TRUE);linphone_conference_params_enable_video(params,FALSE);linphone_conference_params_enable_local_participant(params,TRUE);
        group=linphone_core_create_conference_with_params(core_,params);linphone_conference_params_unref(params);
        if(!group)return fail("Could not create the audio conference.");
    }
    // The 5.3 bulk wrapper exposes a C++ bool as LinphoneStatus. The single
    // participant wrapper normalizes success to zero as documented.
    for(auto *c:{a,b})if(linphone_call_get_conference(c)!=group)
        if(!status(linphone_conference_add_participant(group,c),"Conference"))return false;
    return status(linphone_conference_enter(group),"Join conference");
}
void SipEngine::setCallVideoWindows(const QString &id,quintptr remote,quintptr preview) {
    if(auto *e=owner(id);e&&e!=this){e->setCallVideoWindows(id,remote,preview);return;}
    if(auto *c=call(id)) {
        const auto state=linphone_call_get_state(c);
        if(state==LinphoneCallIncomingReceived||state==LinphoneCallOutgoingInit||state==LinphoneCallOutgoingProgress||state==LinphoneCallOutgoingRinging) {
            // SDK 5.3 creates the renderer from the core's initial surface,
            // before its per-call video control interface becomes available.
            QHash<LinphoneCall*,void*> existing;
            for(auto *other:calls_)if(other!=c)existing.insert(other,linphone_call_get_native_video_window_id(other));
            linphone_core_set_native_video_window_id(core_,reinterpret_cast<void*>(remote));
            for(auto it=existing.cbegin();it!=existing.cend();++it)linphone_call_set_native_video_window_id(it.key(),it.value());
        }
        linphone_call_set_native_video_window_id(c,reinterpret_cast<void*>(remote));
        linphone_core_set_native_preview_window_id(core_,reinterpret_cast<void*>(preview));
        linphone_core_use_preview_window(core_,TRUE);
    }
}
bool SipEngine::mergeCalls() {
    SipEngine *selected=calls_.isEmpty()?nullptr:this;
    for(auto *child:certificateCores_)if(!child->calls_.isEmpty()) {
        if(selected)return fail("Merge requires calls in the same core/account; isolated TLS accounts need PBX-side conferencing.");
        selected=child;
    }
    if(selected && selected!=this)return selected->mergeCalls();
    return core_ && status(linphone_core_add_all_to_conference(core_),"Merge calls");
}
void SipEngine::mute(bool muted) {
    muted_=muted;
    if(core_)linphone_core_enable_mic(core_,!muted);
    for(auto *child:certificateCores_)child->mute(muted);
}
void SipEngine::setDnd(bool enabled) {
    dnd_=enabled;preferences_.dnd=enabled;
    for(auto *child:certificateCores_)child->setDnd(enabled);
}
void SipEngine::refreshRegistrations() {
    if(core_)linphone_core_refresh_registers(core_);
    for(auto *child:certificateCores_)child->refreshRegistrations();
}
bool SipEngine::configureNetwork(const Preferences &p) {
    if(!p.validate().isEmpty())return fail(p.validate());
    auto *config=linphone_core_get_config(core_);
    linphone_config_set_string(config,"sip","bind_address",p.signallingAddress.isEmpty()?nullptr:p.signallingAddress.toUtf8().constData());
    linphone_config_set_string(config,"rtp","bind_address",p.mediaAddress.isEmpty()?nullptr:p.mediaAddress.toUtf8().constData());
    const bool ipv6=QHostAddress(p.signallingAddress).protocol()==QAbstractSocket::IPv6Protocol || QHostAddress(p.mediaAddress).protocol()==QAbstractSocket::IPv6Protocol || (p.signallingAddress.isEmpty()&&p.mediaAddress.isEmpty());
    linphone_core_enable_ipv6(core_,ipv6);
    auto *ports=linphone_factory_create_transports(linphone_factory_get());
    linphone_transports_set_udp_port(ports,p.sipUdpPort);linphone_transports_set_tcp_port(ports,p.sipTcpPort);linphone_transports_set_tls_port(ports,p.sipTlsPort);
    const int result=linphone_core_set_transports(core_,ports);linphone_transports_unref(ports);
    auto *used=linphone_core_get_transports_used(core_);
    const bool bound=result==0 && (p.sipUdpPort<=0||p.sipUdpPort==linphone_transports_get_udp_port(used)) &&
        (p.sipTcpPort<=0||p.sipTcpPort==linphone_transports_get_tcp_port(used)) && (p.sipTlsPort<=0||p.sipTlsPort==linphone_transports_get_tls_port(used));
    linphone_transports_unref(used);
    if(!bound)return fail("Could not bind the requested SIP ports. Check whether another application uses them.");
    linphone_core_set_audio_port_range(core_,p.audioPortMin,p.audioPortMax);linphone_core_set_video_port_range(core_,p.videoPortMin,p.videoPortMax);
    for(auto it=accounts_.begin();it!=accounts_.end();++it) {
        auto *params=linphone_account_params_clone(linphone_account_get_params(it->account));
        auto *nat=linphone_core_create_nat_policy(core_);const auto &a=it->config;
        linphone_nat_policy_enable_ice(nat,a.ice&&p.mediaAddress.isEmpty());linphone_nat_policy_enable_stun(nat,!a.stunServer.isEmpty()&&p.mediaAddress.isEmpty());
        linphone_nat_policy_enable_turn(nat,a.turn);
        if(!a.stunServer.isEmpty())linphone_nat_policy_set_stun_server(nat,a.stunServer.toUtf8().constData());
        if(a.turn)linphone_nat_policy_set_stun_server_username(nat,a.turnUser.toUtf8().constData());
        linphone_account_params_set_nat_policy(params,nat);linphone_nat_policy_unref(nat);
        linphone_account_set_params(it->account,params);linphone_account_params_unref(params);
    }
    preferences_=p;return true;
}
bool SipEngine::applyNetwork(const Preferences &p) {
    if(!calls().isEmpty())return fail("End active calls before applying network settings.");
    if(!p.validate().isEmpty())return fail(p.validate());
    for(const auto &ip:{p.signallingAddress,p.mediaAddress})if(!ip.isEmpty()) {
        bool found=false;for(const auto &local:localAddresses())found|=QHostAddress(local.address)==QHostAddress(ip);
        if(!found)return fail("Local IP is not available: "+ip);
    }
    QList<QPair<SipEngine*,Preferences>> changes{{this,p}};
    for(auto *child:certificateCores_) {
        auto next=p;for(auto *port:{&next.sipUdpPort,&next.sipTcpPort,&next.sipTlsPort})if(*port>0)*port+=2*child->isolatedSlot_;
        if(!next.validate().isEmpty())return fail("Isolated mTLS ports exceed the usable range; choose lower SIP base ports.");
        changes.append({child,next});
    }
    for(const auto &change:changes)for(const auto &h:change.first->accounts_)
        if(!p.mediaAddress.isEmpty()&&(h.config.webrtc||h.config.turn))return fail("WebRTC/TURN accounts require Automatic media IP.");
    QList<QPair<SipEngine*,Preferences>> previous;
    for(const auto &change:changes)previous.append({change.first,change.first->preferences_});
    for(const auto &change:changes)if(!change.first->configureNetwork(change.second)) {
        const QString message=change.first->lastError();for(const auto &old:previous)old.first->configureNetwork(old.second);
        return fail(message);
    }
    for(const auto &change:changes)for(auto it=change.first->accounts_.cbegin();it!=change.first->accounts_.cend();++it)
        if(it->config.registerEnabled)emit accountState(it.key(),"Registering","Network changed; reconnecting…");
    emit notification("Network applied. "+networkDescription());return true;
}
bool SipEngine::applyDevices(const Preferences &p) {
    if(!core_)return fail("Engine is not running.");
    if(!calls().isEmpty())return fail("End active calls before changing audio/video devices.");
    QString input=p.inputDevice.isEmpty()?defaultInput_:p.inputDevice;
    QString output=p.outputDevice.isEmpty()?defaultOutput_:p.outputDevice;
    if(input==dummyMicTone())input=dummyAudio_.tone;else if(input==dummyMicSilence())input=dummyAudio_.silence;
    if(output==dummySpeaker())output=dummyAudio_.speaker;
    if((p.inputDevice==dummyMicTone()||p.inputDevice==dummyMicSilence()) && input.isEmpty())return fail("Dummy microphone was not registered by the media engine.");
    if(p.outputDevice==dummySpeaker() && output.isEmpty())return fail("Dummy speaker was not registered by the media engine.");
    if(p.inputDevice==dummyMicTone()||p.inputDevice==dummyMicSilence())linphone_core_set_use_files(core_,FALSE);
    if(!input.isEmpty()&&!status(linphone_core_set_capture_device(core_,input.toUtf8().constData()),"Microphone"))return false;
    if(!output.isEmpty()) {
        if(!status(linphone_core_set_playback_device(core_,output.toUtf8().constData()),"Speaker"))return false;
        linphone_core_set_ringer_device(core_,output.toUtf8().constData());
    }
    QString camera=p.videoDevice.isEmpty()?defaultVideo_:p.videoDevice;
    if(camera==dummyCamera()) {
        const auto path=dataDirectory_+"/dummy-camera.jpg";
        if(!writeDummyCameraImage(path))return fail("Could not create the internal camera test chart.");
        if(linphone_core_set_static_picture(core_,path.toUtf8().constData())!=0)return fail("Could not load the camera test chart.");
        camera.clear();for(const auto &device:videoDevices())if(device.contains("Static",Qt::CaseInsensitive)){camera=device;break;}
        if(camera.isEmpty())return fail("SDK was built without the static-image camera.");
    }
    if(!camera.isEmpty()&&!status(linphone_core_set_video_device(core_,camera.toUtf8().constData()),"Camera"))return false;
    preferences_.inputDevice=p.inputDevice;preferences_.outputDevice=p.outputDevice;preferences_.videoDevice=p.videoDevice;
    for(auto *child:certificateCores_)if(!child->applyDevices(p))return false;
    return true;
}
void SipEngine::setVideoWindows(quintptr remote,quintptr preview) {
    remoteWindow_=remote;previewWindow_=preview;
    if(!core_)return;
    linphone_core_set_native_video_window_id(core_,reinterpret_cast<void*>(remote));
    linphone_core_set_native_preview_window_id(core_,reinterpret_cast<void*>(preview));
    for(auto *child:certificateCores_)child->setVideoWindows(remote,preview);
}
QStringList SipEngine::soundDevices(bool captureOnly,bool playbackOnly) const {
    QStringList result; if(!core_)return result;
    auto *devices=linphone_core_get_extended_audio_devices(core_);
    for(auto *node=devices;node;node=node->next) {
        auto *device=static_cast<LinphoneAudioDevice*>(node->data);
        const bool suitable=(!captureOnly||linphone_audio_device_has_capability(device,LinphoneAudioDeviceCapabilityRecord))&&
            (!playbackOnly||linphone_audio_device_has_capability(device,LinphoneAudioDeviceCapabilityPlay));
        if(suitable)result<<str(linphone_audio_device_get_id(device));
        linphone_audio_device_unref(device);
    }
    bctbx_list_free(devices);return result;
}
QStringList SipEngine::videoDevices() const {
    QStringList result; if(!core_)return result;
    auto *devices=linphone_core_get_video_devices_list(core_);
    for(auto *node=devices;node;node=node->next){result<<str(static_cast<char*>(node->data));bctbx_free(node->data);}
    bctbx_list_free(devices);return result;
}
QStringList SipEngine::codecs(bool video) const {
    QStringList result;for(const auto &codec:availableCodecs(video))if(codec.enabled)result<<codec.mime+"/"+QString::number(codec.clockRate);return result;
}
QVector<CodecInfo> SipEngine::availableCodecs(bool video) const {
    QVector<CodecInfo> result;if(!core_)return result;
    auto *list=video?linphone_core_get_video_payload_types(core_):linphone_core_get_audio_payload_types(core_);
    for(auto *n=list;n;n=n->next) {
        auto *pt=static_cast<LinphonePayloadType*>(n->data);
        CodecInfo c;c.id=codecId(pt);c.mime=str(linphone_payload_type_get_mime_type(pt));
        c.clockRate=linphone_payload_type_get_clock_rate(pt);c.channels=linphone_payload_type_get_channels(pt);
        c.fmtp=str(linphone_payload_type_get_recv_fmtp(pt));c.encoder=str(linphone_payload_type_get_encoder_description(pt));
        c.enabled=linphone_payload_type_enabled(pt);c.defaultEnabled=c.enabled;c.defaultPriority=result.size();
        const auto &defaults=video?defaultVideoCodecs_:defaultAudioCodecs_;
        for(const auto &d:defaults)if(d.id==c.id){c.defaultEnabled=d.defaultEnabled;c.defaultPriority=d.defaultPriority;break;}
        auto *factory=linphone_core_get_ms_factory(core_);const auto mime=c.mime.toUtf8();
        c.available=ms_factory_has_encoder(factory,mime.constData())&&ms_factory_has_decoder(factory,mime.constData());
        result<<c;
        linphone_payload_type_unref(pt);
    }
    bctbx_list_free(list); return result;
}
bool SipEngine::configureCodecs(bool video,const QStringList &ids,bool custom) {
    const auto &defaults=video?defaultVideoCodecs_:defaultAudioCodecs_;
    QStringList enabled=ids,order=ids;
    if(!custom){enabled.clear();order.clear();for(const auto &c:defaults){order<<c.id;if(c.defaultEnabled&&c.available)enabled<<c.id;}}
    if(enabled.isEmpty())return fail(QString("Select at least one ")+(video?"video":"audio")+" codec.");
    const auto inventory=availableCodecs(video);QStringList known;
    for(const auto &c:inventory)if(c.available)known<<c.id;
    QSet<QString> unique;
    for(const auto &id:enabled){if(!known.contains(id))return fail("Codec unavailable in this build: "+id);if(unique.contains(id))return fail("Duplicate codec selection.");unique.insert(id);}
    auto *list=video?linphone_core_get_video_payload_types(core_):linphone_core_get_audio_payload_types(core_);
    QHash<QString,LinphonePayloadType*> payloads;QStringList remaining;
    for(auto *n=list;n;n=n->next){auto *pt=static_cast<LinphonePayloadType*>(n->data);const auto id=codecId(pt);payloads.insert(id,pt);remaining<<id;}
    bctbx_list_t *sorted=nullptr;
    for(const auto &id:order)if(payloads.contains(id)){sorted=bctbx_list_append(sorted,payloads[id]);remaining.removeAll(id);}
    for(const auto &id:remaining)sorted=bctbx_list_append(sorted,payloads[id]);
    for(auto *n=sorted;n;n=n->next){auto *pt=static_cast<LinphonePayloadType*>(n->data);linphone_payload_type_enable(pt,enabled.contains(codecId(pt)));}
    if(video){linphone_core_set_video_payload_types(core_,sorted);}
    else linphone_core_set_audio_payload_types(core_,sorted);
    bctbx_list_free(sorted);for(auto *pt:payloads)linphone_payload_type_unref(pt);bctbx_list_free(list);
    if(video){preferences_.customVideoCodecs=custom;preferences_.videoCodecs=custom?ids:QStringList();}
    else {preferences_.customAudioCodecs=custom;preferences_.audioCodecs=custom?ids:QStringList();}
    return true;
}
bool SipEngine::applyCodecSelection(bool video,const QStringList &ids,bool custom) {
    if(!core_)return fail("Engine is not running.");
    for(const auto &c:calls())if(c.rawState!=LinphoneCallEnd&&c.rawState!=LinphoneCallReleased)return fail("End active calls before changing codec preferences.");
    // All isolated certificate cores use this same factory/backend inventory.
    if(!configureCodecs(video,ids,custom))return false;
    for(auto *child:certificateCores_)if(!child->configureCodecs(video,ids,custom))return fail(child->lastError());
    error_.clear();emit notification(QString(video?"Video":"Audio")+" codec preferences applied.");
    return true;
}
QString SipEngine::accountId(LinphoneCall *c) const {
    // A PBX may deliver both registered identities to the same Contact socket.
    // The SDK then chooses its default account for a userless Request-URI.
    // The incoming To identity takes precedence over that fallback account.
    const auto *to=linphone_call_get_to_address(c);
    if(linphone_call_get_dir(c)==LinphoneCallIncoming && to) {
        for(auto it=accounts_.cbegin();it!=accounts_.cend();++it)
            if(str(linphone_address_get_username(to))==it->config.username && str(linphone_address_get_domain(to)).compare(it->config.domain,Qt::CaseInsensitive)==0)return it.key();
        QString matched;
        for(auto it=accounts_.cbegin();it!=accounts_.cend();++it)if(str(linphone_address_get_username(to))==it->config.username){if(!matched.isEmpty()){matched.clear();break;}matched=it.key();}
        if(!matched.isEmpty())return matched;
    }
    const auto *params=linphone_call_get_params(c);
    const auto *account=params?linphone_call_params_get_account(params):nullptr;
    for(auto it=accounts_.cbegin();it!=accounts_.cend();++it)if(it->account==account)return it.key();
    if(to)for(auto it=accounts_.cbegin();it!=accounts_.cend();++it)
        if(str(linphone_address_get_username(to))==it->config.username && str(linphone_address_get_domain(to))==it->config.domain)return it.key();
    // Direct-IP incoming calls can address the local interface rather than the
    // configured identity domain. Match a unique non-registering username.
    QString direct;
    if(to)for(auto it=accounts_.cbegin();it!=accounts_.cend();++it)
        if(!it->config.registerEnabled && str(linphone_address_get_username(to))==it->config.username) {
            if(!direct.isEmpty())return {};
            direct=it.key();
        }
    if(!direct.isEmpty())return direct;
    return {};
}
CallInfo SipEngine::info(LinphoneCall *c) const {
    CallInfo i; i.id=callIds_.value(c); i.accountId=accountId(c); i.rawState=linphone_call_get_state(c);
    const auto &account=accounts_.value(i.accountId).config;i.accountName=account.label.isEmpty()?account.username:account.label;
    i.state=str(linphone_call_state_to_string(i.rawState)); i.incoming=linphone_call_get_dir(c)==LinphoneCallIncoming;
    char *uri=linphone_address_as_string_uri_only(linphone_call_get_remote_address(c)); i.remote=str(uri); bctbx_free(uri);
    const auto *address=linphone_call_get_remote_address(c);i.remoteName=str(linphone_address_get_display_name(address));i.remoteUsername=str(linphone_address_get_username(address));
    i.duration=linphone_call_get_duration(c);
    const auto *error=linphone_call_get_error_info(c);i.sipStatus=error?linphone_error_info_get_protocol_code(error):0;
    if(i.sipStatus>=300)i.error="SIP "+QString::number(i.sipStatus)+" · "+str(linphone_error_info_get_phrase(error));
    if(auto *conference=linphone_call_get_conference(c)){i.inConference=true;i.conferenceParticipants=linphone_conference_get_participant_count(conference);i.muted=linphone_conference_get_microphone_muted(conference);i.recording=linphone_conference_is_recording(conference);}
    else i.muted=linphone_call_get_microphone_muted(c);
    const auto *params=linphone_call_get_current_params(c);
    if(params) {
        i.video=linphone_call_params_video_enabled(params);
        if(!i.inConference)i.recording=linphone_call_params_is_recording(linphone_call_get_params(c));
        i.encryption=str(linphone_media_encryption_to_string(linphone_call_params_get_media_encryption(params)));
        auto describe=[](const LinphonePayloadType *pt){return pt?str(linphone_payload_type_get_mime_type(pt))+"/"+QString::number(linphone_payload_type_get_clock_rate(pt)):QString();};
        i.audioCodec=describe(linphone_call_params_get_used_audio_payload_type(params));
        i.videoCodec=describe(linphone_call_params_get_used_video_payload_type(params));
    }
    if(i.rawState==LinphoneCallIncomingReceived) {
        const auto *remote=linphone_call_get_remote_params(c);
        if(remote)i.video=linphone_call_params_video_enabled(remote);
    }
    if(!i.incoming && (i.rawState==LinphoneCallOutgoingInit||i.rawState==LinphoneCallOutgoingProgress||i.rawState==LinphoneCallOutgoingRinging))i.video=linphone_call_params_video_enabled(linphone_call_get_params(c));
    if(i.rawState==LinphoneCallStreamsRunning) {
        auto *audio=linphone_call_get_audio_stats(c);
        if(audio){i.audioDownKbps=linphone_call_stats_get_download_bandwidth(audio);linphone_call_stats_unref(audio);}
        if(i.video){auto *video=linphone_call_get_video_stats(c);if(video){i.videoDownKbps=linphone_call_stats_get_download_bandwidth(video);linphone_call_stats_unref(video);}}
    }
    return i;
}
QList<CallInfo> SipEngine::calls() const {
    QList<CallInfo> list;for(auto *c:calls_)list<<info(c);
    for(const auto *child:certificateCores_)list.append(child->calls());
    return list;
}
void SipEngine::registrationCb(LinphoneCore *lc,LinphoneAccount *account,LinphoneRegistrationState state,const char *message) {
    auto *self=static_cast<SipEngine*>(linphone_core_get_user_data(lc));
    if(!self || self->stopping_)return;
    const QString id=str(linphone_account_params_get_idkey(linphone_account_get_params(account)));
    const auto *error=linphone_account_get_error_info(account);const int code=error?linphone_error_info_get_protocol_code(error):0;
    emit self->accountState(id,str(linphone_registration_state_to_string(state)),(code>0?"SIP "+QString::number(code)+" • ":QString())+str(message));
}
void SipEngine::callCb(LinphoneCore *lc,LinphoneCall *c,LinphoneCallState state,const char *message) {
    auto *self=static_cast<SipEngine*>(linphone_core_get_user_data(lc)); if(!self)return;
    if(!self->callIds_.contains(c)) {
        const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);
        self->callIds_.insert(c,id); self->calls_.insert(id,linphone_call_ref(c));
    }
    auto i=self->info(c); i.message=str(message);
    if(!self->stopping_)emit self->callChanged(i);
    // Post decisions outside the callback to avoid nested call-state notifications.
    if(state==LinphoneCallIncomingReceived && !self->stopping_)QTimer::singleShot(0,self,[self,id=i.id]{
        if(self->stopping_)return;
        auto *pending=self->call(id);if(!pending||linphone_call_get_state(pending)!=LinphoneCallIncomingReceived)return;
        const auto accountId=self->accountId(pending);const auto a=self->accounts_.value(accountId).config;
        bool busy=false;
        for(auto *other:self->calls_)if(other!=pending) {
            const auto s=linphone_call_get_state(other);
            busy|=s!=LinphoneCallEnd && s!=LinphoneCallError && s!=LinphoneCallReleased;
        }
        const bool reject=self->dnd_;
        if(!a.forwardTo.isEmpty() && (!a.forwardBusy||busy||reject))
            self->redirect(id,a.forwardTo);
        else if(reject)linphone_call_decline(pending,LinphoneReasonBusy);
        else if(self->accounts_.contains(accountId)&&a.enabled&&a.autoAnswer)self->answer(id,false);
    });
    if(state==LinphoneCallReleased) {
        if(!self->stopping_)emit self->callFinished(i);
        self->calls_.remove(i.id); self->callIds_.remove(c);
        if(self->localPeerCalls_.remove(c)&&self->localPeerCalls_.isEmpty())linphone_core_set_media_resource_mode(lc,LinphoneExclusiveMediaResources);
        linphone_call_unref(c);
    }
}
void SipEngine::transferCb(LinphoneCore *lc,LinphoneCall *call,LinphoneCallState state) {
    auto *self=static_cast<SipEngine*>(linphone_core_get_user_data(lc));
    if(self && !self->stopping_) {
        emit self->notification("Transfer: "+str(linphone_call_state_to_string(state)));
        if(state==LinphoneCallConnected || state==LinphoneCallStreamsRunning) {
            const auto id=self->callIds_.value(call);
            QTimer::singleShot(0,self,[self,id]{if(self->call(id))self->hangup(id);});
        }
    }
}
}
