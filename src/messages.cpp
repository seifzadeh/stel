#include "sipengine.h"
#include <QCryptographicHash>
#include <QPointer>
#include <QUuid>

namespace stel {
namespace {
QString text(const char *value){return QString::fromUtf8(value?value:"");}
QString uri(const LinphoneAddress *address){
    if(!address)return {};
    auto *copy=linphone_address_clone(address);linphone_address_clean(copy);
    char *value=linphone_address_as_string_uri_only(copy);const auto result=text(value);
    bctbx_free(value);linphone_address_unref(copy);return result;
}
}
QString SipEngine::messageTarget(const QString &id,const QString &target) const {
    if(certificateCores_.contains(id))return certificateCores_[id]->messageTarget(id,target);
    if(!accounts_.contains(id)||target.contains(QChar::Null))return {};
    auto *address=targetAddress(target,id);const auto result=uri(address);
    if(address)linphone_address_unref(address);
    return result;
}
QString SipEngine::sendMessage(const QString &id,const QString &target,const QString &body){
    if(certificateCores_.contains(id))return certificateCores_[id]->sendMessage(id,target,body);
    if(!core_||stopping_||!accounts_.contains(id)||!accounts_[id].config.enabled){fail("Select an enabled account to send a message.");return {};}
    if(body.trimmed().isEmpty()||body.contains(QChar::Null)||body.toUtf8().size()>32768){fail("Enter a message of at most 32 KiB, without NUL characters.");return {};}
    auto *peer=targetAddress(target,id);
    if(!peer||target.contains(QChar::Null)){if(peer)linphone_address_unref(peer);fail("Enter a valid recipient number or SIP address.");return {};}
    const auto *local=linphone_account_params_get_identity_address(linphone_account_get_params(accounts_[id].account));
    auto *params=linphone_chat_room_params_new();
    linphone_chat_room_params_set_backend(params,LinphoneChatRoomBackendBasic);
    linphone_chat_room_params_enable_group(params,FALSE);linphone_chat_room_params_enable_encryption(params,FALSE);
    auto *participants=bctbx_list_append(nullptr,peer);
    auto *room=linphone_core_create_chat_room_6(core_,params,local,participants);
    bctbx_list_free(participants);linphone_chat_room_params_unref(params);
    if(!room){linphone_address_unref(peer);fail("Could not create the SIP conversation.");return {};}
    auto *message=linphone_chat_room_create_message_from_utf8(room,body.toUtf8().constData());
    linphone_chat_room_unref(room);
    if(!message){linphone_address_unref(peer);fail("Could not create the message.");return {};}
    linphone_chat_message_set_content_type(message,"text/plain; charset=utf-8");
    linphone_chat_message_set_to_be_stored(message,FALSE); // App-owned history also works without SDK database support.
    MessageInfo info;info.id=QUuid::createUuid().toString(QUuid::WithoutBraces);info.accountId=id;info.peer=uri(peer);
    info.peerName=text(linphone_address_get_display_name(peer));info.text=body;info.timestamp=QDateTime::currentDateTimeUtc();info.state="sending";
    linphone_address_unref(peer);
    auto *callbacks=linphone_factory_create_chat_message_cbs(linphone_factory_get());
    linphone_chat_message_cbs_set_user_data(callbacks,this);
    linphone_chat_message_cbs_set_msg_state_changed(callbacks,messageStateCb);
    linphone_chat_message_add_callbacks(message,callbacks);
    messages_.insert(message,{info,callbacks});const QPointer<SipEngine> alive(this);emit messageChanged(info);
    if(alive)QTimer::singleShot(0,this,[this,message]{if(!stopping_&&messages_.contains(message))linphone_chat_message_send(message);});
    return info.id;
}
void SipEngine::releaseMessage(LinphoneChatMessage *message){
    if(!messages_.contains(message))return;
    const auto pending=messages_.take(message);
    linphone_chat_message_remove_callbacks(message,pending.callbacks);
    linphone_chat_message_cbs_unref(pending.callbacks);linphone_chat_message_unref(message);
}
void SipEngine::messageStateCb(LinphoneChatMessage *message,LinphoneChatMessageState state){
    auto *callbacks=linphone_chat_message_get_current_callbacks(message);
    auto *engine=static_cast<SipEngine*>(linphone_chat_message_cbs_get_user_data(callbacks));
    if(!engine||!engine->messages_.contains(message))return;
    auto info=engine->messages_[message].info;
    if(state==LinphoneChatMessageStateDelivered)info.state="sent";
    else if(state==LinphoneChatMessageStateDeliveredToUser)info.state="delivered";
    else if(state==LinphoneChatMessageStateDisplayed)info.state="read";
    else if(state==LinphoneChatMessageStateNotDelivered){
        info.state="failed";const auto *error=linphone_chat_message_get_error_info(message);
        // SDK 5.3 can release the SIP operation before this notification, so
        // error details may be unavailable. Never invent a protocol status.
        if(error){info.sipStatus=linphone_error_info_get_protocol_code(error);info.error=text(linphone_error_info_get_phrase(error));}
        if(info.error.isEmpty())info.error="The server could not accept this message.";
    }else info.state="sending";
    engine->messages_[message].info=info;
    const QPointer<SipEngine> alive(engine);emit engine->messageChanged(info);
    if(alive&&info.state!="sending")QTimer::singleShot(0,engine,[engine,message]{engine->releaseMessage(message);});
}
void SipEngine::messageReceivedCb(LinphoneCore *core,LinphoneChatRoom *,LinphoneChatMessage *message){
    auto *engine=static_cast<SipEngine*>(linphone_core_get_user_data(core));
    if(!engine||engine->stopping_)return;
    const char *body=linphone_chat_message_get_utf8_text(message);if(!body)return;
    const auto *local=linphone_chat_message_get_to_address(message);
    QString accountId;
    for(auto it=engine->accounts_.cbegin();it!=engine->accounts_.cend();++it){
        const auto *identity=linphone_account_params_get_identity_address(linphone_account_get_params(it->account));
        if(local&&linphone_address_weak_equal(local,identity)){accountId=it.key();break;}
    }
    if(accountId.isEmpty())return; // Never attribute a message to an arbitrary default account.
    const auto *from=linphone_chat_message_get_from_address(message);
    if(!from)return;
    MessageInfo info;info.accountId=accountId;info.peer=uri(from);info.peerName=text(linphone_address_get_display_name(from));
    info.text=text(body);info.incoming=true;info.unread=true;info.state="received";
    const auto wireId=text(linphone_chat_message_get_message_id(message));
    info.id="in-"+QString::fromLatin1(QCryptographicHash::hash((accountId+'\n'+info.peer+'\n'+wireId).toUtf8(),QCryptographicHash::Sha256).toHex());
    if(wireId.isEmpty())info.id="in-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    info.timestamp=QDateTime::fromSecsSinceEpoch(qint64(linphone_chat_message_get_time(message)),Qt::UTC);
    emit engine->messageChanged(info);
}
}
