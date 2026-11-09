#include "codecselector.h"
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace stel {
CodecSelector::CodecSelector(const QVector<CodecInfo> &inventory,bool video,bool custom,const QStringList &selected,QWidget *parent)
    :QGroupBox(video?"Video codecs":"Audio codecs",parent),inventory_(inventory),custom_(custom) {
    const QString prefix=video?"video":"audio";
    auto *layout=new QVBoxLayout(this);auto *hint=new QLabel("Tick the codecs to offer. The first checked codec has highest priority.\nBoth endpoints must support the negotiated codec.");hint->setWordWrap(true);layout->addWidget(hint);
    list_=new QListWidget;list_->setObjectName(prefix+"Codecs");list_->setMinimumHeight(165);list_->setMaximumHeight(230);list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);layout->addWidget(list_);
    auto *actions=new QHBoxLayout;auto *up=new QPushButton("Move up");up->setObjectName(prefix+"CodecUp");auto *down=new QPushButton("Move down");down->setObjectName(prefix+"CodecDown");auto *reset=new QPushButton("Reset defaults");reset->setObjectName(prefix+"CodecDefaults");actions->addWidget(up);actions->addWidget(down);actions->addStretch();actions->addWidget(reset);layout->addLayout(actions);
    populate(!custom,selected);
    connect(list_,&QListWidget::itemChanged,this,[this]{custom_=true;});
    auto move=[this](int offset){const int row=list_->currentRow(),to=row+offset;if(row<0||to<0||to>=list_->count())return;const QSignalBlocker block(list_);auto *item=list_->takeItem(row);list_->insertItem(to,item);list_->setCurrentItem(item);custom_=true;};
    connect(up,&QPushButton::clicked,this,[move]{move(-1);});connect(down,&QPushButton::clicked,this,[move]{move(1);});
    connect(reset,&QPushButton::clicked,this,[this]{populate(true);custom_=false;});
    if(video) {
        QStringList missing;for(const auto &mime:{"H265","VP9","AV1"}) {bool found=false;for(const auto &c:inventory)if(c.mime==mime&&c.available)found=true;if(!found)missing<<mime;}
        if(!missing.isEmpty()){auto *label=new QLabel("Unavailable in this build: "+missing.join(", ")+". These need a compatible SDK encoder and decoder.");label->setWordWrap(true);label->setStyleSheet("color:#687b8a;font-size:11px;");layout->addWidget(label);}
    }
}
void CodecSelector::populate(bool defaults,const QStringList &selected) {
    const QSignalBlocker block(list_);list_->clear();auto codecs=inventory_;
    if(defaults)std::stable_sort(codecs.begin(),codecs.end(),[](const CodecInfo &a,const CodecInfo &b){return a.defaultPriority<b.defaultPriority;});
    else if(!selected.isEmpty())std::stable_sort(codecs.begin(),codecs.end(),[&](const CodecInfo &a,const CodecInfo &b){int i=selected.indexOf(a.id),j=selected.indexOf(b.id);return (i<0?selected.size():i)<(j<0?selected.size():j);});
    for(const auto &c:codecs) {
        auto *item=new QListWidgetItem(c.label()+(c.available?QString():"  [receive only / unavailable]"),list_);item->setData(Qt::UserRole,c.id);
        item->setData(Qt::UserRole+1,c.available);item->setToolTip(c.encoder+"\n"+c.fmtp+"\n"+c.id);
        if(c.available){item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState((defaults?c.defaultEnabled:selected.contains(c.id))?Qt::Checked:Qt::Unchecked);}
        else item->setFlags(item->flags()&~Qt::ItemIsEnabled);
    }
    QStringList missing;for(const auto &id:selected){bool found=false;for(const auto &c:codecs)found|=c.id==id;if(!found)missing<<id;}
    if(!defaults&&!missing.isEmpty()){auto *item=new QListWidgetItem("Saved codecs unavailable: "+missing.join(", ")+". Defaults are active; choose replacements.",list_);item->setFlags(Qt::NoItemFlags);item->setToolTip(item->text());}
}
QStringList CodecSelector::selectedIds() const {
    QStringList result;for(int n=0;n<list_->count();++n){const auto *item=list_->item(n);if(item->data(Qt::UserRole+1).toBool()&&item->checkState()==Qt::Checked)result<<item->data(Qt::UserRole).toString();}return result;
}
}
