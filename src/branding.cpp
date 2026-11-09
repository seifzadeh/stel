#include "branding.h"
#include <QResource>

static void registerBrandResources() {Q_INIT_RESOURCE(branding);}

namespace stel {
QIcon applicationIcon() {
    static const auto icon=[] {
        registerBrandResources();QIcon result;
        for(int size:{16,20,24,32,40,48,64,128,256,512,1024})result.addFile(QString(":/branding/stel-%1.png").arg(size),QSize(size,size));
        return result;
    }();return icon;
}
QPixmap applicationWordmark(qreal ratio) {
    applicationIcon();auto mark=QPixmap(":/branding/stel-wordmark.png").scaledToHeight(qRound(52*ratio),Qt::SmoothTransformation);mark.setDevicePixelRatio(ratio);return mark;
}
}
