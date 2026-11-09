#include "sipengine.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <mediastreamer2/msfactory.h>

int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);app.setApplicationName("StelProbe");
    const auto resources=qEnvironmentVariable("STEL_RESOURCE_PREFIX");if(!resources.isEmpty())stel::SipEngine::configureResources(resources);
    if(app.arguments().contains("--codecs")) {
        QTemporaryDir temporary;stel::SipEngine engine;
        if(!engine.start({},temporary.path(),true))return 2;
        for(bool video:{false,true}) {
            auto *list=video?linphone_core_get_video_payload_types(engine.native()):linphone_core_get_audio_payload_types(engine.native());
            for(auto *n=list;n;n=n->next) {
                auto *p=static_cast<LinphonePayloadType*>(n->data);
                QTextStream(stdout)<<(video?"VIDEO ":"AUDIO ")<<linphone_payload_type_get_mime_type(p)<<"/"<<linphone_payload_type_get_clock_rate(p)<<"/"<<linphone_payload_type_get_channels(p)
                    <<" enabled="<<linphone_payload_type_enabled(p)<<" encoder="<<linphone_payload_type_get_encoder_description(p)<<" fmtp="<<linphone_payload_type_get_recv_fmtp(p)<<Qt::endl;
                linphone_payload_type_unref(p);
            }
            bctbx_list_free(list);
        }
        auto *factory=linphone_core_get_ms_factory(engine.native());
        for(const auto *mime:{"AV1","H265","VP9","H263","H263-1998","MP4V-ES","theora"})
            QTextStream(stdout)<<"FILTER "<<mime<<" encode="<<ms_factory_has_encoder(factory,mime)<<" decode="<<ms_factory_has_decoder(factory,mime)<<Qt::endl;
        return 0;
    }
    if(app.arguments().size()!=2)return 2;
    QFile f(app.arguments()[1]);if(!f.open(QIODevice::ReadOnly))return 2;
    const auto o=QJsonDocument::fromJson(f.readAll()).object();auto accountList=o["accounts"].toArray();
    if(accountList.isEmpty())accountList.append(o["account"]);
    QSet<QString> registered;
    QTemporaryDir tmp;stel::SipEngine engine;
    const bool callMode=o["call"].toBool();bool dialed=false;
    QObject::connect(&engine,&stel::SipEngine::errorOccurred,&app,[](const QString &e){QTextStream(stdout)<<"ERROR "<<e<<Qt::endl;});
    QObject::connect(&engine,&stel::SipEngine::accountState,&app,[&](const QString &id,const QString &state,const QString &){
        QTextStream(stdout)<<state<<Qt::endl;
        if(state=="LinphoneRegistrationOk") {
            registered.insert(id);
            if(registered.size()==accountList.size()) {
                if(callMode && !dialed){dialed=true;QTimer::singleShot(0,&app,[&]{if(!engine.dial(accountList[0].toObject()["id"].toString(),"sip:bob@localhost"))app.exit(4);});}
                else if(!callMode)QTimer::singleShot(50,&app,[&]{app.exit(0);});
            }
        }
        if(state=="LinphoneRegistrationFailed")QTimer::singleShot(50,&app,[&]{app.exit(1);});
    });
    QObject::connect(&engine,&stel::SipEngine::callChanged,&app,[&](const stel::CallInfo &i){
        if(callMode && i.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&app,[&,id=i.id]{engine.answer(id,false);});
    });
    if(!engine.start(stel::Preferences::fromJson(o["preferences"].toObject()),tmp.path(),true))return 2;
    for(const auto &item:accountList) {
        auto a=stel::Account::fromJson(item.toObject());a.password=qEnvironmentVariable("STEL_TEST_PASSWORD");a.keyPassword=qEnvironmentVariable("STEL_TEST_KEY_PASSWORD");
        if(!engine.addAccount(a))return 2;
    }
    const auto forced=o["audioCodecId"].toString();
    if(!forced.isEmpty()&&!engine.applyCodecSelection(false,{forced}))return 2;
    QTimer poll;poll.setInterval(200);
    QObject::connect(&poll,&QTimer::timeout,&app,[&]{
        const auto calls=engine.calls();if(!callMode||calls.size()!=2)return;
        for(const auto &i:calls)if(i.rawState!=LinphoneCallStreamsRunning || !i.encryption.contains("srtp",Qt::CaseInsensitive) || i.audioDownKbps<=0 || (!o["requireCodec"].toString().isEmpty()&&i.audioCodec!=o["requireCodec"].toString()))return;
        QTextStream(stdout)<<"CALL_OK: two isolated mTLS accounts connected with SRTP"<<Qt::endl;
        // Exercise command dispatch through the public composite engine.
        engine.mute(true);engine.mute(false);engine.dtmf(calls.first().id,'5');
        for(const auto &i:calls)engine.hangup(i.id);
        QTimer::singleShot(300,&app,[&]{app.exit(0);});poll.stop();
    });
    if(callMode)poll.start();
    QTimer::singleShot(11000,&app,[&]{app.exit(3);});return app.exec();
}
