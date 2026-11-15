#include "isolatedsipengine.h"
#include "minipbx.h"
#include "faxwidget.h"
#include "faxdocument.h"
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QPdfWriter>
#include <QPageSize>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QtTest>
extern "C" {
#include <tiffio.h>
}
using namespace stel;
class FaxTests:public QObject {
    Q_OBJECT
    static QString page(const QString &directory){
        const auto path=directory+"/page.tif";auto *t=TIFFOpen(QFile::encodeName(path).constData(),"w");if(!t)return {};
        TIFFSetField(t,TIFFTAG_IMAGEWIDTH,1728);TIFFSetField(t,TIFFTAG_IMAGELENGTH,160);TIFFSetField(t,TIFFTAG_BITSPERSAMPLE,1);TIFFSetField(t,TIFFTAG_SAMPLESPERPIXEL,1);TIFFSetField(t,TIFFTAG_COMPRESSION,COMPRESSION_CCITTFAX4);TIFFSetField(t,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_MINISWHITE);TIFFSetField(t,TIFFTAG_FILLORDER,FILLORDER_MSB2LSB);TIFFSetField(t,TIFFTAG_ROWSPERSTRIP,160);TIFFSetField(t,TIFFTAG_RESOLUTIONUNIT,RESUNIT_INCH);TIFFSetField(t,TIFFTAG_XRESOLUTION,204.0);TIFFSetField(t,TIFFTAG_YRESOLUTION,196.0);
        QByteArray row(216,0);for(int y=0;y<160;++y){row.fill(0);if(y>=24&&y<136)for(int x=32;x<210;++x)row[x]=char((y/8)%2?0xaa:0xf0);TIFFWriteScanline(t,row.data(),y,0);}TIFFClose(t);return path;
    }
    static QImage readPage(const QString &path){
        auto *t=TIFFOpen(QFile::encodeName(path).constData(),"r");if(!t)return {};uint32_t w=0,h=0;TIFFGetField(t,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(t,TIFFTAG_IMAGELENGTH,&h);QImage out(int(w),int(h),QImage::Format_RGB32);QVector<uint32_t> pixels(int(w*h));const bool ok=TIFFReadRGBAImageOriented(t,w,h,pixels.data(),ORIENTATION_TOPLEFT,0);if(ok)for(int y=0;y<int(h);++y)for(int x=0;x<int(w);++x){const auto p=pixels[y*int(w)+x];out.setPixel(x,y,qRgb(TIFFGetR(p),TIFFGetG(p),TIFFGetB(p)));}TIFFClose(t);return ok?out:QImage();
    }
    static Account account(const QString &name,const MiniPbx &pbx){Account a;a.id=a.username=name;a.domain="localhost";a.server=pbx.server();a.transport="udp";a.ice=false;return a;}
private slots:
    void initTestCase(){const auto prefix=qEnvironmentVariable("STEL_RESOURCE_PREFIX");if(!prefix.isEmpty())SipEngine::configureResources(prefix);QVERIFY(SipEngine::t38Available());}
    void udptlValidationAndLossRecovery(){
        const auto packet=faxUdptlPacket(65535,"primary",{"previous","older"});quint16 sequence=0;QList<QByteArray> frames;QVERIFY(faxUdptlDecode(packet,sequence,frames));QCOMPARE(sequence,quint16(65535));QCOMPARE(frames,QList<QByteArray>({"primary","previous","older"}));
        for(int n=0;n<packet.size();++n)QVERIFY(!faxUdptlDecode(packet.left(n),sequence,frames));
        QVERIFY(!faxUdptlDecode(packet+"trailing",sequence,frames));QVERIFY(!faxUdptlDecode(QByteArray::fromHex("0001c0000000"),sequence,frames));QVERIFY(!faxUdptlDecode(QByteArray::fromHex("000101ff8000"),sequence,frames));
        const QByteArray longIfp(300,'x');QVERIFY(faxUdptlDecode(faxUdptlPacket(0,longIfp,{}),sequence,frames));QCOMPARE(frames[0],longIfp);
    }
    void documents(){
        QTemporaryDir tmp;const auto source=page(tmp.path());QString error;QVERIFY(prepareFaxDocument(source,tmp.path()+"/copy.tif",&error));QCOMPARE(readPage(tmp.path()+"/copy.tif").size(),QSize(FaxPageWidth,FaxPageHeight));
        QImage image(600,800,QImage::Format_RGB32);image.fill(Qt::white);{QPainter p(&image);p.fillRect(50,50,450,100,Qt::black);}QVERIFY(image.save(tmp.path()+"/image.png"));QVERIFY(prepareFaxDocument(tmp.path()+"/image.png",tmp.path()+"/rendered.tif",&error));QCOMPARE(readPage(tmp.path()+"/rendered.tif").size(),QSize(FaxPageWidth,FaxPageHeight));
        QVERIFY(!prepareFaxDocument(tmp.path()+"/missing.pdf",tmp.path()+"/bad.tif",&error));QVERIFY(!error.isEmpty());QVERIFY(!QFile::exists(tmp.path()+"/bad.tif"));
    }
    void g711ModemRoundTrip(){
        QTemporaryDir tmp;const auto source=page(tmp.path());FaxMedia sender(false,"g711",source),receiver(true,"g711",tmp.path()+"/rx.tif");QSignalSpy sent(&sender,&FaxMedia::finished),received(&receiver,&FaxMedia::finished);QVERIFY(sender.start());QVERIFY(receiver.start());QVERIFY(sender.connectPeer(QHostAddress::LocalHost,9));QVERIFY(receiver.connectPeer(QHostAddress::LocalHost,9));
        int16_t a[160],b[160];for(int n=0;n<60000&&(!sender.completed()||!receiver.completed());++n){sender.readAudio(a,160);receiver.readAudio(b,160);sender.writeAudio(b,160);receiver.writeAudio(a,160);if(n%256==0)QTest::qWait(2);}
        QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,1000);QTRY_COMPARE_WITH_TIMEOUT(received.size(),1,1000);QVERIFY2(sent[0][0].toBool(),qPrintable(sent[0][2].toString()));QVERIFY2(received[0][0].toBool(),qPrintable(received[0][2].toString()));QCOMPARE(readPage(tmp.path()+"/rx.tif"),readPage(source));
    }
    void t38UdpRoundTrip_data(){QTest::addColumn<bool>("loss");QTest::newRow("clean")<<false;QTest::newRow("packet-loss")<<true;}
    void t38UdpRoundTrip(){
        QFETCH(bool,loss);
        QTemporaryDir tmp;const auto source=page(tmp.path());FaxMedia sender(false,"t38",source),receiver(true,"t38",tmp.path()+"/rx.tif");QSignalSpy sent(&sender,&FaxMedia::finished),received(&receiver,&FaxMedia::finished);QVERIFY(sender.start(QHostAddress::LocalHost));QVERIFY(receiver.start(QHostAddress::LocalHost));QVERIFY(sender.connectPeer(QHostAddress::LocalHost,receiver.port()));QVERIFY(receiver.connectPeer(QHostAddress::LocalHost,sender.port()));
        QUdpSocket relay;int packets=0;
        if(loss){QVERIFY(relay.bind(QHostAddress(QHostAddress::LocalHost),quint16(0)));QVERIFY(sender.connectPeer(QHostAddress::LocalHost,relay.localPort()));QVERIFY(receiver.connectPeer(QHostAddress::LocalHost,relay.localPort()));
            connect(&relay,&QUdpSocket::readyRead,this,[&]{while(relay.hasPendingDatagrams()){QByteArray data(int(relay.pendingDatagramSize()),0);QHostAddress address;quint16 port;relay.readDatagram(data.data(),data.size(),&address,&port);if(++packets%11==0)continue;relay.writeDatagram(data,QHostAddress::LocalHost,port==sender.port()?receiver.port():sender.port());}});
        }
        QTRY_COMPARE_WITH_TIMEOUT(received.size(),1,45000);QTRY_COMPARE_WITH_TIMEOUT(sent.size(),1,5000);QVERIFY2(received[0][0].toBool(),qPrintable(received[0][2].toString()));QVERIFY2(sent[0][0].toBool(),qPrintable(sent[0][2].toString()));QCOMPARE(readPage(tmp.path()+"/rx.tif"),readPage(source));if(loss)QVERIFY(packets>20);
    }
    void sipFax_data(){QTest::addColumn<QString>("protocol");QTest::addColumn<bool>("sameCore");QTest::newRow("t38")<<QString("t38")<<false;QTest::newRow("pass-through")<<QString("g711")<<false;QTest::newRow("same-core-t38")<<QString("t38")<<true;QTest::newRow("same-core-g711")<<QString("g711")<<true;QTest::newRow("default-ice")<<QString("t38")<<false;}
    void sipFax(){
        QFETCH(QString,protocol);QFETCH(bool,sameCore);const bool automatic=QByteArray(QTest::currentDataTag())=="default-ice";IsolatedSipEngine::resetPortPools();QTemporaryDir tmp;MiniPbx pbx;Preferences prefs;prefs.inputDevice=dummyMicSilence();prefs.outputDevice=dummySpeaker();prefs.mediaAddress=automatic?QString():QString("127.0.0.1");IsolatedSipEngine sender,other;auto &receiver=sameCore?sender:other;QVERIFY(sender.start(prefs,tmp.path()+"/a"));if(!sameCore)QVERIFY(receiver.start(prefs,tmp.path()+"/b"));auto alice=account("alice",pbx),bob=account("bob",pbx);alice.ice=bob.ice=automatic;QVERIFY(sender.addAccount(alice));QVERIFY(receiver.addAccount(bob));QTRY_VERIFY_WITH_TIMEOUT(pbx.registered("alice")&&pbx.registered("bob"),6000);
        connect(&receiver,&SipEngine::callChanged,this,[&](const CallInfo &info){if(info.rawState==LinphoneCallIncomingReceived)QTimer::singleShot(0,&receiver,[&,id=info.id]{QVERIFY2(receiver.receiveFax(id,protocol),qPrintable(receiver.lastError()));});});
        connect(&sender,&SipEngine::faxChanged,this,[](const FaxInfo &f){qInfo()<<"TX"<<f.state<<f.pages<<f.detail;});
        connect(&receiver,&SipEngine::faxChanged,this,[](const FaxInfo &f){qInfo()<<"RX"<<f.state<<f.pages<<f.detail;});
        connect(&sender,&SipEngine::callChanged,this,[](const CallInfo &c){qInfo()<<"TX call"<<c.state;});
        connect(&receiver,&SipEngine::callChanged,this,[](const CallInfo &c){qInfo()<<"RX call"<<c.state;});
        auto source=page(tmp.path());int expectedPages=1;
        if(!sameCore&&!automatic&&protocol=="t38"){
            initializeFaxDocumentFonts();source=tmp.path()+"/document.pdf";QPdfWriter pdf(source);pdf.setPageSize(QPageSize(QPageSize::Letter));pdf.setResolution(144);
            QPainter painter(&pdf);painter.setFont(QFont("Vazirmatn",14));painter.drawText(QRect(40,40,pdf.width()-80,220),Qt::AlignLeft,QString::fromUtf8("PDF fax — A4 conversion\nسلام، ارسال و دریافت فکس فارسی"));
            pdf.setPageSize(QPageSize(QSizeF(300,180),QPageSize::Millimeter));QVERIFY(pdf.newPage());painter.drawText(QRect(40,40,pdf.width()-80,220),Qt::AlignLeft,QString::fromUtf8("صفحهٔ دوم PDF\nLast page — mixed page sizes"));painter.end();expectedPages=2;
        }else if(!sameCore&&protocol=="g711"){
            source=tmp.path()+"/document.txt";QFile text(source);QVERIFY(text.open(QIODevice::WriteOnly));text.write(QString::fromUtf8("فکس از فایل متنی TXT\nPersian and English — A4 fax test").toUtf8());text.close();
        }else if(sameCore&&protocol=="g711"){
            initializeFaxDocumentFonts();source=tmp.path()+"/typed.tif";QVERIFY(convertFaxText(QString::fromUtf8("متن تایپ‌شده برای فکس\nTyped text fax"),source).ok);
        }
        QVERIFY2(sender.sendFax("alice","bob",source,protocol),qPrintable(sender.lastError()));
        const auto job=[](const SipEngine &e,bool incoming){for(const auto &f:e.faxes())if(f.incoming==incoming)return f;return FaxInfo{};};
        const auto done=[&]{const auto tx=job(sender,false),rx=job(receiver,true);return !tx.id.isEmpty()&&!rx.id.isEmpty()&&((tx.state=="completed"&&rx.state=="completed")||tx.state=="failed"||rx.state=="failed");};
        QTRY_VERIFY_WITH_TIMEOUT(done(),90000);
        const auto tx=job(sender,false),rx=job(receiver,true);if(tx.state!="completed"||rx.state!="completed")qInfo()<<pbx.inviteBodies<<pbx.sdpAnswers;
        QCOMPARE(tx.state,QString("completed"));QCOMPARE(rx.state,QString("completed"));QCOMPARE(tx.pages,expectedPages);QCOMPARE(rx.pages,expectedPages);QCOMPARE(rx.protocol,protocol);
        for(int n=0;n<expectedPages;++n){const auto received=faxDocumentPage(rx.path,n);QCOMPARE(received,faxDocumentPage(tx.path,n));QCOMPARE(received.size(),QSize(FaxPageWidth,FaxPageHeight));}QVERIFY(pbx.answers>=1);
        if(protocol=="t38"){bool image=false;for(const auto &sdp:pbx.inviteBodies)image|=sdp.contains(" udptl t38");QVERIFY(image);}else{for(const auto &sdp:pbx.inviteBodies)QVERIFY(!sdp.contains(" udptl t38"));}
        FaxWidget widget(receiver);widget.setAccounts({account("bob",pbx)});QCOMPARE(widget.findChild<QComboBox*>("faxProtocol")->currentData().toString(),QString("t38"));QCOMPARE(widget.findChild<QTableWidget*>("receivedFaxes")->rowCount(),1);widget.resize(900,620);widget.show();
        const auto screenshots=qEnvironmentVariable("STEL_QA_SCREENSHOTS");if(!screenshots.isEmpty()){QDir().mkpath(screenshots);widget.grab().save(screenshots+"/fax-"+protocol+".png");}
        sender.shutdown();if(!sameCore)receiver.shutdown();SipEngine restored;QVERIFY(restored.start(prefs,tmp.path()+(sameCore?"/a":"/b")));QCOMPARE(restored.faxes().size(),sameCore?2:1);QCOMPARE(job(restored,true).state,QString("completed"));QCOMPARE(job(restored,true).path,rx.path);
    }
    void corruptHistoryIsPreserved(){QTemporaryDir tmp;QVERIFY(QDir().mkpath(tmp.path()+"/faxes"));QFile history(tmp.path()+"/faxes/history.json");QVERIFY(history.open(QIODevice::WriteOnly));history.write("invalid fax history");history.close();{SipEngine engine;QVERIFY(engine.start({},tmp.path(),true));}QVERIFY(history.open(QIODevice::ReadOnly));QCOMPARE(history.readAll(),QByteArray("invalid fax history"));}
};
QTEST_MAIN(FaxTests)
#include "test_fax.moc"
