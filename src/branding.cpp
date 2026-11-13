#include "branding.h"
#include <QResource>
#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QVector>
#include <QTabBar>
#include <QTabWidget>

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
namespace {
class NavigationIconEngine : public QIconEngine {
public:
    explicit NavigationIconEngine(NavigationIcon icon):icon_(icon){}
    QIconEngine *clone() const override{return new NavigationIconEngine(icon_);}
    QPixmap pixmap(const QSize &size,QIcon::Mode mode,QIcon::State state) override{
        QPixmap result(size);result.fill(Qt::transparent);QPainter painter(&result);paint(&painter,QRect(QPoint(),size),mode,state);return result;
    }
    void paint(QPainter *p,const QRect &rect,QIcon::Mode mode,QIcon::State state) override{
        p->save();p->setRenderHint(QPainter::Antialiasing);
        const qreal side=qMin(rect.width(),rect.height());
        p->translate(rect.x()+(rect.width()-side)/2,rect.y()+(rect.height()-side)/2);p->scale(side/24.0,side/24.0);
        const QColor color=mode==QIcon::Disabled?QColor("#a6b4bf"):
            (state==QIcon::On||mode==QIcon::Selected||mode==QIcon::Active)?QColor("#08796b"):QColor("#647d8c");
        p->setPen(QPen(color,1.75,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p->setBrush(Qt::NoBrush);
        QPainterPath path;
        switch(icon_){
        case NavigationIcon::Phone:
            path.moveTo(7.2,3.5);path.lineTo(4.8,3.5);path.quadTo(3,3.5,3.3,5.5);path.cubicTo(4.2,13.6,10.4,19.8,18.5,20.7);
            path.quadTo(20.5,21,20.5,19.2);path.lineTo(20.5,16.8);path.lineTo(15.8,14.6);path.lineTo(13.5,16.6);
            path.quadTo(9.2,14.6,7.4,10.4);path.lineTo(9.4,8.2);path.closeSubpath();p->drawPath(path);break;
        case NavigationIcon::Messages:
            path.moveTo(7,17);path.lineTo(4,20.5);path.lineTo(4,15.5);path.quadTo(2.5,15,2.5,13.5);path.lineTo(2.5,6);
            path.quadTo(2.5,3.5,5,3.5);path.lineTo(19,3.5);path.quadTo(21.5,3.5,21.5,6);path.lineTo(21.5,14.5);
            path.quadTo(21.5,17,19,17);path.closeSubpath();p->drawPath(path);p->drawLine(QPointF(7,8),QPointF(17,8));p->drawLine(QPointF(7,12),QPointF(14,12));break;
        case NavigationIcon::Contacts:
            p->drawRoundedRect(QRectF(5,3,15,18),2,2);p->drawLine(QPointF(8,3),QPointF(8,21));p->drawEllipse(QPointF(14,9),2.2,2.2);
            path.moveTo(10.5,17);path.cubicTo(10.5,12.5,17.5,12.5,17.5,17);p->drawPath(path);p->drawLine(QPointF(3,7),QPointF(5,7));p->drawLine(QPointF(3,17),QPointF(5,17));break;
        case NavigationIcon::History:
            p->drawArc(QRectF(4,4,16,16),45*16,-305*16);p->drawPolyline(QPolygonF(QVector<QPointF>{QPointF(3,4),QPointF(3,9),QPointF(8,9)}));
            p->drawPolyline(QPolygonF(QVector<QPointF>{QPointF(12,7),QPointF(12,12),QPointF(16,14)}));break;
        case NavigationIcon::Accounts:
            p->drawEllipse(QPointF(9,7.5),3,3);path.moveTo(3,20);path.lineTo(3,18);path.cubicTo(3,12,15,12,15,18);path.lineTo(15,20);
            path.moveTo(16,4.8);path.cubicTo(20,4.8,20,10.2,16,10.2);path.moveTo(18,14);path.cubicTo(21,14.5,21,17,21,20);p->drawPath(path);break;
        case NavigationIcon::Audio:
            p->drawRoundedRect(QRectF(9,2.5,6,12),3,3);path.moveTo(6,10);path.lineTo(6,12);path.cubicTo(6,20,18,20,18,12);path.lineTo(18,10);p->drawPath(path);
            p->drawLine(QPointF(12,18),QPointF(12,22));p->drawLine(QPointF(8,22),QPointF(16,22));break;
        case NavigationIcon::Video:
            p->drawRoundedRect(QRectF(2.5,5.5,13,13),2,2);p->drawPolygon(QPolygonF(QVector<QPointF>{QPointF(15.5,10),QPointF(21.5,6.5),QPointF(21.5,17.5),QPointF(15.5,14)}));break;
        case NavigationIcon::Network:
            p->drawEllipse(QPointF(12,4.5),2.5,2.5);p->drawLine(QPointF(12,7),QPointF(12,11));p->drawLine(QPointF(5,11),QPointF(19,11));
            p->drawLine(QPointF(5,11),QPointF(5,16));p->drawLine(QPointF(19,11),QPointF(19,16));p->drawRoundedRect(QRectF(2,16,6,5),1,1);p->drawRoundedRect(QRectF(16,16,6,5),1,1);break;
        case NavigationIcon::Security:
            path.moveTo(12,2.5);path.lineTo(20,5.5);path.lineTo(20,11);path.cubicTo(20,16,16,20,12,21.5);path.cubicTo(8,20,4,16,4,11);path.lineTo(4,5.5);path.closeSubpath();p->drawPath(path);
            p->drawPolyline(QPolygonF(QVector<QPointF>{QPointF(8,12),QPointF(11,15),QPointF(16,9)}));break;
        case NavigationIcon::Info:
            p->drawEllipse(QPointF(12,12),9,9);p->drawPoint(QPointF(12,7));p->drawLine(QPointF(12,11),QPointF(12,17));break;
        case NavigationIcon::General:
            for(int row=0;row<3;++row){const qreal y=6+row*6,x=row==1?16:row==0?8:10;
                p->drawLine(QPointF(3,y),QPointF(x-2,y));p->drawLine(QPointF(x+2,y),QPointF(21,y));p->drawEllipse(QPointF(x,y),2,2);}break;
        case NavigationIcon::Globe:
            p->drawEllipse(QPointF(12,12),9,9);p->drawEllipse(QRectF(8,3,8,18));p->drawLine(QPointF(3,12),QPointF(21,12));break;
        case NavigationIcon::Forward:
            p->drawLine(QPointF(3,12),QPointF(20,12));p->drawPolyline(QPolygonF(QVector<QPointF>{QPointF(14,6),QPointF(20,12),QPointF(14,18)}));break;
        case NavigationIcon::Document:
            path.moveTo(5,3);path.lineTo(15,3);path.lineTo(20,8);path.lineTo(20,21);path.lineTo(5,21);path.closeSubpath();
            path.moveTo(15,3);path.lineTo(15,8);path.lineTo(20,8);p->drawPath(path);p->drawLine(QPointF(8,12),QPointF(17,12));p->drawLine(QPointF(8,16),QPointF(15,16));break;
        }
        p->restore();
    }
private:
    NavigationIcon icon_;
};
}
QIcon navigationIcon(NavigationIcon icon){return QIcon(new NavigationIconEngine(icon));}
void styleNavigationTabs(QTabWidget *pages,NavigationLevel level){
    auto *bar=pages->tabBar();bar->setDrawBase(false);bar->setExpanding(false);bar->setUsesScrollButtons(true);bar->setElideMode(Qt::ElideNone);
    const bool compact=level==NavigationLevel::Compact,settings=level==NavigationLevel::Settings;
    pages->setIconSize(QSize(compact?17:settings?23:21,compact?17:settings?23:21));
    bar->setStyleSheet(QString(R"(
        QTabBar { background:transparent; border:0; }
        QTabBar::tab {
            background:%1; color:#607786; border:1px solid %2; border-radius:%3px;
            padding:%4px %5px; margin:0px %6px 0px 0px; min-height:%7px; font-weight:500;
        }
        QTabBar::tab:hover { background:#e8f5f0; color:#206e60; border-color:#bcded4; }
        QTabBar::tab:selected { background:#def3ec; color:#087567; border:1px solid #a6d6c8; font-weight:600; }
        QTabBar::tab:selected:focus { border:1px solid #138e81; }
        QTabBar::tab:disabled { color:#a4b1ba; background:#edf1f4; border-color:#e4eaee; }
        QTabBar::scroller { width:28px; }
    )").arg(settings?"#ffffff":"#eaf0f4",settings?"#dbe5eb":"#e1e9ee")
       .arg(compact?7:10).arg(compact?7:settings?12:10).arg(compact?11:settings?16:17).arg(compact?5:7).arg(compact?18:settings?26:22));
    pages->setStyleSheet(QString("QTabWidget#%1::pane { border:1px solid #d7e3e9; border-radius:%2px; background:#fafcfd; margin-top:%3px; }")
        .arg(pages->objectName()).arg(compact?8:11).arg(compact?6:9));
}
}
