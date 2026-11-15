#include "faxwidget.h"
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
static void protocols(QComboBox *box){box->addItem("T.38 (UDPTL)","t38");box->addItem("Pass-through · G.711","g711");box->setCurrentIndex(0);}
static void previewFax(QWidget *parent,const QString &path,int pages,bool standard){
    QDialog dialog(parent);dialog.setObjectName("faxPreviewDialog");dialog.setWindowTitle("Fax preview");dialog.resize(800,760);
    if(auto *screen=QGuiApplication::primaryScreen())dialog.resize(qMin(850,screen->availableGeometry().width()-80),qMin(820,screen->availableGeometry().height()-100));
    auto *layout=new QVBoxLayout(&dialog);auto *navigation=new QHBoxLayout;auto *previous=new QPushButton("Previous");previous->setObjectName("faxPreviewPrevious");auto *next=new QPushButton("Next");next->setObjectName("faxPreviewNext");auto *label=new QLabel;label->setObjectName("faxPreviewPage");auto *zoom=new QPushButton("100%");zoom->setCheckable(true);navigation->addWidget(previous);navigation->addWidget(next);navigation->addWidget(label,1);navigation->addWidget(zoom);layout->addLayout(navigation);
    auto *scroll=new QScrollArea;scroll->setWidgetResizable(false);auto *image=new QLabel;image->setObjectName("faxPreviewImage");image->setAlignment(Qt::AlignCenter);scroll->setWidget(image);layout->addWidget(scroll,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Close);layout->addWidget(buttons);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    int page=0;QImage current;
    const auto refresh=[&]{QString error;current=faxDocumentPage(path,page,&error);if(standard&&!current.isNull()){const int paper=qRound(210*FaxDpiX/25.4);current=current.copy((current.width()-paper)/2,0,paper,current.height()).scaled(paper,qRound(297*FaxDpiX/25.4),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);}label->setText(QString("Page %1 / %2").arg(page+1).arg(pages)+(standard?" · A4 · 210 × 297 mm":""));previous->setEnabled(page>0);next->setEnabled(page+1<pages);
        if(current.isNull()){image->setText(error);image->adjustSize();return;}const int width=zoom->isChecked()?current.width():qMax(200,dialog.width()-65);image->setPixmap(QPixmap::fromImage(current.scaledToWidth(width,Qt::SmoothTransformation)));image->adjustSize();};
    QObject::connect(previous,&QPushButton::clicked,&dialog,[&]{--page;refresh();});QObject::connect(next,&QPushButton::clicked,&dialog,[&]{++page;refresh();});QObject::connect(zoom,&QPushButton::toggled,&dialog,[&]{refresh();});refresh();dialog.exec();
}
FaxWidget::FaxWidget(SipEngine &engine,QWidget *parent):QWidget(parent),engine_(engine){
    initializeFaxDocumentFonts();
    setObjectName("faxPage");auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);auto *scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);auto *content=new QWidget;content->setObjectName("faxContent");content->setStyleSheet("QWidget#faxContent { background:#fafcfd; }");scroll->setWidget(content);outer->addWidget(scroll);
    auto *layout=new QVBoxLayout(content);layout->setContentsMargins(14,14,14,14);layout->setSpacing(12);
    auto *sendBox=new QGroupBox("Send fax");auto *form=new QFormLayout(sendBox);form->setSizeConstraint(QLayout::SetMinimumSize);sendBox->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Minimum);
    account_=new QComboBox;account_->setObjectName("faxAccount");form->addRow("Account",account_);
    target_=new QLineEdit;target_->setObjectName("faxTarget");target_->setPlaceholderText("Fax number, extension or SIP address");form->addRow("Destination",target_);
    source_=new QComboBox;source_->setObjectName("faxSource");source_->addItem("File · PDF / TXT / image","file");source_->addItem("Type text","text");form->addRow("Source",source_);
    auto *input=new QWidget;auto *inputLayout=new QVBoxLayout(input);inputLayout->setContentsMargins(0,0,0,0);
    fileInput_=new QWidget;auto *fileLayout=new QHBoxLayout(fileInput_);fileLayout->setContentsMargins(0,0,0,0);
    document_=new QLineEdit;document_->setObjectName("faxDocument");document_->setPlaceholderText("PDF, TXT, TIFF, PNG or JPEG");auto *browse=new QPushButton("Browse…");browse->setObjectName("faxBrowse");fileLayout->addWidget(document_,1);fileLayout->addWidget(browse);inputLayout->addWidget(fileInput_);
    textInput_=new QWidget;auto *textLayout=new QHBoxLayout(textInput_);textLayout->setContentsMargins(0,0,0,0);textSummary_=new QLabel("No text yet");textSummary_->setObjectName("faxTextSummary");textSummary_->setTextFormat(Qt::PlainText);textSummary_->setWordWrap(true);auto *edit=new QPushButton("Write / edit text…");edit->setObjectName("faxEditText");textLayout->addWidget(textSummary_,1);textLayout->addWidget(edit);textInput_->hide();inputLayout->addWidget(textInput_);form->addRow("Document",input);
    protocol_=new QComboBox;protocol_->setObjectName("faxProtocol");protocols(protocol_);form->addRow("Protocol for this call",protocol_);
    for(auto *box:{account_,source_,protocol_})box->setMinimumHeight(34);
    auto *hint=new QLabel("Every document is converted to standard A4 fax pages with safe margins. Preview before sending. T.38 uses unencrypted UDPTL and requires PBX support; pass-through uses G.711.");hint->setWordWrap(true);form->addRow(hint);
    auto *actions=new QHBoxLayout;send_=new QPushButton("Send fax");send_->setObjectName("faxSend");preview_=new QPushButton("Preview A4");preview_->setObjectName("faxPreview");cancel_=new QPushButton("Cancel fax");cancel_->setObjectName("faxCancel");cancel_->setEnabled(false);actions->addWidget(send_);actions->addWidget(preview_);actions->addWidget(cancel_);actions->addStretch();form->addRow(actions);
    activity_=new QLabel("Choose a document and destination.");activity_->setObjectName("faxActivity");activity_->setTextFormat(Qt::PlainText);activity_->setWordWrap(true);form->addRow(activity_);layout->addWidget(sendBox);
    auto *receivedBox=new QGroupBox("Received faxes");auto *receivedLayout=new QVBoxLayout(receivedBox);
    received_=new QTableWidget(0,6);received_->setObjectName("receivedFaxes");received_->setHorizontalHeaderLabels({"Date","From","Account","Protocol","Pages","Status"});received_->setEditTriggers(QAbstractItemView::NoEditTriggers);received_->setSelectionBehavior(QAbstractItemView::SelectRows);received_->setSelectionMode(QAbstractItemView::SingleSelection);received_->setAlternatingRowColors(true);received_->verticalHeader()->hide();received_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);received_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch);received_->setMinimumHeight(120);receivedLayout->addWidget(received_,1);
    auto *receivedActions=new QHBoxLayout;open_=new QPushButton("Open");open_->setObjectName("faxOpen");save_=new QPushButton("Save as…");save_->setObjectName("faxSaveAs");receivedActions->addWidget(open_);receivedActions->addWidget(save_);receivedActions->addStretch();receivedLayout->addLayout(receivedActions);layout->addWidget(receivedBox,1);
    connect(source_,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int n){fileInput_->setVisible(n==0);textInput_->setVisible(n==1);});
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
    refresh();
}
FaxWidget::~FaxWidget(){if(conversion_)conversion_->cancelled=true;}
void FaxWidget::setAccounts(const QVector<Account> &accounts){const auto selected=account_->currentData();const QSignalBlocker block(account_);account_->clear();for(const auto &a:accounts)if(a.enabled)account_->addItem(a.label.isEmpty()?a.username+"@"+a.domain:a.label,a.id);const int index=account_->findData(selected);account_->setCurrentIndex(index<0?0:index);send_->setEnabled(account_->count()>0&&!conversion_);refresh();}
QString FaxWidget::selectedPath() const {const auto *item=received_->item(received_->currentRow(),0);return item?item->data(Qt::UserRole+1).toString():QString();}
void FaxWidget::send(){
    if(target_->text().trimmed().isEmpty()){activity_->setText("Enter a fax destination.");return;}prepare(true);
}
void FaxWidget::editText(){
    QDialog dialog(this);dialog.setObjectName("faxTextDialog");dialog.setWindowTitle("Write fax text");dialog.resize(650,460);auto *layout=new QVBoxLayout(&dialog);auto *hint=new QLabel("Plain text · automatic A4 pagination · Persian and English");layout->addWidget(hint);auto *editor=new QPlainTextEdit;editor->setObjectName("faxTextInput");editor->setLayoutDirection(Qt::LeftToRight);editor->setPlainText(textDraft_);editor->setPlaceholderText("Type the text to fax…");layout->addWidget(editor,1);auto *buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);buttons->button(QDialogButtonBox::Ok)->setText("Use text");layout->addWidget(buttons);connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if(dialog.exec()==QDialog::Accepted){textDraft_=editor->toPlainText();textSummary_->setText(textDraft_.isEmpty()?QString("No text yet"):QString::number(textDraft_.size())+" characters · "+textDraft_.simplified().left(60));}
}
void FaxWidget::prepare(bool sendAfter){
    if(conversion_)return;const bool typed=source_->currentData()=="text";
    if(typed&&textDraft_.trimmed().isEmpty()){editText();if(textDraft_.trimmed().isEmpty())return;}
    if(!typed&&document_->text().trimmed().isEmpty()){activity_->setText("Choose a document to fax.");return;}
    auto temporary=std::make_shared<QTemporaryDir>();if(!temporary->isValid()){activity_->setText("Cannot create a temporary document.");return;}
    const auto output=temporary->filePath("a4-fax.tif"),file=document_->text().trimmed(),text=textDraft_,account=account_->currentData().toString(),target=target_->text().trimmed(),protocol=protocol_->currentData().toString();
    conversion_=std::make_shared<FaxConversionControl>();const auto control=conversion_;send_->setEnabled(false);preview_->setEnabled(false);cancel_->setEnabled(true);conversionProgress_->start();activity_->setText("Preparing A4 document…");
    auto *watcher=new QFutureWatcher<FaxDocumentResult>(this);connect(watcher,&QFutureWatcher<FaxDocumentResult>::finished,this,[this,watcher,temporary,output,account,target,protocol,sendAfter,control]{
        const auto result=watcher->result();watcher->deleteLater();conversionProgress_->stop();conversion_.reset();send_->setEnabled(account_->count()>0);preview_->setEnabled(true);cancel_->setEnabled(false);
        if(control->cancelled){activity_->setText("Conversion cancelled.");return;}
        if(!result.ok){activity_->setText(result.error);return;}
        activity_->setText(QString("%1 A4 page(s) ready · 210 × 297 mm").arg(result.pages));
        if(sendAfter){if(!engine_.sendFax(account,target,output,protocol))activity_->setText(engine_.lastError());}
        else previewFax(this,output,result.pages,true);
    });
    watcher->setFuture(QtConcurrent::run([temporary,output,file,text,typed,control]{return typed?convertFaxText(text,output,control):convertFaxDocument(file,output,control);}));
}
void FaxWidget::updateFax(const FaxInfo &fax){
    if(conversion_){refresh();return;}
    if(!fax.incoming)active_=fax.id;
    if(fax.id==active_||fax.incoming){const bool active=fax.state=="dialing"||fax.state=="negotiating"||fax.state=="sending"||fax.state=="receiving";cancel_->setEnabled(active&&!fax.callId.isEmpty());if(fax.incoming&&active)active_=fax.id;
        activity_->setText((fax.incoming?"Receive":"Send")+QString(" · ")+fax.remote+" · "+(fax.protocol=="t38"?"T.38":"G.711")+" · "+fax.state+" · "+QString::number(fax.pages)+" pages"+(fax.detail.isEmpty()?QString():"\n"+fax.detail));}
    refresh();
}
void FaxWidget::refresh(){
    const auto path=selectedPath();received_->setRowCount(0);
    for(const auto &f:engine_.faxes())if(f.incoming){const int row=received_->rowCount();received_->insertRow(row);QString account=f.accountId;const int accountIndex=account_->findData(f.accountId);if(accountIndex>=0)account=account_->itemText(accountIndex);
        const QStringList values={f.timestamp.toLocalTime().toString("yyyy-MM-dd HH:mm"),f.remote,account,f.protocol=="t38"?"T.38":"G.711",QString::number(f.pages),f.state};
        for(int c=0;c<values.size();++c){auto *item=new QTableWidgetItem(values[c]);item->setToolTip(f.detail);received_->setItem(row,c,item);}
        received_->item(row,0)->setData(Qt::UserRole,f.id);received_->item(row,0)->setData(Qt::UserRole+1,f.path);if(f.path==path)received_->selectRow(row);
    }
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
