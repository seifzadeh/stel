#pragma once
#include "sipengine.h"

// Several real endpoints share one host in these integration tests. Give each
// core its own RTP/RTCP pools; SO_REUSEADDR otherwise permits silent collisions.
// Keep explicit test ranges, such as the network bind assertions, unchanged.
class IsolatedSipEngine : public stel::SipEngine {
public:
    explicit IsolatedSipEngine(QObject *parent=nullptr):stel::SipEngine(parent),slot_(nextSlot_++){}
    static void resetPortPools(){nextSlot_=0;}
    bool start(const stel::Preferences &prefs,const QString &directory,bool testMode=false,int port=-2) {
        auto isolated=prefs;const stel::Preferences defaults;
        const int base=20000+slot_*400;
        if(prefs.audioPortMin==defaults.audioPortMin&&prefs.audioPortMax==defaults.audioPortMax){
            isolated.audioPortMin=base;isolated.audioPortMax=base+98;
        }
        if(prefs.videoPortMin==defaults.videoPortMin&&prefs.videoPortMax==defaults.videoPortMax){
            isolated.videoPortMin=base+200;isolated.videoPortMax=base+298;
        }
        return stel::SipEngine::start(isolated,directory,testMode,port);
    }
private:
    inline static int nextSlot_=0;
    int slot_;
};
