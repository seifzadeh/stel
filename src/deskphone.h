#pragma once
#include "sipengine.h"
#include <QPushButton>
#include <QWidget>
#include <QPixmap>
class QTabWidget; class QComboBox; class QLineEdit; class QListWidget; class QLabel; class QPainter;
namespace stel {
class InlineCallHost;
class VideoView;
enum class DeskIcon { None, Phone, Hangup, Fax, Contacts, Messages, History, Hold, Transfer, Add, Conference, Record, Keypad, Mute, Speaker, Settings, Minimize, Maximize, Close, Redial, Video, Previous, Next, Attach, Check, Network };
void paintDeskIcon(QPainter &,DeskIcon,const QRectF &,const QColor &color=Qt::white);
class DeskButton : public QPushButton {
    Q_OBJECT
public:
    enum Face { Physical, Navigation, Glass, Silver };
    DeskButton(const QString &,DeskIcon,QWidget *,Face=Physical);
    QString caption,subcaption;
    DeskIcon symbol;
    Face face;
    QSizeF designSize;
    bool horizontal=false;
protected:
    void paintEvent(QPaintEvent *) override;
};
// All coordinates refer to the reference's 1448 × 1086 canvas. Native widgets
// are laid out at one uniform scale; there is no screen-sized screenshot UI.
class DeskPhone : public QWidget {
    Q_OBJECT
public:
    explicit DeskPhone(QWidget *parent=nullptr);
    QTabWidget *pages;
    QComboBox *account;
    QLineEdit *target;
    QListWidget *calls;
    QLabel *detail,*status;
    DeskButton *settings,*audioCall,*videoCall,*hangup,*hold,*mute,*transfer,*conference,*record,*addCall,*redial,*speaker,*openCall;
    DeskButton *forward,*attended,*receiveFax;
    InlineCallHost *callHost;
    VideoView *remoteVideo,*previewVideo;
    QLabel *callMessage,*dtmfDisplay;
    void dockFax(QWidget *);
    void refresh(const CallInfo &,int callCount,int registered,int accounts,int unread,int faxCount,bool faxBusy);
    void place(QWidget *,QRectF,qreal fontSize=0);
    void setExpansionKeys(const QVector<ExpansionKey> &);
    void setExpansionState(const BlfInfo &);
    qreal scale() const{return scale_;}
signals:
    void digitPressed(QChar);
    void expansionPressed(int key);
    void expansionConfigureRequested(int key);
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
private:
    struct Placement { QWidget *widget; QRectF rect; qreal font; };
    QVector<Placement> placements_;
    QPixmap chassis_;
    qreal scale_=1;
    QPointF offset_;
    QPoint drag_;
    QPoint resizeStart_;
    QSize initialSize_;
    bool resizing_=false;
    DeskButton *navigation_[5],*idleLines_[2],*sip_,*faxStatus_;
    QPushButton *expansion_[ExpansionKeyCount];
    QLabel *videoCaption_;
    CallInfo call_;
    int registered_=0,accounts_=0,callCount_=0;
    QWidget *fax_=nullptr;
    void arrange();
};
}
