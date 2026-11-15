#include "deskphone.h"
#include "calldialog.h"
#include "branding.h"
#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTabBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTimer>
#include <QWindow>

namespace stel {
void paintDeskIcon(QPainter &p,DeskIcon icon,const QRectF &r,const QColor &color){
    p.save();p.translate(r.topLeft());p.scale(r.width()/24,r.height()/24);p.setPen(QPen(color,1.7,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));p.setBrush(Qt::NoBrush);QPainterPath a;
    switch(icon){
    case DeskIcon::Hangup:p.translate(12,12);p.rotate(-45);p.translate(-12,-12);[[fallthrough]];
    case DeskIcon::Phone:
        a.moveTo(6,2);a.lineTo(3,3);a.cubicTo(0,9,15,24,21,21);a.lineTo(22,18);a.lineTo(16,14);a.lineTo(13,17);a.quadTo(8,14,7,10);a.lineTo(10,7);a.closeSubpath();p.fillPath(a,color);break;
    case DeskIcon::Fax:
        p.drawRect(QRectF(6,2,12,6));p.drawRoundedRect(QRectF(2,8,20,11),2,2);p.fillRect(QRectF(6,14,12,8),color);p.setPen(QPen(QColor("#183047"),1));p.drawLine(8,17,16,17);p.drawLine(8,19,16,19);p.setPen(color);p.drawPoint(5,11);break;
    case DeskIcon::Contacts:case DeskIcon::Conference:
        p.setBrush(color);p.setPen(Qt::NoPen);p.drawEllipse(QRectF(8,2,7,7));p.drawEllipse(QRectF(1,5,5,5));p.drawEllipse(QRectF(18,5,5,5));a.moveTo(5,21);a.cubicTo(4,8,20,8,19,21);a.closeSubpath();p.drawPath(a);p.drawRoundedRect(QRectF(0,13,5,7),2,2);p.drawRoundedRect(QRectF(20,13,4,7),2,2);break;
    case DeskIcon::Messages:p.drawRoundedRect(QRectF(2,4,20,16),1,1);p.drawLine(2,5,12,13);p.drawLine(12,13,22,5);break;
    case DeskIcon::History:p.drawEllipse(QRectF(2,2,20,20));p.drawLine(12,5,12,12);p.drawLine(12,12,17,16);break;
    case DeskIcon::Hold:p.fillRect(QRectF(6,3,4,18),color);p.fillRect(QRectF(15,3,4,18),color);break;
    case DeskIcon::Transfer:a.moveTo(3,20);a.lineTo(3,15);a.quadTo(3,8,10,8);a.lineTo(20,8);a.moveTo(15,3);a.lineTo(21,8);a.lineTo(15,13);p.drawPath(a);break;
    case DeskIcon::Add:p.drawLine(12,3,12,21);p.drawLine(3,12,21,12);break;
    case DeskIcon::Record:p.setPen(QPen(color,2.5));p.drawEllipse(QRectF(3,3,18,18));break;
    case DeskIcon::Keypad:p.setPen(Qt::NoPen);p.setBrush(color);for(int y=0;y<4;++y)for(int x=0;x<3;++x)p.drawEllipse(QPointF(5+x*7,3+y*6),1.7,1.7);break;
    case DeskIcon::Mute:p.drawRoundedRect(QRectF(9,2,6,12),3,3);a.moveTo(5,10);a.cubicTo(5,22,19,22,19,10);a.moveTo(12,18);a.lineTo(12,23);a.moveTo(8,23);a.lineTo(16,23);p.drawPath(a);p.setPen(QPen(color,2.4));p.drawLine(3,22,21,1);break;
    case DeskIcon::Speaker:a.moveTo(2,9);a.lineTo(7,9);a.lineTo(13,3);a.lineTo(13,21);a.lineTo(7,15);a.lineTo(2,15);a.closeSubpath();p.fillPath(a,color);p.drawArc(QRectF(10,6,10,12),-70*16,140*16);p.drawArc(QRectF(10,2,17,20),-65*16,130*16);break;
    case DeskIcon::Settings:p.drawEllipse(QRectF(6,6,12,12));p.drawEllipse(QRectF(10,10,4,4));for(int k=0;k<8;++k){p.save();p.translate(12,12);p.rotate(k*45);p.setPen(QPen(color,3));p.drawLine(0,-7,0,-10);p.restore();}break;
    case DeskIcon::Minimize:p.drawLine(4,15,20,15);break;
    case DeskIcon::Maximize:p.drawRect(QRectF(5,4,15,16));break;
    case DeskIcon::Close:p.drawLine(5,5,19,19);p.drawLine(5,19,19,5);break;
    case DeskIcon::Redial:p.drawArc(QRectF(3,3,18,18),40*16,290*16);a.moveTo(16,3);a.lineTo(21,8);a.lineTo(21,1);p.drawPath(a);break;
    case DeskIcon::Video:p.drawRoundedRect(QRectF(2,5,13,14),2,2);a.moveTo(15,10);a.lineTo(22,6);a.lineTo(22,18);a.lineTo(15,14);p.drawPath(a);break;
    case DeskIcon::Previous:case DeskIcon::Next:p.translate(12,12);if(icon==DeskIcon::Next)p.rotate(180);a.moveTo(3,-7);a.lineTo(-4,0);a.lineTo(3,7);a.closeSubpath();p.fillPath(a,color);break;
    case DeskIcon::Attach:a.moveTo(8,15);a.lineTo(16,7);a.cubicTo(20,3,24,8,20,12);a.lineTo(10,22);a.cubicTo(2,28,-3,17,3,11);a.lineTo(13,1);p.drawPath(a);break;
    case DeskIcon::Check:p.drawLine(5,12,10,17);p.drawLine(10,17,20,6);break;
    case DeskIcon::Network:p.drawRect(QRectF(9,1,6,5));p.drawLine(12,6,12,12);p.drawLine(3,12,21,12);for(int x:{3,12,21}){p.drawLine(x,12,x,17);p.drawRect(QRectF(x-2.5,17,5,5));}break;
    case DeskIcon::None:break;
    }p.restore();
}
DeskButton::DeskButton(const QString &text,DeskIcon icon,QWidget *parent,Face f):QPushButton(text,parent),caption(text),symbol(icon),face(f){setCursor(Qt::PointingHandCursor);setAttribute(Qt::WA_Hover);setFlat(true);setAccessibleName(text);}
void DeskButton::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);const auto size=designSize.isEmpty()?QSizeF(width(),height()):designSize;p.scale(width()/size.width(),height()/size.height());const QRectF r(1,1,size.width()-2,size.height()-2);
    const bool active=isChecked()||isDown();
    if(face!=Physical){
        QLinearGradient g(0,0,0,size.height());
        if(face==Navigation&&!active){g.setColorAt(0,QColor(8,33,62,40));g.setColorAt(1,QColor(4,23,42,110));}
        else if(face==Silver){g.setColorAt(0,QColor("#f2f4f7"));g.setColorAt(.45,QColor("#c5c9d0"));g.setColorAt(1,QColor("#a9aeb7"));}
        else {g.setColorAt(0,active?QColor("#087ef2"):QColor(61,103,152,220));g.setColorAt(.4,active?QColor("#0068d9"):QColor(27,64,108,215));g.setColorAt(1,QColor(6,28,54,245));}
        p.setBrush(g);p.setPen(QPen(active?QColor("#38d3ff"):face==Navigation?QColor(0,0,0,0):QColor(126,166,207,150),active?2:1));p.drawRoundedRect(r,face==Navigation?6:8,8);
        p.setPen(QColor(239,249,255,55));p.drawLine(QPointF(6,3),QPointF(size.width()-6,3));
    }
    if(underMouse()||isDown()||(active&&face==Physical)){p.setPen(QPen(QColor(82,197,255,200),1.5));p.setBrush(QColor(20,149,255,isDown()?70:30));p.drawRoundedRect(r.adjusted(2,2,-2,-2),8,8);}
    p.setOpacity(isEnabled()?1:.34);const QColor ink=(face==Silver||property("darkInk").toBool())?QColor("#19222b"):QColor("#edf5ff");
    QFont font("Segoe UI");font.setPixelSize(horizontal&&face==Physical?21:18);p.setFont(font);p.setPen(ink);
    if(horizontal){const qreal iconSize=qMin<qreal>(30,size.height()-14);if(symbol!=DeskIcon::None)paintDeskIcon(p,symbol,QRectF(20,(size.height()-iconSize)/2,iconSize,iconSize),ink);
        const qreal x=symbol==DeskIcon::None?8:60;
        if(subcaption.isEmpty())p.drawText(QRectF(x,0,size.width()-x-6,size.height()),Qt::AlignVCenter|Qt::AlignLeft,caption);
        else {font.setPixelSize(19);font.setBold(true);p.setFont(font);p.drawText(QRectF(x,5,size.width()-x-5,25),Qt::AlignVCenter,caption);font.setPixelSize(15);font.setBold(false);p.setFont(font);p.drawText(QRectF(x,30,size.width()-x-5,23),Qt::AlignVCenter,subcaption);}
        if(property("statusDot").isValid()){p.setPen(Qt::NoPen);QRadialGradient glow(11,27,8);glow.setColorAt(0,property("statusDot").value<QColor>().lighter(170));glow.setColorAt(1,property("statusDot").value<QColor>().darker(150));p.setBrush(glow);p.drawEllipse(QRectF(7,23,10,13));}
    }
    else if(!subcaption.isEmpty()&&symbol==DeskIcon::None){font.setPixelSize(35);p.setFont(font);p.drawText(QRectF(0,0,size.width(),size.height()-18),Qt::AlignCenter,caption);font.setPixelSize(15);p.setFont(font);p.drawText(QRectF(0,size.height()-25,size.width(),20),Qt::AlignCenter,subcaption);}
    else {if(symbol!=DeskIcon::None)paintDeskIcon(p,symbol,QRectF((size.width()-28)/2,9,28,28),ink);font.setPixelSize(15);p.setFont(font);p.drawText(QRectF(1,symbol==DeskIcon::None?0:39,size.width()-2,symbol==DeskIcon::None?size.height():size.height()-40),Qt::AlignCenter,caption);}
    if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#92d8ff"),1,Qt::DotLine));p.drawRoundedRect(r.adjusted(4,4,-4,-4),5,5);}
}
namespace {
class LineDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option,const QModelIndex &)const override{const auto s=option.widget?option.widget->width()/237.:1.;return QSize(qRound(237*s),qRound(67*s));}
    void paint(QPainter *p,const QStyleOptionViewItem &option,const QModelIndex &index) const override{
        p->save();p->setRenderHint(QPainter::Antialiasing);const auto s=option.rect.width()/237.;p->translate(option.rect.topLeft());p->scale(s,s);const bool selected=option.state&QStyle::State_Selected;
        QLinearGradient gradient(0,0,0,58);gradient.setColorAt(0,QColor(selected?"#165aa2":"#26384d"));gradient.setColorAt(1,QColor(selected?"#10365e":"#142234"));p->setBrush(gradient);p->setPen(QPen(QColor(selected?"#31c4ff":"#456383"),selected?1.5:1));p->drawRoundedRect(QRectF(1,1,235,57),9,9);
        p->setBrush(QColor("#50e000"));p->setPen(Qt::NoPen);p->drawEllipse(QRectF(14,22,13,14));paintDeskIcon(*p,DeskIcon::Phone,{41,14,28,28});const auto lines=index.data().toString().split('\n');QFont f("Segoe UI");f.setPixelSize(18);f.setBold(true);p->setFont(f);p->setPen(Qt::white);p->drawText(QRectF(87,4,143,25),Qt::AlignVCenter,lines.value(0));f.setBold(false);f.setPixelSize(14);p->setFont(f);p->drawText(QRectF(87,28,143,25),Qt::AlignVCenter,lines.value(1));p->restore();
    }
};
}
DeskPhone::DeskPhone(QWidget *parent):QWidget(parent),chassis_(":/desk/chassis.png"){
    setObjectName("deskPhone");setMinimumSize(1000,750);setMouseTracking(true);
    const auto button=[this](QString text,DeskIcon icon,QRectF rect,DeskButton::Face face=DeskButton::Physical){auto *b=new DeskButton(text,icon,this,face);place(b,rect);return b;};
    settings=button("Settings",DeskIcon::Settings,{1114,38,103,50});settings->caption.clear();settings->setToolTip("Settings");settings->setObjectName("openSettings");
    auto *min=button("Minimize",DeskIcon::Minimize,{1218,38,66,50});min->caption.clear();connect(min,&QPushButton::clicked,this,[this]{window()->showMinimized();});
    auto *max=button("Maximize",DeskIcon::Maximize,{1284,38,61,50});max->caption.clear();connect(max,&QPushButton::clicked,this,[this]{window()->isMaximized()?window()->showNormal():window()->showMaximized();});
    auto *close=button("Close",DeskIcon::Close,{1345,38,64,50});close->caption.clear();connect(close,&QPushButton::clicked,this,[this]{window()->close();});
    pages=new QTabWidget(this);pages->blockSignals(true);pages->setObjectName("mainPages");pages->tabBar()->hide();pages->setStyleSheet("QTabWidget#mainPages::pane {border:0;background:transparent;} QTabWidget#mainPages>QWidget {background:transparent;}");place(pages,{502,168,492,276});
    const QStringList nav={"Phone","Fax","Contacts","Messages","Call History"};const int indices[]={0,2,3,1,4};const DeskIcon icons[]={DeskIcon::Phone,DeskIcon::Fax,DeskIcon::Contacts,DeskIcon::Messages,DeskIcon::History};
    for(int n=0;n<5;++n){auto *b=button(nav[n],icons[n],{297.,210.+46*n,205,44},DeskButton::Navigation);b->setObjectName("deskNav"+QString::number(n));b->horizontal=true;b->setCheckable(true);navigation_[n]=b;connect(b,&QPushButton::clicked,this,[this,n,indices]{pages->setCurrentIndex(indices[n]);navigation_[n]->setChecked(true);});
        auto *led=button(nav[n],DeskIcon::None,{382.+188.5*n,480,95,31});led->caption.clear();led->setToolTip(nav[n]);connect(led,&QPushButton::clicked,b,&QPushButton::click);}
    connect(pages,&QTabWidget::currentChanged,this,[this,indices](int index){for(int n=0;n<5;++n)navigation_[n]->setChecked(index==indices[n]);arrange();update();});
    navigation_[0]->setChecked(true);
    auto *phone=new QWidget;phone->setObjectName("deskCallPage");pages->addTab(phone,"Phone");
    target=new QLineEdit(this);target->setObjectName("dialTarget");target->setPlaceholderText("Number or SIP address");target->setClearButtonEnabled(true);place(target,{632,269,343,37},22);
    account=new QComboBox(this);account->setObjectName("callAccount");place(account,{632,311,343,31},14);
    status=new QLabel(this);status->setObjectName("accountStatus");status->hide();
    calls=new QListWidget(this);calls->setItemDelegate(new LineDelegate(calls));calls->setObjectName("activeCalls");calls->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);calls->setTextElideMode(Qt::ElideRight);place(calls,{1152,162,237,127},16);
    calls->setStyleSheet("QListWidget {background:transparent;border:0;padding:0;color:white;} QListWidget::item {padding:0;margin:0;border:0;background:transparent;}");
    for(int n=0;n<2;++n){idleLines_[n]=button("Line "+QString::number(n+1)+"   ·   Idle",DeskIcon::Phone,{1152.,162.+67*n,237,58},DeskButton::Glass);idleLines_[n]->horizontal=true;idleLines_[n]->caption="Line "+QString::number(n+1);idleLines_[n]->subcaption="Idle";idleLines_[n]->setProperty("statusDot",QColor("#8494a6"));connect(idleLines_[n],&QPushButton::clicked,this,[this]{pages->setCurrentIndex(0);target->setFocus();});}
    calls->raise();
    sip_=button("SIP",DeskIcon::Network,{1152,298,237,58},DeskButton::Glass);sip_->horizontal=true;connect(sip_,&QPushButton::clicked,settings,&QPushButton::click);
    faxStatus_=button("Fax Ready",DeskIcon::Fax,{1152,366,237,59},DeskButton::Glass);faxStatus_->horizontal=true;connect(faxStatus_,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(2);});
    const QString keys="123456789*0#";const QStringList letters={" ","ABC","DEF","GHI","JKL","MNO","PQRS","TUV","WXYZ"," ","+"," "};
    for(int n=0;n<12;++n){auto *b=button(QString(keys[n]),DeskIcon::None,{291.+102*(n%3),567.+76.5*(n/3),89,65});b->setObjectName("deskKey_"+QString(keys[n]));b->subcaption=letters[n];connect(b,&QPushButton::clicked,this,[this,n,keys]{emit digitPressed(keys[n]);});}
    const auto utility=[&](QString text,DeskIcon icon,int x,int y){return button(text,icon,{618.+113*x,581.+94*y,98,68});};
    auto *contacts=utility("Contacts",DeskIcon::Contacts,0,0);connect(contacts,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(3);});
    auto *messages=utility("Messages",DeskIcon::Messages,1,0);connect(messages,&QPushButton::clicked,this,[this]{pages->setCurrentIndex(1);});
    redial=utility("Redial",DeskIcon::Redial,0,1);transfer=utility("Transfer",DeskIcon::Transfer,1,1);hold=utility("Hold",DeskIcon::Hold,0,2);mute=utility("Mute",DeskIcon::Mute,1,2);
    audioCall=button("Call",DeskIcon::Phone,{276,893,174,69});audioCall->caption="Answer";audioCall->horizontal=true;audioCall->setObjectName("deskAnswer");audioCall->setToolTip("Answer an incoming call, or call the entered number");
    hangup=button("Hang Up",DeskIcon::Hangup,{475,893,175,69});hangup->horizontal=true;hangup->setObjectName("deskHangup");
    speaker=button("Speaker",DeskIcon::Speaker,{675,893,156,69});speaker->horizontal=true;speaker->setProperty("darkInk",true);speaker->setCheckable(true);speaker->setObjectName("deskSpeaker");
    const QStringList actions={"Hold","Transfer","Add Call","Conf.","Record","Keypad"};const DeskIcon symbols[]={DeskIcon::Hold,DeskIcon::Transfer,DeskIcon::Add,DeskIcon::Conference,DeskIcon::Record,DeskIcon::Keypad};
    for(int n=0;n<6;++n){auto *b=button(actions[n],symbols[n],{518.+79*n,367,69,66},DeskButton::Glass);b->setObjectName("deskAction"+QString::number(n));if(n==0)connect(b,&QPushButton::clicked,hold,&QPushButton::click);if(n==1)connect(b,&QPushButton::clicked,transfer,&QPushButton::click);if(n==2)addCall=b;if(n==3)conference=b;if(n==4)record=b;if(n==5)openCall=b;}
    openCall->setObjectName("openCallWindow");
    videoCall=button("Video call",DeskIcon::Video,{1007,368,111,65},DeskButton::Glass);videoCall->setObjectName("deskVideoCall");
    detail=new QLabel(this);detail->setObjectName("deskCallDetail");detail->hide();
    auto *timer=new QTimer(this);connect(timer,&QTimer::timeout,this,QOverload<>::of(&DeskPhone::update));timer->start(1000);
    pages->blockSignals(false);
    setStyleSheet("QLineEdit#dialTarget, QComboBox#callAccount {color:#f2f7ff;background:rgba(8,29,55,125);border:1px solid #5786b6;border-radius:5px;padding:3px 8px;selection-background-color:#087fdf;} QComboBox#callAccount QAbstractItemView {background:#122c46;color:white;selection-background-color:#1769ac;}");
}
void DeskPhone::place(QWidget *w,QRectF r,qreal font){if(w->parentWidget()!=this)w->setParent(this);placements_.append({w,r,font});if(auto *b=dynamic_cast<DeskButton*>(w))b->designSize=r.size();}
void DeskPhone::dockFax(QWidget *w){fax_=w;place(w,{866,549,539,410});arrange();}
void DeskPhone::resizeEvent(QResizeEvent *){arrange();}
void DeskPhone::arrange(){
    scale_=qMin(width()/1448.,height()/1086.);offset_={(width()-1448*scale_)/2,(height()-1086*scale_)/2};
    for(const auto &i:placements_){QRectF r=i.rect;if(i.widget==pages&&pages->currentIndex()!=0)r={505,167,623,278};if(i.widget==calls)r.setHeight(qMin(2,callCount_)*67);i.widget->setGeometry(QRect(qRound(offset_.x()+r.x()*scale_),qRound(offset_.y()+r.y()*scale_),qRound(r.width()*scale_),qRound(r.height()*scale_)));if(i.font){auto f=i.widget->font();f.setPixelSize(qMax(9,qRound(i.font*scale_)));i.widget->setFont(f);}}
    target->setVisible(pages->currentIndex()==0&&call_.id.isEmpty());account->setVisible(pages->currentIndex()==0&&call_.id.isEmpty());
    for(auto *b:{addCall,conference,record,openCall})b->setVisible(pages->currentIndex()==0);
    findChild<DeskButton*>("deskAction0")->setVisible(pages->currentIndex()==0);findChild<DeskButton*>("deskAction1")->setVisible(pages->currentIndex()==0);videoCall->setVisible(pages->currentIndex()==0);
}
void DeskPhone::refresh(const CallInfo &call,int count,int registered,int accounts,int unread,int faxCount,bool faxBusy){
    call_=call;callCount_=count;registered_=registered;accounts_=accounts;calls->setVisible(count>0);for(int n=0;n<2;++n)idleLines_[n]->setVisible(n>=qMin(2,count));
    sip_->caption="SIP";sip_->subcaption=registered?"Registered":accounts?"Unregistered":"No accounts";sip_->setProperty("statusDot",QColor(registered?"#46e400":"#8494a6"));sip_->setToolTip(status->text());
    faxStatus_->caption=faxBusy?"Fax in progress":"Fax Ready";faxStatus_->subcaption=QString::number(faxCount)+" received";faxStatus_->setProperty("statusDot",QColor(faxBusy?"#17a7ff":"#46e400"));faxStatus_->setToolTip(QString::number(faxCount)+" received faxes");navigation_[3]->caption=unread?"Messages ("+QString::number(unread)+")":"Messages";
    const bool active=!call.id.isEmpty()&&!callHasEnded(call.rawState);const bool incoming=active&&call.incoming&&(call.rawState==LinphoneCallIncomingReceived||call.rawState==LinphoneCallIncomingEarlyMedia);
    audioCall->setEnabled(incoming||!active);hangup->setEnabled(active);for(auto *b:{hold,mute,transfer,conference,record,openCall,findChild<DeskButton*>("deskAction0"),findChild<DeskButton*>("deskAction1")})b->setEnabled(active);hold->caption=call.rawState==LinphoneCallPaused?"Resume":"Hold";
    mute->setCheckable(true);mute->setChecked(call.muted);record->setCheckable(true);record->setChecked(call.recording);arrange();update();
}
void DeskPhone::paintEvent(QPaintEvent *){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);p.fillRect(rect(),QColor("#dce0e6"));p.translate(offset_);p.scale(scale_,scale_);p.drawPixmap(QRectF(0,0,1448,1086),chassis_,QRectF(chassis_.rect()));
    const auto text=[&](QRectF r,QString value,int size=18,bool bold=false,QColor c=QColor("#eef5ff"),int flags=Qt::AlignLeft|Qt::AlignVCenter){QFont f("Segoe UI");f.setPixelSize(size);f.setBold(bold);p.setFont(f);p.setPen(c);p.drawText(r,flags,value);};
    const auto time=QDateTime::currentDateTime();text({316,118,193,48},time.toString("h:mm AP").section(' ',0,0),46);text({438,132,55,27},time.toString("AP"),22);text({326,167,173,26},time.toString("ddd, MMM d, yyyy"),17);
    p.setPen(QPen(QColor("#eef5ff"),2));p.setBrush(Qt::NoBrush);p.drawArc(QRectF(884,125,19,19),0,180*16);p.drawRoundedRect(QRectF(882,134,5,11),2,2);p.drawRoundedRect(QRectF(902,134,5,11),2,2);p.drawArc(QRectF(891,139,14,8),-110*16,110*16);
    paintDeskIcon(p,DeskIcon::Speaker,{918,124,26,25});for(int n=0;n<4;++n)p.fillRect(QRectF(964+n*6,144-n*5,4,6+n*5),registered_?QColor("#5be700"):QColor("#657281"));
    p.setBrush(registered_?QColor("#3cdb16"):QColor("#7a8897"));p.setPen(Qt::NoPen);p.drawEllipse(QRectF(1002,125,19,19));if(registered_)paintDeskIcon(p,DeskIcon::Check,{1003,126,17,17});text({1027,120,107,29},registered_?"SIP Registered":"SIP Offline",14);
    text({1165,117,219,36},"Lines & Status",21,true);
    if(pages->currentIndex()==0){
        QLinearGradient g(502,168,994,445);g.setColorAt(0,QColor(32,73,121,218));g.setColorAt(1,QColor(8,35,67,210));p.setBrush(g);p.setPen(QPen(QColor("#418ed0"),1));p.drawRoundedRect(QRectF(502,168,492,276),14,14);p.setPen(QColor(139,183,226,70));p.drawLine(516,211,979,211);
        p.setPen(Qt::NoPen);p.setBrush(call_.id.isEmpty()?QColor("#8d9ca9"):QColor("#52eb0e"));p.drawEllipse(QRectF(523,183,13,13));text({546,174,325,33},call_.id.isEmpty()?"Line 1 – Ready":QString("Line %1 – %2").arg(qMax(1,calls->currentRow()+1)).arg(callStateLabel(call_.rawState)),18);
        text({879,174,98,33},QTime(0,0).addSecs(call_.duration).toString("hh:mm:ss"),18,false,QColor("#eef5ff"),Qt::AlignRight|Qt::AlignVCenter);
        QLinearGradient avatar(520,228,615,321);avatar.setColorAt(0,QColor("#487fb8"));avatar.setColorAt(1,QColor("#14446f"));p.setBrush(avatar);p.setPen(QColor("#6cc6ff"));p.drawEllipse(QRectF(522,227,94,94));p.setPen(Qt::NoPen);p.setBrush(QColor("#cee7fb"));p.drawEllipse(QRectF(556,245,27,29));QPainterPath bust;bust.moveTo(546,300);bust.cubicTo(541,271,599,271,594,300);bust.closeSubpath();p.drawPath(bust);
        text({631,225,350,39},call_.id.isEmpty()?"Ready to call":call_.remoteName.isEmpty()?call_.remoteUsername.isEmpty()?call_.remote:call_.remoteUsername:call_.remoteName,29,true);
        if(!call_.id.isEmpty()){text({632,270,343,31},call_.remote,20);text({632,305,343,27},call_.accountName+(call_.audioCodec.isEmpty()?QString():"  ·  "+call_.audioCodec),16);}
    }
}
void DeskPhone::mousePressEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton){resizing_=e->pos().x()>width()-22&&e->pos().y()>height()-22;resizeStart_=e->globalPos();initialSize_=window()->size();drag_=e->globalPos()-window()->frameGeometry().topLeft();}QWidget::mousePressEvent(e);}
void DeskPhone::mouseMoveEvent(QMouseEvent *e){if(e->buttons()&Qt::LeftButton&&!window()->isMaximized()){if(resizing_){const auto delta=e->globalPos()-resizeStart_;window()->resize(initialSize_+QSize(delta.x(),delta.y()));}else window()->move(e->globalPos()-drag_);}else setCursor(e->pos().x()>width()-22&&e->pos().y()>height()-22?Qt::SizeFDiagCursor:Qt::ArrowCursor);}
void DeskPhone::mouseDoubleClickEvent(QMouseEvent *e){if(e->pos().y()<100*scale_){window()->isMaximized()?window()->showNormal():window()->showMaximized();}}
}
