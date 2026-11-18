#include "windowlayout.h"
#include "inlinecallhost.h"
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QScrollArea>
#include <QVBoxLayout>
namespace stel {
InlineCallHost::InlineCallHost(QWidget *parent):QFrame(parent) {
    setObjectName("inlineCallHost");auto *layout=new QVBoxLayout(this);layout->setContentsMargins(uiPixels(10),uiPixels(6),uiPixels(10),uiPixels(7));layout->setSpacing(uiPixels(3));
    auto *header=new QHBoxLayout;title_=new QLabel;title_->setTextFormat(Qt::PlainText);title_->setMinimumWidth(0);title_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);header->addWidget(title_,1);
    auto *window=new QPushButton("Expand video");window_=window;window->setObjectName("expandCallWindow");header->addWidget(window);
    auto *back=new QPushButton("Back");back->setObjectName("inlineCallBack");header->addWidget(back);layout->addLayout(header);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setStyleSheet(uiStyleSheet("QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;border:0;}"));
    contentWidget_=new QWidget;content_=new QVBoxLayout(contentWidget_);content_->setContentsMargins(0,0,0,0);scroll->setWidget(contentWidget_);layout->addWidget(scroll,1);
    connect(back,&QPushButton::clicked,this,&InlineCallHost::backRequested);
    connect(window,&QPushButton::clicked,this,&InlineCallHost::windowRequested);
    auto *escape=new QShortcut(QKeySequence(Qt::Key_Escape),this);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,&InlineCallHost::backRequested);
    setStyleSheet(uiStyleSheet("QFrame#inlineCallHost{background:#14385e;border:1px solid #639bc9;border-radius:9px;} QFrame#inlineCallHost QLabel,QFrame#inlineCallHost QGroupBox{color:#edf7ff;font-size:11px;} QFrame#inlineCallHost QGroupBox{border:0;margin-top:16px;padding-top:4px;} QFrame#inlineCallHost QGroupBox::title{color:#c6e8ff;} QFrame#inlineCallHost QLineEdit,QFrame#inlineCallHost QComboBox{color:#18314b;background:#f0f5fa;border:1px solid #87afce;border-radius:4px;padding:4px;min-height:18px;font-size:11px;} QFrame#inlineCallHost QPushButton{color:white;background:#24568a;border:1px solid #689ac8;border-radius:4px;padding:2px 6px;min-height:18px;font-size:12px;} QFrame#inlineCallHost QPushButton:disabled{color:#8da7c0;background:#1a3d62;}"));
    hide();
}
void InlineCallHost::present(QWidget *panel,const QString &title) {
    if(current_&&current_!=panel){current_->hide();content_->removeWidget(current_);}
    current_=panel;title_->setText(title);panel->setParent(contentWidget_);content_->addWidget(panel);panel->show();show();raise();
}
void InlineCallHost::dismiss(){if(current_){current_->hide();content_->removeWidget(current_);}current_.clear();hide();}
void InlineCallHost::setExpansionAvailable(bool enabled){window_->setVisible(enabled);}
}
