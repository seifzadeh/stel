#include "dummydevices.h"
#include <QFile>
#include <QSaveFile>
#include <QImage>
#include <mediastreamer2/msfactory.h>
#include <mediastreamer2/mssndcard.h>
#include <mediastreamer2/msticker.h>
#include <cmath>
#include <cstdint>

namespace stel {
namespace {
struct AudioState { int rate=48000, channels=1; double phase=0; uint32_t frames=0; bool tone=false; };
void init(MSFilter *f){f->data=new AudioState;}
void uninit(MSFilter *f){delete static_cast<AudioState*>(f->data);}
int setRate(MSFilter *f,void *v){const int n=*static_cast<int*>(v);if(n<8000||n>192000)return -1;ms_filter_lock(f);static_cast<AudioState*>(f->data)->rate=n;ms_filter_unlock(f);return 0;}
int getRate(MSFilter *f,void *v){ms_filter_lock(f);*static_cast<int*>(v)=static_cast<AudioState*>(f->data)->rate;ms_filter_unlock(f);return 0;}
int setChannels(MSFilter *f,void *v){const int n=*static_cast<int*>(v);if(n<1||n>2)return -1;ms_filter_lock(f);static_cast<AudioState*>(f->data)->channels=n;ms_filter_unlock(f);return 0;}
int getChannels(MSFilter *f,void *v){ms_filter_lock(f);*static_cast<int*>(v)=static_cast<AudioState*>(f->data)->channels;ms_filter_unlock(f);return 0;}
MSFilterMethod methods[]={{MS_FILTER_SET_SAMPLE_RATE,setRate},{MS_FILTER_GET_SAMPLE_RATE,getRate},
    {MS_FILTER_SET_NCHANNELS,setChannels},{MS_FILTER_GET_NCHANNELS,getChannels},{0,nullptr}};
void produce(MSFilter *f) {
    ms_filter_lock(f);auto &s=*static_cast<AudioState*>(f->data);
    const int frames=s.rate*f->ticker->interval/1000;
    auto *msg=allocb(frames*s.channels*2,0);if(!msg){ms_filter_unlock(f);return;}
    auto *samples=reinterpret_cast<int16_t*>(msg->b_wptr);
    for(int i=0;i<frames;++i) {
        const int16_t value=s.tone?static_cast<int16_t>(std::sin(s.phase)*2500):0;
        for(int c=0;c<s.channels;++c)*samples++=value;
        s.phase+=2*3.141592653589793*440/s.rate;if(s.phase>2*3.141592653589793)s.phase-=2*3.141592653589793;
    }
    msg->b_wptr+=frames*s.channels*2;mblk_set_timestamp_info(msg,s.frames);s.frames+=frames;
    ms_queue_put(f->outputs[0],msg);ms_filter_unlock(f);
}
void consume(MSFilter *f){ms_queue_flush(f->inputs[0]);}
MSFilterDesc reader={MS_FILTER_PLUGIN_ID,"StelDummyMic","Internal PCM test source",MS_FILTER_OTHER,nullptr,0,1,init,nullptr,produce,nullptr,uninit,methods,0};
MSFilterDesc writer={MS_FILTER_PLUGIN_ID,"StelDummySpeaker","Discard test playback",MS_FILTER_OTHER,nullptr,1,0,init,nullptr,consume,nullptr,uninit,methods,0};
MSFilter *createReader(MSSndCard *card) {
    auto *f=ms_factory_create_filter_from_desc(card->sndcardmanager->factory,&reader);
    if(f)static_cast<AudioState*>(f->data)->tone=card->internal_id==1;
    return f;
}
MSFilter *createWriter(MSSndCard *card){return ms_factory_create_filter_from_desc(card->sndcardmanager->factory,&writer);}
MSSndCardDesc descriptor{};
void detect(MSSndCardManager *manager) {
    const char *names[]={"Dummy mic (silence)","Dummy mic (440 Hz tone)","Dummy speaker (discard)"};
    for(int i=0;i<3;++i) {
        auto *card=ms_snd_card_new(&descriptor);card->name=ms_strdup(names[i]);card->internal_id=i;
        card->capabilities=i==2?MS_SND_CARD_CAP_PLAYBACK:MS_SND_CARD_CAP_CAPTURE;
        card->preferred_sample_rate=48000;ms_snd_card_manager_add_card(manager,card);
    }
}
}
DummyAudioDevices installDummyAudio(MSFactory *factory) {
    static const bool configured=[] {descriptor.driver_type="Stel";descriptor.detect=detect;descriptor.create_reader=createReader;descriptor.create_writer=createWriter;return true;}();
    Q_UNUSED(configured);
    auto *manager=ms_factory_get_snd_card_manager(factory);
    bool found=false;for(const auto *node=ms_snd_card_manager_get_list(manager);node;node=node->next)
        found|=static_cast<MSSndCard*>(node->data)->desc==&descriptor;
    if(!found){ms_snd_card_manager_register_desc(manager,&descriptor);}
    DummyAudioDevices ids;
    for(const auto *node=ms_snd_card_manager_get_list(manager);node;node=node->next) {
        auto *card=static_cast<MSSndCard*>(node->data);if(card->desc!=&descriptor)continue;
        const auto id=QString::fromUtf8(ms_snd_card_get_string_id(card));
        if(card->internal_id==0)ids.silence=id;else if(card->internal_id==1)ids.tone=id;else ids.speaker=id;
    }
    return ids;
}
bool writeDummyCameraImage(const QString &path) {
    // Generate a JPEG chart that the SDK's static webcam can decode on every OS.
    constexpr int width=640,height=480;QByteArray pixels;pixels.reserve(width*height*3);
    const int colors[8][3]={{235,235,235},{235,210,20},{20,210,210},{20,190,50},{210,30,210},{215,35,35},{35,60,210},{25,38,52}};
    for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
        const auto *color=colors[x/(width/8)];
        for(int c=0;c<3;++c)pixels.append(char(y>height*3/4?((x/20+y/20)%2?210:40):color[c]));
    }
    QImage image(reinterpret_cast<const uchar*>(pixels.constData()),width,height,width*3,QImage::Format_RGB888);
    QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;
    return image.save(&f,"JPEG",90)&&f.commit();
}
}
