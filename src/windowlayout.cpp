#include "windowlayout.h"
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QFont>
#include <QLayout>
#include <QMargins>
#include <QPointer>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QWindow>
namespace stel {
int uiPixels(int pixels){return pixels<=0?pixels:qMax(1,qRound(pixels*UiScale));}
QSize uiSize(QSize size){return {uiPixels(size.width()),uiPixels(size.height())};}
QString uiStyleSheet(const QString &style){
    static const QRegularExpression length(QStringLiteral("(-?\\d+(?:\\.\\d+)?)px\\b"));
    QString result;int offset=0;auto matches=length.globalMatch(style);
    while(matches.hasNext()){
        const auto match=matches.next();result+=style.mid(offset,match.capturedStart()-offset);
        result+=QString::number(match.captured(1).toDouble()*UiScale,'g',10)+"px";offset=match.capturedEnd();
    }
    return result+style.mid(offset);
}
namespace {
class CompactStyle final : public QProxyStyle {
public:
    int pixelMetric(PixelMetric metric,const QStyleOption *option=nullptr,const QWidget *widget=nullptr) const override{
        return uiPixels(QProxyStyle::pixelMetric(metric,option,widget));
    }
};
}
void configureUiDpi(){
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    // Qt 5's default rounding can turn Windows 150/175% into a 200% UI.
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}
void compactApplicationFont(QApplication &app){
    if(app.property("stelCompactUi").toBool())return;
    app.setProperty("stelCompactUi",true);app.setStyle(new CompactStyle);
    auto font=app.font();font.setPixelSize(uiPixels(12));app.setFont(font);
}
QSize boundedWindowSize(QSize requested,QSize available,bool keepAspect){
    available=available.expandedTo(QSize(1,1));requested=requested.expandedTo(QSize(1,1));
    if(requested.width()<=available.width()&&requested.height()<=available.height())return requested;
    return keepAspect?requested.scaled(available,Qt::KeepAspectRatio):requested.boundedTo(available);
}
namespace {
class WindowLayout final : public QObject {
public:
    WindowLayout(QWidget *window,QSize preferred,QSize minimum,bool aspect):QObject(window),window_(window),minimum_(minimum),aspect_(aspect){
        if(auto *layout=window->layout())if(qobject_cast<QDialog*>(window))layout->setSizeConstraint(QLayout::SetNoConstraint);
        window->setMinimumSize(0,0);window->resize(preferred);window->installEventFilter(this);fit(true);
    }
protected:
    bool eventFilter(QObject *,QEvent *event) override{
        if(event->type()==QEvent::Show){connectScreen();schedule();}
        else if(event->type()==QEvent::Move){
            // Let a user drag across monitor boundaries. Recover a window only
            // when it has been moved completely outside every attached screen.
            bool onScreen=false;for(auto *screen:QGuiApplication::screens())onScreen|=screen->availableGeometry().intersects(window_->frameGeometry());if(!onScreen)schedule();
        }else if(event->type()==QEvent::Resize||event->type()==QEvent::WindowStateChange)schedule();
        return false;
    }
private:
    QWidget *window_;QSize minimum_;bool aspect_,queued_=false;
    QPointer<QWindow> handle_;QPointer<QScreen> screen_;
    QScreen *currentScreen() const{
        if(window_->isVisible()&&window_->windowHandle()&&window_->windowHandle()->screen())return window_->windowHandle()->screen();
        if(auto *parent=window_->parentWidget()){auto *owner=parent->window();if(owner->windowHandle()&&owner->windowHandle()->screen())return owner->windowHandle()->screen();}
        return QGuiApplication::primaryScreen();
    }
    void connectScreen(){
        if(auto *handle=window_->windowHandle())if(handle_!=handle){handle_=handle;connect(handle,&QWindow::screenChanged,this,[this]{connectScreen();schedule();});}
        auto *screen=currentScreen();if(screen==screen_)return;
        if(screen_)disconnect(screen_,nullptr,this,nullptr);
        screen_=screen;
        if(screen){connect(screen,&QScreen::availableGeometryChanged,this,[this]{schedule();});connect(screen,&QScreen::logicalDotsPerInchChanged,this,[this]{schedule();});}
    }
    void schedule(){if(queued_)return;queued_=true;QTimer::singleShot(0,this,[this]{queued_=false;fit(false);});}
    void fit(bool centre){
        auto *screen=currentScreen();if(!screen||window_->isMinimized()||window_->isMaximized()||window_->isFullScreen())return;
        const auto work=screen->availableGeometry().adjusted(10,10,-10,-10);QMargins frame;
        if(window_->windowHandle())frame=window_->windowHandle()->frameMargins();
        if(frame.isNull()&&!window_->windowFlags().testFlag(Qt::FramelessWindowHint))frame=QMargins(6,30,6,6);
        const QSize room(qMax(1,work.width()-frame.left()-frame.right()),qMax(1,work.height()-frame.top()-frame.bottom()));
        window_->setMinimumSize(boundedWindowSize(minimum_.expandedTo(QSize(1,1)),room,aspect_));
        const auto size=boundedWindowSize(window_->size(),room,aspect_);if(size!=window_->size())window_->resize(size);
        if(centre){const auto owner=window_->parentWidget()?window_->parentWidget()->window()->frameGeometry().center():work.center();window_->move(owner-QPoint((size.width()+frame.left()+frame.right())/2,(size.height()+frame.top()+frame.bottom())/2));}
        const auto outer=window_->frameGeometry();QPoint shift;
        if(outer.left()<work.left())shift.setX(work.left()-outer.left());else if(outer.right()>work.right())shift.setX(work.right()-outer.right());
        if(outer.top()<work.top())shift.setY(work.top()-outer.top());else if(outer.bottom()>work.bottom())shift.setY(work.bottom()-outer.bottom());
        if(!shift.isNull())window_->move(window_->pos()+shift);
    }
};
}
void manageWindowLayout(QWidget *window,QSize preferred,QSize minimum,bool keepAspect){new WindowLayout(window,uiSize(preferred),uiSize(minimum),keepAspect);}
}
