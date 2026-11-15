#include "faxdocument.h"
#include "faxwidget.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QLabel>
#include <QLineEdit>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
extern "C" {
#include <tiffio.h>
}
using namespace stel;
class FaxDocumentTests:public QObject {
    Q_OBJECT
    static QRect ink(const QImage &image){QRect box;for(int y=0;y<image.height();++y){int first=-1,last=-1;for(int x=0;x<image.width();++x)if(qGray(image.pixel(x,y))<128){if(first<0)first=x;last=x;}if(first>=0)box=box.united(QRect(first,y,last-first+1,1));}return box;}
    static void verifyA4(const QString &path,int pages){
        auto *file=TIFFOpen(QFile::encodeName(path).constData(),"r");QVERIFY(file);int count=0;
        do{uint32_t w=0,h=0;uint16_t bits=0,unit=0,compression=0;float dx=0,dy=0;
            TIFFGetField(file,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(file,TIFFTAG_IMAGELENGTH,&h);TIFFGetField(file,TIFFTAG_XRESOLUTION,&dx);TIFFGetField(file,TIFFTAG_YRESOLUTION,&dy);TIFFGetField(file,TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetField(file,TIFFTAG_RESOLUTIONUNIT,&unit);TIFFGetField(file,TIFFTAG_COMPRESSION,&compression);
            QCOMPARE(w,uint32_t(1728));QCOMPARE(h,uint32_t(2292));QCOMPARE(bits,uint16_t(1));QCOMPARE(unit,uint16_t(RESUNIT_INCH));QCOMPARE(compression,uint16_t(COMPRESSION_CCITTFAX4));QCOMPARE(dx,204.0f);QCOMPARE(dy,196.0f);++count;
        }while(TIFFReadDirectory(file));TIFFClose(file);QCOMPARE(count,pages);
    }
    static QString pdf(const QString &directory,int pages=3){
        const auto path=directory+"/mixed-pages.pdf";QPdfWriter writer(path);writer.setResolution(144);writer.setPageMargins(QMarginsF(0,0,0,0));writer.setPageSize(QPageSize(QPageSize::Letter));
        QPainter painter(&writer);for(int page=0;page<pages;++page){if(page){writer.setPageSize(QPageSize(page%2?QSizeF(420,210):QSizeF(210,500),QPageSize::Millimeter));writer.newPage();}
            const int w=writer.width(),h=writer.height();painter.setPen(QPen(Qt::black,3));painter.drawRect(15,15,w-30,h-30);painter.fillRect(25,25,40,40,Qt::black);painter.fillRect(w-65,25,40,40,Qt::black);painter.fillRect(25,h-65,40,40,Qt::black);painter.fillRect(w-65,h-65,40,40,Qt::black);
            painter.setFont(QFont("Vazirmatn",16));painter.drawText(QRect(80,100,w-160,200),Qt::AlignCenter,QString::fromUtf8("صفحهٔ آزمایشی PDF — متن فارسی و English\nPage ")+QString::number(page+1));
        }painter.end();return path;
    }
    static void screenshot(const QImage &image,const QString &name){const auto folder=qEnvironmentVariable("STEL_QA_SCREENSHOTS");if(!folder.isEmpty()){QDir().mkpath(folder);image.save(folder+'/'+name+".png");}}
private slots:
    void initTestCase(){initializeFaxDocumentFonts();QVERIFY(QFontDatabase().families().contains("Vazirmatn"));}
    void imageGeometry(){
        QTemporaryDir tmp;QImage image(1000,200,QImage::Format_RGB32);image.fill(Qt::white);{QPainter p(&image);p.setPen(QPen(Qt::black,5));p.drawRect(2,2,995,195);p.fillRect(20,20,50,50,Qt::black);}const auto source=tmp.path()+"/landscape.png";QVERIFY(image.save(source));
        const auto dest=tmp.path()+"/fax.tif";const auto result=convertFaxDocument(source,dest);QVERIFY2(result.ok,qPrintable(result.error));verifyA4(dest,1);auto rendered=faxDocumentPage(dest,0);const auto bounds=ink(rendered);QVERIFY(bounds.left()>=98);QVERIFY(bounds.right()<=1630);QVERIFY(bounds.top()>=75);QVERIFY(bounds.bottom()<=2218);QVERIFY(std::abs(double(bounds.width())/bounds.height()-5.0*204/196)<0.15);screenshot(rendered,"fax-a4-landscape");
        const auto copy=tmp.path()+"/copy.tif";QVERIFY(convertFaxDocument(dest,copy).ok);QFile a(dest),b(copy);QVERIFY(a.open(QIODevice::ReadOnly));QVERIFY(b.open(QIODevice::ReadOnly));QCOMPARE(a.readAll(),b.readAll());
    }
    void persianTextPagination(){
        QTemporaryDir tmp;QString text=QString::fromUtf8("نامهٔ فارسی آزمایشی\nاین متن می‌تواند نیم‌فاصله، اعداد ۱۲۳۴۵ و English 123 داشته باشد.\n");
        for(int n=1;n<=120;++n)text+=QString::fromUtf8("سطر %1: متن فارسی برای بررسی خوانایی و ادامهٔ صحیح در صفحهٔ بعد. SIP 62223 / 62224\n").arg(n);
        text+=QString::fromUtf8("پایان متن — LAST LINE");const auto destination=tmp.path()+"/text.tif";const auto result=convertFaxText(text,destination);QVERIFY2(result.ok,qPrintable(result.error));QVERIFY(result.pages>=2&&result.pages<=6);verifyA4(destination,result.pages);
        for(int n=0;n<result.pages;++n){const auto image=faxDocumentPage(destination,n);const auto bounds=ink(image);QVERIFY(!bounds.isEmpty());QVERIFY(bounds.left()>=98&&bounds.right()<=1630);QVERIFY(bounds.top()>=75&&bounds.bottom()<=2218);if(n==0||n+1==result.pages)screenshot(image,"fax-persian-text-page-"+QString::number(n+1));}
        const auto split=convertFaxText("First page\fSecond page\nSecond line",tmp.path()+"/break.tif");QVERIFY(split.ok);QCOMPARE(split.pages,2);
    }
    void textFileEncodings(){
        QTemporaryDir tmp;const auto text=QString::fromUtf8("سلام دنیا\nمی‌توانم فارسی بنویسم. English 123");QList<QByteArray> encoded={text.toUtf8(),QByteArray("\xef\xbb\xbf")+text.toUtf8()};
        QByteArray little("\xff\xfe"),big("\xfe\xff");for(auto c:text){little.append(char(c.unicode()&255));little.append(char(c.unicode()>>8));big.append(char(c.unicode()>>8));big.append(char(c.unicode()&255));}encoded<<little<<big;
        const auto reference=tmp.path()+"/reference.tif";QVERIFY(convertFaxText(text,reference).ok);const auto expected=faxDocumentPage(reference,0);
        for(int n=0;n<encoded.size();++n){QFile file(tmp.path()+"/encoding.txt");QVERIFY(file.open(QIODevice::WriteOnly));file.write(encoded[n]);file.close();const auto dest=tmp.path()+"/encoded.tif";const auto result=convertFaxDocument(file.fileName(),dest);QVERIFY2(result.ok,qPrintable(result.error));QCOMPARE(faxDocumentPage(dest,0),expected);}
        QFile old(tmp.path()+"/old.txt");QVERIFY(old.open(QIODevice::WriteOnly));old.write(QByteArray::fromHex("d3e1c7e320cfe4edc7"));old.close();QVERIFY(convertFaxDocument(old.fileName(),tmp.path()+"/old.tif").ok);QVERIFY(convertFaxText(QString::fromUtf8("سلام دنیا"),tmp.path()+"/old-reference.tif").ok);QCOMPARE(faxDocumentPage(tmp.path()+"/old.tif",0),faxDocumentPage(tmp.path()+"/old-reference.tif",0));
    }
    void coverPageIsPreservedBeforeDocument(){
        QTemporaryDir tmp;const auto cover=tmp.path()+"/cover.tif",body=tmp.path()+"/body.tif",joined=tmp.path()+"/joined.tif";
        QVERIFY(convertFaxText(QString::fromUtf8("FAX\nبه: دفتر\nموضوع: آزمایش برگهٔ همراه"),cover).ok);
        QVERIFY(convertFaxText("Document page one\fDocument page two",body).ok);
        auto result=joinFaxDocuments({cover,body},joined);QVERIFY2(result.ok,qPrintable(result.error));QCOMPARE(result.pages,3);verifyA4(joined,3);
        QCOMPARE(faxDocumentPage(joined,0),faxDocumentPage(cover,0));QCOMPARE(faxDocumentPage(joined,1),faxDocumentPage(body,0));QCOMPARE(faxDocumentPage(joined,2),faxDocumentPage(body,1));
        auto control=std::make_shared<FaxConversionControl>();control->cancelled=true;const auto cancelled=tmp.path()+"/cancelled.tif";QVERIFY(joinFaxDocuments({cover,body},cancelled,control).cancelled);QVERIFY(!QFile::exists(cancelled));
    }
    void pdfMixedPageSizes(){
        QTemporaryDir tmp;const auto source=pdf(tmp.path()),dest=tmp.path()+"/pdf.tif";const auto result=convertFaxDocument(source,dest);QVERIFY2(result.ok,qPrintable(result.error));QCOMPARE(result.pages,3);verifyA4(dest,3);
        for(int n=0;n<3;++n){auto image=faxDocumentPage(dest,n);const auto bounds=ink(image);QVERIFY(!bounds.isEmpty());QVERIFY(bounds.left()>=98&&bounds.right()<=1630);QVERIFY(bounds.top()>=75&&bounds.bottom()<=2218);screenshot(image,"fax-pdf-page-"+QString::number(n+1));}
    }
    void validationAndCancellation(){
        QTemporaryDir tmp;QFile broken(tmp.path()+"/bad.pdf");QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("%PDF-1.7\nbroken PDF");broken.close();const auto output=tmp.path()+"/output.tif";QVERIFY(!convertFaxDocument(broken.fileName(),output).ok);QVERIFY(!QFile::exists(output));QVERIFY(!convertFaxText(" ",output).ok);QVERIFY(!convertFaxText(QString(250001,'x'),output).ok);
        auto control=std::make_shared<FaxConversionControl>();control->cancelled=true;auto cancelled=convertFaxText("cancel me",output,control);QVERIFY(!cancelled.ok&&cancelled.cancelled);QVERIFY(!QFile::exists(output));
        QFile binary(tmp.path()+"/binary.txt");QVERIFY(binary.open(QIODevice::WriteOnly));binary.write(QByteArray("abc\0def",7));binary.close();QVERIFY(!convertFaxDocument(binary.fileName(),output).ok);
        QVERIFY(!convertFaxDocument(broken.fileName(),broken.fileName()).ok);QVERIFY(QFile::exists(broken.fileName()));
        const auto excessive=pdf(tmp.path(),101);QVERIFY(!convertFaxDocument(excessive,output).ok);QVERIFY(!QFile::exists(output));
    }
    void typedTextPreview(){
        SipEngine engine;FaxWidget widget(engine);widget.resize(940,650);widget.show();auto *source=widget.findChild<QComboBox*>("faxSource");QVERIFY(source);source->setCurrentIndex(1);
        QTimer::singleShot(0,&widget,[&]{auto *dialog=widget.findChild<QDialog*>("faxTextDialog");QVERIFY(dialog);auto *edit=dialog->findChild<QPlainTextEdit*>("faxTextInput");QCOMPARE(edit->layoutDirection(),Qt::LeftToRight);edit->setPlainText(QString::fromUtf8("متن تایپ‌شده برای ارسال فکس\nEnglish text on A4\fصفحهٔ دوم"));dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        widget.findChild<QPushButton*>("faxEditText")->click();QVERIFY(widget.findChild<QLabel*>("faxTextSummary")->text().contains("characters"));
        bool previewed=false;QTimer close;close.setInterval(20);connect(&close,&QTimer::timeout,&widget,[&]{auto *dialog=widget.findChild<QDialog*>("faxPreviewDialog");if(!dialog||!dialog->isVisible())return;auto *label=dialog->findChild<QLabel*>("faxPreviewPage");QVERIFY(label->text().contains("1 / 2"));dialog->findChild<QPushButton*>("faxPreviewNext")->click();QVERIFY(label->text().contains("2 / 2"));const auto folder=qEnvironmentVariable("STEL_QA_SCREENSHOTS");if(!folder.isEmpty())dialog->grab().save(folder+"/fax-a4-preview-dialog.png");previewed=true;dialog->reject();close.stop();});close.start();
        widget.findChild<QPushButton*>("faxPreview")->click();QTRY_VERIFY_WITH_TIMEOUT(previewed,15000);
    }
    void cancelBeforeCalling(){
        QTemporaryDir tmp;QFile input(tmp.path()+"/draft.txt");QVERIFY(input.open(QIODevice::WriteOnly));input.write(QString::fromUtf8("متن آزمایشی برای لغو ارسال").toUtf8());input.close();
        SipEngine engine;FaxWidget widget(engine);Account account;account.id=account.username="test";account.domain="localhost";widget.setAccounts({account});
        widget.findChild<QLineEdit*>("faxTarget")->setText("other");widget.findChild<QLineEdit*>("faxDocument")->setText(input.fileName());
        auto *send=widget.findChild<QPushButton*>("faxSend"),*cancel=widget.findChild<QPushButton*>("faxCancel");QVERIFY(send->isEnabled());send->click();QVERIFY(cancel->isEnabled());cancel->click();
        QTRY_COMPARE_WITH_TIMEOUT(widget.findChild<QLabel*>("faxActivity")->text(),QString("Conversion cancelled."),15000);
        QVERIFY(engine.faxes().isEmpty());QVERIFY(engine.lastError().isEmpty());QVERIFY(send->isEnabled());QVERIFY(!cancel->isEnabled());
    }
    void deskFaxCoverPreview(){
        SipEngine engine;FaxWidget widget(engine,nullptr,true);widget.resize(539,410);widget.show();
        widget.findChild<QComboBox*>("faxSource")->setCurrentIndex(1);widget.findChild<QCheckBox*>("faxCoverPage")->setChecked(true);
        QTimer::singleShot(0,&widget,[&]{auto *dialog=widget.findChild<QDialog*>("faxTextDialog");QVERIFY(dialog);dialog->findChild<QPlainTextEdit*>("faxTextInput")->setPlainText("First document page\fSecond document page");dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        widget.findChild<QPushButton*>("faxEditText")->click();
        bool previewed=false;QTimer close;close.setInterval(20);connect(&close,&QTimer::timeout,&widget,[&]{auto *dialog=widget.findChild<QDialog*>("faxPreviewDialog");if(!dialog||!dialog->isVisible())return;QVERIFY(dialog->findChild<QLabel*>("faxPreviewPage")->text().contains("1 / 3"));previewed=true;dialog->reject();close.stop();});close.start();widget.findChild<QPushButton*>("faxPreview")->click();QTRY_VERIFY_WITH_TIMEOUT(previewed,15000);
        widget.findChild<QPushButton*>("faxDeskTab1")->click();QVERIFY(!widget.findChild<QLineEdit*>("faxTarget")->isVisible());widget.findChild<QPushButton*>("faxDeskTab0")->click();QVERIFY(widget.findChild<QLineEdit*>("faxTarget")->isVisible());
    }
};
QTEST_MAIN(FaxDocumentTests)
#include "test_faxdocuments.moc"
