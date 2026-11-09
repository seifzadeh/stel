#include "videoview.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <mediastreamer2/msfactory.h>
#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msvideo.h>
#ifdef STEL_LIBYUV_RGB
#include <libyuv/convert_argb.h>
#endif

namespace stel {
namespace {
QMutex frameMutex;
QSet<quintptr> surfaces;
QHash<quintptr,QImage> latestFrames;
struct Display {
    quintptr surface=0;
    MSScalerContext *scaler=nullptr;
    int width=0,height=0;
    bool mirror=false,visible=true;
};
void displayInit(MSFilter *f){f->data=new Display;}
void displayUninit(MSFilter *f){auto *d=static_cast<Display*>(f->data);if(d->scaler)ms_scaler_context_free(d->scaler);delete d;}
void displayProcess(MSFilter *f) {
    ms_filter_lock(f);auto *d=static_cast<Display*>(f->data);
    bool active=false;{QMutexLocker lock(&frameMutex);active=surfaces.contains(d->surface);}
    auto *last=f->inputs[0]?ms_queue_peek_last(f->inputs[0]):nullptr;
    MSPicture picture{};
    if(active&&d->visible&&last&&ms_yuv_buf_init_from_mblk(&picture,last)==0&&picture.w>0&&picture.h>0) {
#ifndef STEL_LIBYUV_RGB
        if(d->width!=picture.w||d->height!=picture.h){
            if(d->scaler)ms_scaler_context_free(d->scaler);
            d->width=picture.w;d->height=picture.h;
            d->scaler=ms_scaler_create_context(picture.w,picture.h,MS_YUV420P,picture.w,picture.h,MS_RGB24,MS_SCALER_METHOD_BILINEAR);
        }
        if(d->scaler){
#endif
            QImage frame(picture.w,picture.h,QImage::Format_RGB888);
            if(!frame.isNull()){
#ifdef STEL_LIBYUV_RGB
                const int converted=libyuv::I420ToRAW(picture.planes[0],picture.strides[0],
                    picture.planes[1],picture.strides[1],picture.planes[2],picture.strides[2],
                    frame.bits(),int(frame.bytesPerLine()),picture.w,picture.h);
#else
                uint8_t *pixels[4]={frame.bits(),nullptr,nullptr,nullptr};int strides[4]={int(frame.bytesPerLine()),0,0,0};
                const int converted=ms_scaler_process(d->scaler,picture.planes,picture.strides,pixels,strides);
#endif
                if(converted>=0){
                    if(d->mirror&&!mblk_get_precious_flag(last))frame=frame.mirrored(true,false);
                    QMutexLocker lock(&frameMutex);if(surfaces.contains(d->surface))latestFrames[d->surface]=frame;
                }
            }
#ifndef STEL_LIBYUV_RGB
        }
#endif
    }
    for(int n=0;n<2;++n)if(f->inputs[n])ms_queue_flush(f->inputs[n]);
    ms_filter_unlock(f);
}
int setSurface(MSFilter *f,void *arg){ms_filter_lock(f);static_cast<Display*>(f->data)->surface=*static_cast<quintptr*>(arg);ms_filter_unlock(f);return 0;}
int getSurface(MSFilter *f,void *arg){ms_filter_lock(f);*static_cast<quintptr*>(arg)=static_cast<Display*>(f->data)->surface;ms_filter_unlock(f);return 0;}
int setMirror(MSFilter *f,void *arg){ms_filter_lock(f);static_cast<Display*>(f->data)->mirror=*static_cast<int*>(arg)!=0;ms_filter_unlock(f);return 0;}
int showVideo(MSFilter *f,void *arg){ms_filter_lock(f);static_cast<Display*>(f->data)->visible=*static_cast<int*>(arg)!=0;ms_filter_unlock(f);return 0;}
int ignore(MSFilter *,void *){return 0;}
MSFilterMethod methods[]={
    {MS_FILTER_SET_VIDEO_SIZE,ignore},{MS_VIDEO_DISPLAY_SET_NATIVE_WINDOW_ID,setSurface},{MS_VIDEO_DISPLAY_GET_NATIVE_WINDOW_ID,getSurface},
    {MS_VIDEO_DISPLAY_ENABLE_MIRRORING,setMirror},{MS_VIDEO_DISPLAY_SHOW_VIDEO,showVideo},
    {MS_VIDEO_DISPLAY_ENABLE_AUTOFIT,ignore},{MS_VIDEO_DISPLAY_SET_LOCAL_VIEW_MODE,ignore},
    {MS_VIDEO_DISPLAY_SET_LOCAL_VIEW_SCALEFACTOR,ignore},{MS_VIDEO_DISPLAY_SET_BACKGROUND_COLOR,ignore},
    {MS_VIDEO_DISPLAY_SET_MODE,ignore},{MS_VIDEO_DISPLAY_ZOOM,ignore},{0,nullptr}
};
MSFilterDesc descriptor={MS_FILTER_PLUGIN_ID,"StelQtVideo","Qt software video display",MS_FILTER_OTHER,nullptr,2,0,displayInit,nullptr,displayProcess,nullptr,displayUninit,methods,0};
}
void installQtVideoRenderer(LinphoneCore *core) {
    auto *factory=linphone_core_get_ms_factory(core);
    if(!ms_factory_lookup_filter_by_name(factory,"StelQtVideo"))ms_factory_register_filter(factory,&descriptor);
    linphone_core_set_video_display_filter(core,"StelQtVideo");
}
VideoView::VideoView(QWidget *parent):QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);setAttribute(Qt::WA_OpaquePaintEvent);
    auto *timer=new QTimer(this);timer->setInterval(40);
    connect(timer,&QTimer::timeout,this,[this]{
        QImage next;{QMutexLocker lock(&frameMutex);next=latestFrames.take(surface_);}
        if(!next.isNull()){frame_=next;setProperty("hasVideoFrame",true);update();}
    });timer->start();
}
VideoView::~VideoView(){QMutexLocker lock(&frameMutex);surfaces.remove(surface_);latestFrames.remove(surface_);}
quintptr VideoView::surfaceId(){
    const auto id=quintptr(winId());if(id!=surface_){QMutexLocker lock(&frameMutex);surfaces.remove(surface_);latestFrames.remove(surface_);surface_=id;surfaces.insert(id);}return id;
}
void VideoView::paintEvent(QPaintEvent *) {
    QPainter painter(this);painter.fillRect(rect(),QColor("#132c3b"));
    if(!frame_.isNull()){const auto size=frame_.size().scaled(this->size(),Qt::KeepAspectRatio);const QRect target(QPoint((width()-size.width())/2,(height()-size.height())/2),size);painter.drawImage(target,frame_);}
}
}
