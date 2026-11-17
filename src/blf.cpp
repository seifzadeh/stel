#include "blf.h"
#include "sipengine.h"
#include <QDateTime>
#include <QXmlStreamReader>

namespace stel {
QString blfStateLabel(BlfState state) {
    switch(state) {
    case BlfState::Idle:return "Available";
    case BlfState::Busy:return "Busy";
    case BlfState::Ringing:return "Ringing";
    case BlfState::Subscribing:return "Waiting for PBX";
    case BlfState::Error:return "BLF unavailable";
    default:return "Unknown";
    }
}
bool DialogInfo::update(const QByteArray &body,QString *error) {
    const auto fail=[&](const QString &text){if(error)*error=text;return false;};
    if(body.isEmpty()||body.size()>262144||body.contains("<!DOCTYPE"))return fail("Invalid dialog-info document.");
    QXmlStreamReader xml(body);
    if(!xml.readNextStartElement()||xml.name()!=QLatin1String("dialog-info")||
       xml.namespaceUri()!=QLatin1String("urn:ietf:params:xml:ns:dialog-info"))return fail("Expected RFC 4235 dialog-info.");
    const auto attributes=xml.attributes();bool number=false;
    const auto version=attributes.value("version").toLongLong(&number);
    const auto mode=attributes.value("state").toString();const auto entity=attributes.value("entity").toString();
    if(!number||version<0||(mode!="full"&&mode!="partial")||entity.isEmpty())return fail("Incomplete dialog-info document.");
    QMap<QString,QString> changes;
    while(xml.readNextStartElement()) {
        if(xml.name()!=QLatin1String("dialog")){xml.skipCurrentElement();continue;}
        const auto id=xml.attributes().value("id").toString();QString state;
        while(xml.readNextStartElement()) {
            if(xml.name()==QLatin1String("state"))state=xml.readElementText().trimmed().toLower();
            else xml.skipCurrentElement();
        }
        if(id.isEmpty()||!QStringList{"trying","proceeding","early","confirmed","terminated"}.contains(state))return fail("Invalid dialog state.");
        changes[id]=state;
    }
    while(!xml.atEnd())xml.readNext();
    if(xml.hasError())return fail("Malformed dialog-info XML.");
    if(version_>=0&&version<=version_)return true; // retransmitted/stale NOTIFY
    if(mode=="partial"&&(version_<0||version!=version_+1||entity!=entity_))return fail("Missing BLF state; requesting a full update.");
    if(mode=="full")dialogs_.clear();
    for(auto it=changes.cbegin();it!=changes.cend();++it) {
        if(it.value()=="terminated")dialogs_.remove(it.key());else dialogs_[it.key()]=it.value();
    }
    entity_=entity;version_=version;return true;
}
BlfState DialogInfo::state() const {
    if(version_<0)return BlfState::Unknown;
    if(dialogs_.values().contains("confirmed"))return BlfState::Busy;
    return dialogs_.isEmpty()?BlfState::Idle:BlfState::Ringing;
}

void SipEngine::setExpansionKeys(const QVector<ExpansionKey> &keys) {
    auto normalized=keys;normalized.resize(ExpansionKeyCount);
    preferences_.expansionKeys=normalized;
    if(expansionKeys_==normalized){syncBlf();return;}
    clearBlf();expansionKeys_=normalized;
    for(auto *child:certificateCores_)child->setExpansionKeys(normalized);
    syncBlf();
}
BlfInfo SipEngine::blfInfo(int key) const {return blfInfos_.value(key,BlfInfo{key,BlfState::Unknown,{}});}
void SipEngine::setBlfState(int key,BlfState state,const QString &detail) {
    const auto previous=blfInfos_.value(key);if(previous.key==key&&previous.state==state&&previous.detail==detail)return;
    const BlfInfo info{key,state,detail};blfInfos_[key]=info;emit blfChanged(info);
}
void SipEngine::releaseBlf(int key) {
    if(!blfSubscriptions_.contains(key))return;
    auto handle=blfSubscriptions_.take(key);blfEvents_.remove(handle.event);
    linphone_event_terminate(handle.event);
    // SDK 5.3 can still dispatch an in-flight NOTIFY after unsubscribe. Keep
    // its event alive through SIP transaction expiry, and through core_stop
    // during shutdown, so the SAL callback cannot follow a freed user pointer.
    retiredBlf_.append({handle.event,QDateTime::currentMSecsSinceEpoch()+65000});
}
void SipEngine::clearBlf() {
    const auto keys=blfSubscriptions_.keys();for(int key:keys)releaseBlf(key);
    for(int n=0;n<ExpansionKeyCount;++n)
        if(!certificateLeaf_||accounts_.contains(expansionKeys_.value(n).accountId))setBlfState(n,BlfState::Unknown,{});
}
void SipEngine::syncBlf() {
    if(!core_||stopping_)return;
    const auto now=QDateTime::currentMSecsSinceEpoch();
    for(int n=retiredBlf_.size()-1;n>=0;--n)if(retiredBlf_[n].second<=now){auto *event=retiredBlf_.takeAt(n).first;linphone_event_unref(event);}
    for(int n=0;n<expansionKeys_.size();++n) {
        const auto key=expansionKeys_[n];
        if(key.type!="blf"||!accounts_.contains(key.accountId))continue;
        const auto &account=accounts_[key.accountId];
        const auto registration=linphone_account_get_state(account.account);
        if(!account.config.enabled||!account.config.registerEnabled||
           (registration!=LinphoneRegistrationOk&&registration!=LinphoneRegistrationRefreshing)) {
            releaseBlf(n);setBlfState(n,BlfState::Unknown,"Register this account to monitor the extension.");continue;
        }
        if(blfSubscriptions_.contains(n)) {
            const auto &handle=blfSubscriptions_[n];
            if(!handle.retryAt||now<handle.retryAt)continue;
            releaseBlf(n);
        }
        if(!key.validate().isEmpty()){setBlfState(n,BlfState::Error,key.validate());continue;}
        LinphoneProxyConfig *proxy=nullptr;
        const auto *identity=linphone_account_params_get_identity_address(linphone_account_get_params(account.account));
        for(auto *node=linphone_core_get_proxy_config_list(core_);node;node=node->next) {
            auto *candidate=static_cast<LinphoneProxyConfig*>(node->data);
            const auto *candidateIdentity=linphone_proxy_config_get_identity_address(candidate);
            if(candidateIdentity&&linphone_address_weak_equal(identity,candidateIdentity)){proxy=candidate;break;}
        }
        auto *target=targetAddress(key.target,key.accountId);
        if(!proxy||!target){if(target)linphone_address_unref(target);setBlfState(n,BlfState::Error,"Invalid BLF account or extension.");continue;}
        auto *event=linphone_core_create_subscribe_2(core_,target,proxy,"dialog",600);linphone_address_unref(target);
        if(!event){setBlfState(n,BlfState::Error,"Could not create BLF subscription.");continue;}
        blfSubscriptions_.insert(n,{event,{},0});blfEvents_[event]=n;
        linphone_event_add_custom_header(event,"Accept","application/dialog-info+xml");
        setBlfState(n,BlfState::Subscribing,"Subscribing to "+key.target);
        if(linphone_event_send_subscribe(event,nullptr)!=0) {
            blfSubscriptions_[n].retryAt=now+30000;setBlfState(n,BlfState::Error,"PBX subscription failed; retrying automatically.");
        }
    }
}
void SipEngine::blfSubscriptionCb(LinphoneCore *core,LinphoneEvent *event,LinphoneSubscriptionState state) {
    auto *engine=static_cast<SipEngine*>(linphone_core_get_user_data(core));
    if(!engine||engine->stopping_||!engine->blfEvents_.contains(event))return;
    const auto key=engine->blfEvents_.value(event);
    if(state==LinphoneSubscriptionError||state==LinphoneSubscriptionTerminated) {
        const auto *error=linphone_event_get_error_info(event);
        const int code=error?linphone_error_info_get_protocol_code(error):0;
        const auto phrase=QString::fromUtf8(error&&linphone_error_info_get_phrase(error)?linphone_error_info_get_phrase(error):"Subscription ended");
        engine->blfSubscriptions_[key].retryAt=QDateTime::currentMSecsSinceEpoch()+30000;
        engine->setBlfState(key,BlfState::Error,QString("%1 %2 — retrying automatically").arg(code).arg(phrase));
    }
}
void SipEngine::blfNotifyCb(LinphoneCore *core,LinphoneEvent *event,const char *name,const LinphoneContent *body) {
    auto *engine=static_cast<SipEngine*>(linphone_core_get_user_data(core));
    if(!engine||engine->stopping_||!engine->blfEvents_.contains(event)||QByteArray(name)!="dialog")return;
    const auto key=engine->blfEvents_.value(event);QString error;
    const auto type=body?QByteArray(linphone_content_get_type(body)).toLower():QByteArray();
    const auto subtype=body?QByteArray(linphone_content_get_subtype(body)).toLower():QByteArray();
    auto &subscription=engine->blfSubscriptions_[key];
    if(subscription.retryAt)return;
    if(type!="application"||subtype!="dialog-info+xml"||
       !subscription.dialogs.update(body&&linphone_content_get_size(body)<=262144?
            QByteArray(reinterpret_cast<const char*>(linphone_content_get_buffer(body)),int(linphone_content_get_size(body))):QByteArray(),&error)) {
        engine->blfSubscriptions_[key].retryAt=QDateTime::currentMSecsSinceEpoch()+3000;
        engine->setBlfState(key,BlfState::Error,error.isEmpty()?"PBX sent unsupported BLF content.":error);return;
    }
    auto *entity=linphone_factory_create_address(linphone_factory_get(),subscription.dialogs.entity().toUtf8().constData());
    auto *expected=engine->targetAddress(engine->expansionKeys_[key].target,engine->expansionKeys_[key].accountId);
    const auto actualUser=entity?QString::fromUtf8(linphone_address_get_username(entity)):QString();
    const auto expectedUser=expected?QString::fromUtf8(linphone_address_get_username(expected)):QString();
    if(entity)linphone_address_unref(entity);
    if(expected)linphone_address_unref(expected);
    // PBXs can use an alias for their own domain in entity; the monitored SIP
    // user must still match. Never paint a different extension as available.
    if(actualUser.isEmpty()||actualUser!=expectedUser){subscription.retryAt=QDateTime::currentMSecsSinceEpoch()+3000;engine->setBlfState(key,BlfState::Error,"PBX sent status for a different extension.");return;}
    engine->setBlfState(key,engine->blfSubscriptions_[key].dialogs.state(),"PBX: "+engine->expansionKeys_[key].target);
}
}
