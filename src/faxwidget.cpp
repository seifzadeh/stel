#include "faxwidget.h"
#include "windowlayout.h"
#include "deskphone.h"
#include <QCheckBox>
#include <QPainter>
#include <QPainterPath>
#include <functional>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTimer>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace stel {
class DeskFaxSurface : public QWidget {
public:
    struct Item { QWidget *widget;QRectF rect;int font; };
    QVector<Item> items;QVector<QWidget*> compose,history;
    QImage thumbnail;int page=0,pages=0;bool showCompose=true;QColor statusColor=QColor("#7b8796");
    explicit DeskFaxSurface(QWidget *parent):QWidget(parent){setObjectName("deskFaxPanel");}
    void place(QWidget *w,QRectF r,int font=14,int group=0){w->setParent(this);w->setMinimumSize(0,0);w->setMaximumSize(QWIDGETSIZE_MAX,QWIDGETSIZE_MAX);items.append({w,r,font});if(group==1)compose<<w;if(group==2)history<<w;if(auto *b=dynamic_cast<DeskButton*>(w))b->designSize=r.size();w->show();}
    void resizeEvent(QResizeEvent *) override{const auto sx=width()/539.,sy=height()/410.;for(const auto &i:items){i.widget->setGeometry(qRound(i.rect.x()*sx),qRound(i.rect.y()*sy),qRound(i.rect.width()*sx),qRound(i.rect.height()*sy));auto f=i.widget->font();f.setPixelSize(qMax(uiPixels(7),qRound(i.font*qMin(sx,sy))));i.widget->setFont(f);if(i.widget->objectName()=="faxSend")if(auto *button=qobject_cast<QPushButton*>(i.widget)){const int icon=qMax(uiPixels(10),qRound(26*qMin(sx,sy)));button->setIconSize(QSize(icon,icon));}}}
    void paintEvent(QPaintEvent *) override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);p.scale(width()/539.,height()/410.);
        QLinearGradient header(0,0,0,58);header.setColorAt(0,QColor("#52575e"));header.setColorAt(1,QColor("#272d33"));p.setBrush(header);p.setPen(QColor("#eceff4"));p.drawRoundedRect(QRectF(0,0,539,58),17,17);
        paintDeskIcon(p,DeskIcon::Fax,{21,13,32,32});QFont f("Segoe UI");f.setPixelSize(28);f.setBold(true);p.setFont(f);p.setPen(Qt::white);p.drawText(QRectF(69,7,82,43),Qt::AlignVCenter,"Fax");
        if(showCompose){
            QLinearGradient metal(0,74,165,318);metal.setColorAt(0,QColor("#8b919b"));metal.setColorAt(.5,QColor("#dfe2e7"));metal.setColorAt(1,QColor("#707780"));p.setBrush(metal);p.setPen(QPen(QColor("#e8edf3"),2));p.drawRoundedRect(QRectF(13,75,163,247),12,12);p.setPen(QColor("#606976"));p.fillRect(QRectF(25,95,138,181),QColor("#f8f9fb"));p.setBrush(Qt::NoBrush);p.drawRect(QRectF(24,94,140,183));
            if(!thumbnail.isNull()){const auto r=QSizeF(thumbnail.size()).scaled(QSizeF(138,181),Qt::KeepAspectRatio);p.drawImage(QRectF(25+(138-r.width())/2,95+(181-r.height())/2,r.width(),r.height()),thumbnail);}
            else {f.setPixelSize(20);p.setFont(f);p.setPen(QColor("#303740"));p.drawText(QRectF(25,106,138,29),Qt::AlignCenter,"FAX");p.setPen(QPen(QColor("#a4aab2"),2));for(int y=150;y<258;y+=8)p.drawLine(42,y,y%3?144:112,y);}
            f.setPixelSize(15);f.setBold(false);p.setFont(f);p.setPen(Qt::white);p.drawText(QRectF(56,282,76,27),Qt::AlignCenter,pages?QString("%1 / %2").arg(page+1).arg(pages):"A4");
            p.setPen(QColor("#17202b"));f.setPixelSize(17);p.setFont(f);p.drawText(QRectF(194,69,322,25),Qt::AlignVCenter,"Fax Number");
        }
        QLinearGradient status(0,345,0,396);status.setColorAt(0,QColor("#dce0e6"));status.setColorAt(1,QColor("#a9afb8"));p.setBrush(status);p.setPen(QColor("#737d89"));p.drawRoundedRect(QRectF(12,344,514,51),9,9);
        f.setPixelSize(17);f.setBold(true);p.setFont(f);p.setPen(QColor("#202833"));p.drawText(QRectF(24,350,136,38),Qt::AlignVCenter,"Fax Status");p.setPen(QPen(Qt::white,1.2));p.setBrush(statusColor);p.drawEllipse(QRectF(171,355,29,29));paintDeskIcon(p,DeskIcon::Check,{174,358,23,23});
    }
};
static void protocols(QComboBox *box){box->addItem("T.38 (UDPTL)","t38");box->addItem("Pass-through · G.711","g711");box->setCurrentIndex(0);}
static void previewFax(QWidget *parent,const QString &path,int pages,bool standard){
    QDialog dialog(parent);dialog.setObjectName("faxPreviewDialog");dialog.setWindowTitle("Fax preview");dialog.resize(800,760);

    auto *layout=new QVBoxLayout(&dialog);auto *navigation=new QHBoxLayout;auto *previous=new QPushButton("Previous");previous->setObjectName("faxPreviewPrevious");auto *next=new QPushButton("Next");next->setObjectName("faxPreviewNext");auto *label=new QLabel;label->setObjectName("faxPreviewPage");auto *zoom=new QPushButton("100%");zoom->setCheckable(true);navigation->addWidget(previous);navigation->addWidget(next);navigation->addWidget(label,1);navigation->addWidget(zoom);layout->addLayout(navigation);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(false);auto *image=new QLabel;image->setObjectName("faxPreviewImage");image->setAlignment(Qt::AlignCenter);scroll->setWidget(image);layout->addWidget(scroll,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    int page=0;QImage current;
    const auto refresh=[&]{QString error;current=faxDocumentPage(path,page,&error);if(standard&&!current.isNull()){const int paper=qRound(210*FaxDpiX/25.4);current=current.copy((current.width()-paper)/2,0,paper,current.height()).scaled(paper,qRound(297*FaxDpiX/25.4),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}label->setText(QString("Page %1 / %2").arg(page+1).arg(pages)+(standard?" · A4 · 210 × 297 mm":""));previous->setEnabled(page>0);next->setEnabled(page+1<pages);
        if(current.isNull()){image->setText(error);image->adjustSize();return;}const int width=zoom->isChecked()?current.width():qMax(200,dialog.width()-65);image->setPixmap(QPixmap::fromImage(current.scaledToWidth(width,Qt::SmoothTransformation)));image->adjustSize();};
    QObject::connect(previous,&QPushButton::clicked,&dialog,[&]{--page;refresh();});QObject::connect(next,&QPushButton::clicked,&dialog,[&]{++page;refresh();});QObject::connect(zoom,&QPushButton::toggled,&dialog,[&]{refresh();});refresh();manageWindowLayout(&dialog,{640,580},{360,280});dialog.exec();
}
FaxWidget::FaxWidget(SipEngine &engine,QWidget *parent,bool desk):QWidget(parent),engine_(engine){
    initializeFaxDocumentFonts();
    setObjectName("faxPage");auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *content=new QWidget;content->setObjectName("faxContent");content->setStyleSheet(uiStyleSheet("QWidget#faxContent { background:#fafcfd; }"));scroll->setWidget(content);outer->addWidget(scroll);
    auto *layout=new QVBoxLayout(content);layout->setContentsMargins(uiPixels(14),uiPixels(14),uiPixels(14),uiPixels(14));layout->setSpacing(uiPixels(12));
    auto *sendBox=new QGroupBox("Send fax");auto *form=new QFormLayout(sendBox);form->setSizeConstraint(QLayout::SetMinimumSize);sendBox->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
    account_=new QComboBox;account_->setObjectName("faxAccount");form->addRow("Account",account_);
    target_=new QLineEdit;target_->setObjectName("faxTarget");target_->setPlaceholderText("Fax number, extension or SIP address");form->addRow("Destination",target_);
    source_=new QComboBox;source_->setObjectName("faxSource");source_->addItem("File · PDF / TXT / image","file");source_->addItem("Type text","text");form->addRow("Source",source_);
    auto *input=new QWidget;auto *inputLayout=new QVBoxLayout(input);inputLayout->setContentsMargins(0,0,0,0);
    fileInput_=new QWidget;auto *fileLayout=new QHBoxLayout(fileInput_);fileLayout->setContentsMargins(0,0,0,0);
    document_=new QLineEdit;document_->setObjectName("faxDocument");document_->setPlaceholderText("PDF, TXT, TIFF, PNG or JPEG");auto *browse=new QPushButton("Browse…");browse->setObjectName("faxBrowse");fileLayout->addWidget(document_,1);fileLayout->addWidget(browse);inputLayout->addWidget(fileInput_);
    textInput_=new QWidget;auto *textLayout=new QHBoxLayout(textInput_);textLayout->setContentsMargins(0,0,0,0);textSummary_=new QLabel("No text yet");textSummary_->setObjectName("faxTextSummary");textSummary_->setTextFormat(Qt::PlainText);textSummary_->setWordWrap(true);auto *edit=new QPushButton("Write / edit text…");edit->setObjectName("faxEditText");textLayout->addWidget(textSummary_,1);textLayout->addWidget(edit);textInput_->hide();inputLayout->addWidget(textInput_);form->addRow("Document",input);
    protocol_=new QComboBox;protocol_->setObjectName("faxProtocol");protocols(protocol_);form->addRow("Protocol for this call",protocol_);
    for(auto *box:{account_,source_,protocol_})box->setMinimumHeight(uiPixels(34));
    auto *hint=new QLabel("Every document is converted to standard A4 fax pages with safe margins. Preview before sending. T.38 uses unencrypted UDPTL and requires PBX support; pass-through uses G.711.");hint->setWordWrap(true);form->addRow(hint);
    auto *actions=new QHBoxLayout;send_=new QPushButton("Send fax");send_->setObjectName("faxSend");preview_=new QPushButton("Preview A4");preview_->setObjectName("faxPreview");cancel_=new QPushButton("Cancel fax");cancel_->setObjectName("faxCancel");cancel_->setEnabled(false);actions->addWidget(send_);actions->addWidget(preview_);actions->addWidget(cancel_);actions->addStretch();form->addRow(actions);
    activity_=new QLabel("Choose a document and destination.");activity_->setObjectName("faxActivity");activity_->setTextFormat(Qt::PlainText);activity_->setWordWrap(true);form->addRow(activity_);layout->addWidget(sendBox);
    auto *receivedBox=new QGroupBox("Received faxes");auto *receivedLayout=new QVBoxLayout(receivedBox);
    received_=new QTableWidget(0,6);received_->setObjectName("receivedFaxes");received_->setHorizontalHeaderLabels({"Date","From","Account","Protocol","Pages","Status"});received_->setEditTriggers(QAbstractItemView::NoEditTriggers);received_->setSelectionBehavior(QAbstractItemView::SelectRows);received_->setSelectionMode(QAbstractItemView::SingleSelection);received_->setAlternatingRowColors(true);received_->verticalHeader()->hide();received_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);received_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);received_->setMinimumHeight(uiPixels(120));receivedLayout->addWidget(received_,1);
    auto *receivedActions=new QHBoxLayout;open_=new QPushButton("Open");open_->setObjectName("faxOpen");save_=new QPushButton("Save as…");save_->setObjectName("faxSaveAs");receivedActions->addWidget(open_);receivedActions->addWidget(save_);receivedActions->addStretch();receivedLayout->addLayout(receivedActions);layout->addWidget(receivedBox,1);
    connect(source_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int n){if(!desk_){fileInput_->setVisible(n==0);textInput_->setVisible(n==1);}});
    connect(edit,&QPushButton::clicked,this,&FaxWidget::editText);
    connect(preview_,&QPushButton::clicked,this,[this]{prepare(false);});
    connect(browse,&QPushButton::clicked,this,[this]{const auto path=QFileDialog::getOpenFileName(this,"Choose fax document",{},"Fax documents (*.pdf *.txt *.tif *.tiff *.png *.jpg *.jpeg);;All files (*)");if(!path.isEmpty())document_->setText(path);});
    connect(send_,&QPushButton::clicked,this,&FaxWidget::send);
    connect(cancel_,&QPushButton::clicked,this,[this]{if(conversion_){conversion_->cancelled=true;activity_->setText("Cancelling document conversion…");return;}for(const auto &f:engine_.faxes())if(f.id==active_&&!f.callId.isEmpty()){engine_.hangup(f.callId);break;}});
    conversionProgress_=new QTimer(this);conversionProgress_->setInterval(100);connect(conversionProgress_,&QTimer::timeout,this,[this]{if(conversion_&&!conversion_->cancelled)activity_->setText(conversion_->total>0?QString("Preparing A4 page %1 / %2…").arg(conversion_->page.load()).arg(conversion_->total.load()):QString("Preparing A4 document…"));});
    connect(&engine_,&SipEngine::faxChanged,this,&FaxWidget::updateFax);
    const auto selection=[this]{const bool exists=QFileInfo::exists(selectedPath());open_->setEnabled(exists);save_->setEnabled(exists);};
    connect(received_,&QTableWidget::itemSelectionChanged,this,selection);
    connect(open_,&QPushButton::clicked,this,[this]{const auto path=selectedPath();for(const auto &fax:engine_.faxes())if(fax.path==path){previewFax(this,path,qMax(1,fax.pages),false);break;}});
    connect(received_,&QTableWidget::cellDoubleClicked,this,[this](int,int){open_->click();});
    connect(save_,&QPushButton::clicked,this,[this]{const auto source=selectedPath();if(source.isEmpty())return;const auto path=QFileDialog::getSaveFileName(this,"Save received fax",QFileInfo(source).fileName(),"TIFF (*.tif *.tiff)");if(path.isEmpty()||QFileInfo(path)==QFileInfo(source))return;
        QFile input(source);QSaveFile output(path);if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)){activity_->setText("Cannot save the fax to this location.");return;}
        while(!input.atEnd()){const auto chunk=input.read(1024*1024);if((chunk.isEmpty()&&input.error()!=QFile::NoError)||output.write(chunk)!=chunk.size()){output.cancelWriting();activity_->setText("Cannot save the fax to this location.");return;}}
        if(!output.commit())activity_->setText("Cannot save the fax to this location.");});
    if(desk){scroll->hide();buildDesk(browse,edit);outer->setContentsMargins(0,0,0,0);outer->addWidget(desk_);}
    refresh();
}
void FaxWidget::buildDesk(QPushButton *browse,QPushButton *edit){
    desk_=new DeskFaxSurface(this);
    desk_->setStyleSheet(uiStyleSheet(R"(QLineEdit,QComboBox {background:#edf0f5;color:#17202b;border:1px solid #788290;border-radius:5px;padding:2px 5px;selection-background-color:#087cdf;}
        QPushButton {background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #f4f5f7,stop:1 #b2b8c1);color:#1d2732;border:1px solid #7d8793;border-radius:5px;padding:1px 5px;}
        QPushButton:hover {border-color:#0083ec;background:#e4f2ff;} QCheckBox {color:#17202b;spacing:6px;}
        QTableWidget {background:#e8ecf2;color:#1b2b3a;border:1px solid #7d8793;gridline-color:#bac3cc;alternate-background-color:#dce2e9;selection-background-color:#167bd0;selection-color:white;}
        QHeaderView::section {background:#c6cdd6;color:#243445;border:0;padding:4px;})"));
    const auto button=[this](QString text,DeskIcon icon,QRectF rect,DeskButton::Face face=DeskButton::Silver,int group=0){auto *b=new DeskButton(text,icon,desk_,face);desk_->place(b,rect,14,group);return b;};
    for(int n=0;n<3;++n){auto *b=button(QStringList{"Send Fax","Inbox (0)","Outbox (0)"}[n],DeskIcon::None,{155.+129*n,10,118,45},DeskButton::Glass);deskTabs_[n]=b;b->setCheckable(true);b->setObjectName("faxDeskTab"+QString::number(n));connect(b,&QPushButton::clicked,this,[this,n]{showDeskPage(n);});}
    desk_->place(target_,{194,101,278,36},18,1);target_->setPlaceholderText("Fax number or SIP address");
    auto *contact=button("Contacts",DeskIcon::Contacts,{479,101,43,36},DeskButton::Silver,1);contact->caption.clear();contact->setToolTip("Choose a fax contact");
    connect(contact,&QPushButton::clicked,this,&FaxWidget::contactRequested);
    desk_->place(account_,{194,144,169,29},13,1);account_->setToolTip("Sending account");desk_->place(protocol_,{370,144,152,29},13,1);protocol_->setToolTip("Protocol for this fax call");
    cover_=new QCheckBox("Include Cover Page");cover_->setObjectName("faxCoverPage");desk_->place(cover_,{194,180,234,31},15,1);
    auto *coverEdit=button("Edit…",DeskIcon::None,{451,181,71,29},DeskButton::Silver,1);coverEdit->setObjectName("faxCoverEdit");
    connect(coverEdit,&QPushButton::clicked,this,[this]{QDialog dialog(this);dialog.setObjectName("faxCoverDialog");dialog.setWindowTitle("Fax cover page");dialog.resize(480,340);auto *layout=new QVBoxLayout(&dialog);auto *text=new QPlainTextEdit;text->setObjectName("faxCoverText");text->setPlainText(coverText_.isEmpty()?QString("FAX\n\nTo: %1\nFrom: \nSubject: \n\n").arg(target_->text()):coverText_);layout->addWidget(text);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);manageWindowLayout(&dialog,{480,340},{320,220});if(dialog.exec()==QDialog::Accepted){coverText_=text->toPlainText();cover_->setChecked(true);}});
    desk_->place(document_,{194,218,278,29},13,1);document_->setPlaceholderText("PDF, TXT, TIFF or image…");desk_->place(browse,{479,218,43,29},13,1);browse->setText("…");browse->setToolTip("Attach a document");
    desk_->place(source_,{194,253,96,25},12,1);source_->setItemText(0,"File");source_->setItemText(1,"Type text");desk_->place(edit,{298,253,102,25},12,1);edit->setText("Write / edit text");desk_->place(preview_,{407,253,115,25},12,1);
    desk_->place(send_,{194,288,328,43},21,1);send_->setText("Send Fax");QPixmap faxIcon(32,32);faxIcon.fill(Qt::transparent);{QPainter painter(&faxIcon);painter.setRenderHint(QPainter::Antialiasing);paintDeskIcon(painter,DeskIcon::Fax,{1,1,30,30});}send_->setIcon(QIcon(faxIcon));send_->setIconSize(QSize(26,26));send_->setStyleSheet(uiStyleSheet("QPushButton {color:white;font-weight:bold;border:2px solid #44baff;border-radius:10px;background:qlineargradient(x1:0,y1:0,x2:0,y2:1,stop:0 #008fff,stop:0.2 #0076e6,stop:0.6 #0059bc,stop:1 #003b80);} QPushButton:disabled {color:#b4c3d4;border-color:#7893af;background:#3a5877;}"));
    auto *previous=button("Previous",DeskIcon::Previous,{21,281,31,31},DeskButton::Physical,1);previous->caption.clear();auto *next=button("Next",DeskIcon::Next,{135,281,31,31},DeskButton::Physical,1);next->caption.clear();connect(previous,&QPushButton::clicked,this,[this]{if(previewPage_>0){--previewPage_;updateDeskPreview();}});connect(next,&QPushButton::clicked,this,[this]{if(previewPage_+1<previewPages_){++previewPage_;updateDeskPreview();}});
    desk_->place(activity_,{215,351,225,35},14);activity_->setText("Fax Ready – Choose a document");desk_->place(cancel_,{447,352,70,34},12);cancel_->setText("Cancel");
    desk_->place(received_,{13,77,511,220},12,2);received_->setHorizontalHeaderLabels({"Date","From","Account","Mode","Pages","Status"});received_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);received_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);
    sent_=new QTableWidget(0,5);sent_->setObjectName("sentFaxes");sent_->setHorizontalHeaderLabels({"Date","To","Mode","Pages","Status"});sent_->setSelectionBehavior(QAbstractItemView::SelectRows);sent_->setSelectionMode(QAbstractItemView::SingleSelection);sent_->setEditTriggers(QAbstractItemView::NoEditTriggers);sent_->verticalHeader()->hide();sent_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);sent_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);desk_->place(sent_,{13,77,511,220},12,2);
    desk_->place(open_,{15,305,100,29},13,2);desk_->place(save_,{124,305,112,29},13,2);
    connect(sent_,&QTableWidget::itemSelectionChanged,this,[this]{const bool exists=QFileInfo::exists(selectedPath());open_->setEnabled(exists);save_->setEnabled(exists);});connect(sent_,&QTableWidget::cellDoubleClicked,this,[this]{open_->click();});
    connect(source_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int n){document_->setEnabled(n==0);document_->setPlaceholderText(n?"Typed text document":"PDF, TXT, TIFF or image…");});
    auto *previewTimer=new QTimer(this);previewTimer->setSingleShot(true);previewTimer->setInterval(700);connect(document_,&QLineEdit::textChanged,this,[previewTimer]{previewTimer->start();});connect(previewTimer,&QTimer::timeout,this,[this]{if(source_->currentData()=="file"&&QFileInfo::exists(document_->text()))prepare(false,true);});
    cancel_->setVisible(false);
    showDeskPage(0);
}
void FaxWidget::showDeskPage(int page){if(!desk_)return;deskPage_=page;desk_->showCompose=page==0;for(auto *w:desk_->compose)w->setVisible(page==0);for(auto *w:desk_->history)w->setVisible(page!=0);received_->setVisible(page==1);sent_->setVisible(page==2);for(int n=0;n<3;++n)deskTabs_[n]->setChecked(n==page);desk_->update();}
void FaxWidget::updateDeskPreview(){if(!desk_)return;desk_->thumbnail=faxDocumentPage(previewPath_,previewPage_);desk_->page=previewPage_;desk_->pages=previewPages_;desk_->update();}
void FaxWidget::setDestination(const QString &target){showDeskPage(0);target_->setText(target);target_->setFocus();}
FaxWidget::~FaxWidget(){if(conversion_)conversion_->cancelled=true;}
void FaxWidget::setAccounts(const QVector<Account> &accounts){const auto selected=account_->currentData();const QSignalBlocker block(account_);account_->clear();for(const auto &a:accounts)if(a.enabled)account_->addItem(a.label.isEmpty()?a.username+"@"+a.domain:a.label,a.id);const int index=account_->findData(selected);account_->setCurrentIndex(index<0?0:index);send_->setEnabled(account_->count()>0&&!conversion_);if(desk_)desk_->statusColor=QColor(account_->count()?"#36bf32":"#7b8796");refresh();}
QString FaxWidget::selectedPath() const {auto *table=deskPage_==2&&sent_?sent_:received_;const auto *item=table->item(table->currentRow(),0);return item?item->data(Qt::UserRole+1).toString():QString();}
void FaxWidget::send(){
    if(target_->text().trimmed().isEmpty()){activity_->setText("Enter a fax destination.");return;}prepare(true);
}
void FaxWidget::editText(){
    QDialog dialog(this);dialog.setObjectName("faxTextDialog");dialog.setWindowTitle("Write fax text");dialog.resize(540,380);auto *layout=new QVBoxLayout(&dialog);auto *hint=new QLabel("Plain text · automatic A4 pagination · Persian and English");layout->addWidget(hint);auto *editor=new QPlainTextEdit;editor->setObjectName("faxTextInput");editor->setLayoutDirection(Qt::LeftToRight);editor->setPlainText(textDraft_);editor->setPlaceholderText("Type the text to fax…");layout->addWidget(editor,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Use text");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    manageWindowLayout(&dialog,{540,380},{340,240});if(dialog.exec()==QDialog::Accepted){textDraft_=editor->toPlainText();textSummary_->setText(textDraft_.isEmpty()?QString("No text yet"):QString::number(textDraft_.size())+" characters · "+textDraft_.simplified().left(60));}
}
void FaxWidget::prepare(bool sendAfter,bool inlineOnly){
    if(conversion_)return;
    const bool typed=source_->currentData()=="text";
    if(typed&&textDraft_.trimmed().isEmpty()){editText();if(textDraft_.trimmed().isEmpty())return;}
    if(!typed&&document_->text().trimmed().isEmpty()){activity_->setText("Choose a document to fax.");return;}
    auto temporary=std::make_shared<QTemporaryDir>();if(!temporary->isValid()){activity_->setText("Cannot create a temporary document.");return;}
    const auto output=temporary->filePath("a4-fax.tif"),file=document_->text().trimmed(),text=textDraft_,account=account_->currentData().toString(),target=target_->text().trimmed(),protocol=protocol_->currentData().toString();
    conversion_=std::make_shared<FaxConversionControl>();const auto control=conversion_;const bool includeCover=cover_&&cover_->isChecked();const QString coverText=coverText_.isEmpty()?QString("FAX\n\nTo: %1\nDate: %2\n\n").arg(target,QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm")):coverText_;send_->setEnabled(false);preview_->setEnabled(false);cancel_->setEnabled(true);if(desk_){cancel_->show();desk_->statusColor=QColor("#0789e5");desk_->update();}conversionProgress_->start();activity_->setText("Preparing A4 document…");
    auto *watcher=new QFutureWatcher<FaxDocumentResult>(this);connect(watcher,&QFutureWatcher<FaxDocumentResult>::finished,this,[this,watcher,temporary,output,account,target,protocol,sendAfter,inlineOnly,control]{
        const auto result=watcher->result();watcher->deleteLater();conversionProgress_->stop();conversion_.reset();send_->setEnabled(account_->count()>0);preview_->setEnabled(true);cancel_->setEnabled(false);if(desk_)cancel_->hide();
        if(control->cancelled){activity_->setText("Conversion cancelled.");return;}
        if(!result.ok){activity_->setText(result.error);activity_->setToolTip(result.error);if(desk_){desk_->statusColor=QColor("#cd3d45");desk_->update();}return;}
        activity_->setText(QString("%1 A4 page(s) ready · 210 × 297 mm").arg(result.pages));
        if(desk_)desk_->statusColor=QColor("#36bf32");
        previewDirectory_=temporary;previewPath_=output;previewPage_=0;previewPages_=result.pages;updateDeskPreview();
        if(sendAfter){if(!engine_.sendFax(account,target,output,protocol))activity_->setText(engine_.lastError());}
        else if(!inlineOnly)previewFax(this,output,result.pages,true);
    });
    watcher->setFuture(QtConcurrent::run([temporary,output,file,text,typed,control,includeCover,coverText]{
        const auto body=includeCover?temporary->filePath("body.tif"):output;
        auto result=typed?convertFaxText(text,body,control):convertFaxDocument(file,body,control);
        if(!result.ok||!includeCover)return result;
        const auto cover=temporary->filePath("cover.tif");result=convertFaxText(coverText,cover,control);if(!result.ok)return result;
        return joinFaxDocuments({cover,body},output,control);
    }));
}
void FaxWidget::updateFax(const FaxInfo &fax){
    if(conversion_){refresh();return;}
    if(!fax.incoming)active_=fax.id;
    if(fax.id==active_||fax.incoming){const bool active=fax.state=="dialing"||fax.state=="negotiating"||fax.state=="sending"||fax.state=="receiving";cancel_->setEnabled(active&&!fax.callId.isEmpty());if(desk_)cancel_->setVisible(active);if(fax.incoming&&active)active_=fax.id;
        activity_->setText((fax.incoming?"Receive":"Send")+QString(" · ")+fax.remote+" · "+(fax.protocol=="t38"?"T.38":"G.711")+" · "+fax.state+" · "+QString::number(fax.pages)+" pages"+(fax.detail.isEmpty()?QString():"\n"+fax.detail));}
    activity_->setToolTip(activity_->text());
    if(desk_){const auto direction=fax.incoming?QString("Receive"):QString("Send");const auto mode=fax.protocol=="t38"?QString("T.38"):QString("G.711");
        if(fax.state=="completed")activity_->setText((fax.incoming?QString("Received"):QString("Sent"))+" · "+QString::number(fax.pages)+" pages\n"+mode+" · Completed");
        else if(fax.state=="failed")activity_->setText("Fax failed\n"+fax.detail.left(26));
        else activity_->setText(direction+" · "+mode+"\n"+fax.state+" · "+QString::number(fax.pages)+" pages");
    }
    if(desk_){desk_->statusColor=QColor(fax.state=="failed"?"#cd3d45":fax.state=="completed"?"#36bf32":"#0789e5");desk_->update();}
    refresh();
}
void FaxWidget::refresh(){
    const auto path=selectedPath();received_->setRowCount(0);
    for(const auto &f:engine_.faxes())if(f.incoming){const int row=received_->rowCount();received_->insertRow(row);QString account=f.accountId;const int accountIndex=account_->findData(f.accountId);if(accountIndex>=0)account=account_->itemText(accountIndex);
        const QStringList values={f.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm"),f.remote,account,f.protocol=="t38"?"T.38":"G.711",QString::number(f.pages),f.state};
        for(int c=0;c<values.size();++c){auto *item=new QTableWidgetItem(values[c]);item->setToolTip(f.detail);received_->setItem(row,c,item);}
        received_->item(row,0)->setData(Qt::UserRole,f.id);received_->item(row,0)->setData(Qt::UserRole+1,f.path);if(f.path==path)received_->selectRow(row);
    }
    if(sent_){sent_->setRowCount(0);for(const auto &f:engine_.faxes())if(!f.incoming){const int row=sent_->rowCount();sent_->insertRow(row);const QStringList values={f.timestamp.toLocalTime().toString("MM-dd HH:mm"),f.remote,f.protocol=="t38"?"T.38":"G.711",QString::number(f.pages),f.state};for(int c=0;c<values.size();++c)sent_->setItem(row,c,new QTableWidgetItem(values[c]));sent_->item(row,0)->setData(Qt::UserRole+1,f.path);if(f.path==path)sent_->selectRow(row);}
        deskTabs_[1]->caption="Inbox ("+QString::number(received_->rowCount())+")";deskTabs_[2]->caption="Outbox ("+QString::number(sent_->rowCount())+")";deskTabs_[1]->update();deskTabs_[2]->update();}
    const bool exists=QFileInfo::exists(selectedPath());open_->setEnabled(exists);save_->setEnabled(exists);
}
bool FaxWidget::receiveCall(const CallInfo &call){
    QDialog dialog(this);dialog.setObjectName("receiveFaxDialog");dialog.setWindowTitle("Receive fax");auto *layout=new QVBoxLayout(&dialog);auto *identity=new QLabel(call.remoteName.isEmpty()?call.remote:call.remoteName+"\n"+call.remote);identity->setTextFormat(Qt::PlainText);layout->addWidget(identity);
    auto *protocol=new QComboBox;protocol->setObjectName("receiveFaxProtocol");protocols(protocol);layout->addWidget(protocol);auto *hint=new QLabel("T.38 is the default. Select G.711 for a caller sending fax tones over audio.");hint->setWordWrap(true);layout->addWidget(hint);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Receive fax");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()!=QDialog::Accepted)return false;
    if(!engine_.receiveFax(call.id,protocol->currentData().toString())){QMessageBox::warning(this,"Cannot receive fax",engine_.lastError());return false;}return true;
}
}
