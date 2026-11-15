#include "faxdocument.h"
#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImageReader>
#include <QPainter>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QThread>
#include <cmath>
#include <functional>
#include <mupdf/fitz.h>
extern "C" {
#include <tiffio.h>
}

static void registerFaxDocumentResources(){Q_INIT_RESOURCE(faxdocuments);}
namespace stel {
namespace {
constexpr int MaxPages=100;
constexpr qint64 MaxFile=100*1024*1024;
constexpr double MmPerInch=25.4,MarginMm=10,LayoutDpi=144;
QString documentFont;
TIFF *openTiff(const QString &path,const char *mode){
#ifdef Q_OS_WIN
    return TIFFOpenW(reinterpret_cast<const wchar_t*>(path.utf16()),mode);
#else
    return TIFFOpen(path.toUtf8().constData(),mode);
#endif
}
bool cancelled(const std::shared_ptr<FaxConversionControl> &c){return c&&c->cancelled.load();}
FaxDocumentResult failure(const QString &path,const QString &message,bool cancel=false){QFile::remove(path);return {false,cancel,0,message};}
void progress(const std::shared_ptr<FaxConversionControl> &c,int page,int total){if(c){c->total=total;c->page=page;}}
QRectF printablePixels(){
    const double paperWidth=210*FaxDpiX/MmPerInch;
    return {(FaxPageWidth-paperWidth)/2+MarginMm*FaxDpiX/MmPerInch,
        MarginMm*FaxDpiY/MmPerInch,190*FaxDpiX/MmPerInch,277*FaxDpiY/MmPerInch};
}
QRectF fitPhysical(double width,double height){
    const double scale=qMin(190/width,277/height);
    const QSizeF mm(width*scale,height*scale);const auto area=printablePixels();
    const QSizeF pixels(mm.width()*FaxDpiX/MmPerInch,mm.height()*FaxDpiY/MmPerInch);
    return {area.center().x()-pixels.width()/2,area.center().y()-pixels.height()/2,pixels.width(),pixels.height()};
}
QImage whitePage(){QImage image(FaxPageWidth,FaxPageHeight,QImage::Format_RGB32);image.fill(Qt::white);return image;}
QImage fitImage(const QImage &image){
    double width=image.width(),height=image.height();
    if(image.dotsPerMeterX()>0&&image.dotsPerMeterY()>0){width/=image.dotsPerMeterX();height/=image.dotsPerMeterY();}
    QImage page=whitePage();QPainter p(&page);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawImage(fitPhysical(width,height),image);return page;
}
class TiffWriter {
    TIFF *file_=nullptr;
    QString path_;
    bool complete_=false;
public:
    explicit TiffWriter(const QString &path):path_(path){file_=openTiff(path,"w");}
    ~TiffWriter(){if(file_)TIFFClose(file_);if(!complete_)QFile::remove(path_);}
    bool valid() const{return file_;}
    bool write(const QImage &image,int index,int total){
        if(!file_||image.size()!=QSize(FaxPageWidth,FaxPageHeight))return false;
        TIFFSetField(file_,TIFFTAG_IMAGEWIDTH,FaxPageWidth);TIFFSetField(file_,TIFFTAG_IMAGELENGTH,FaxPageHeight);
        TIFFSetField(file_,TIFFTAG_BITSPERSAMPLE,1);TIFFSetField(file_,TIFFTAG_SAMPLESPERPIXEL,1);
        TIFFSetField(file_,TIFFTAG_COMPRESSION,COMPRESSION_CCITTFAX4);TIFFSetField(file_,TIFFTAG_PHOTOMETRIC,PHOTOMETRIC_MINISWHITE);
        TIFFSetField(file_,TIFFTAG_FILLORDER,FILLORDER_MSB2LSB);TIFFSetField(file_,TIFFTAG_PLANARCONFIG,PLANARCONFIG_CONTIG);
        TIFFSetField(file_,TIFFTAG_ORIENTATION,ORIENTATION_TOPLEFT);TIFFSetField(file_,TIFFTAG_ROWSPERSTRIP,FaxPageHeight);
        TIFFSetField(file_,TIFFTAG_RESOLUTIONUNIT,RESUNIT_INCH);TIFFSetField(file_,TIFFTAG_XRESOLUTION,FaxDpiX);TIFFSetField(file_,TIFFTAG_YRESOLUTION,FaxDpiY);
        TIFFSetField(file_,TIFFTAG_PAGENUMBER,uint16_t(index),uint16_t(total));
        const auto mono=image.convertToFormat(QImage::Format_Mono,Qt::MonoOnly|Qt::DiffuseDither);QByteArray row(FaxPageWidth/8,0);
        const bool oneIsBlack=qGray(mono.color(1))<128;
        for(int y=0;y<FaxPageHeight;++y){const auto *line=mono.constScanLine(y);for(int x=0;x<row.size();++x)row[x]=char(oneIsBlack?line[x]:~line[x]);
            if(TIFFWriteScanline(file_,row.data(),uint32_t(y),0)<0)return false;
        }
        const bool ok=TIFFWriteDirectory(file_)==1;if(ok&&index+1==total)complete_=true;return ok;
    }
};
QImage readTiffPage(TIFF *file,QString *error){
    uint32_t w=0,h=0;TIFFGetField(file,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(file,TIFFTAG_IMAGELENGTH,&h);
    if(!w||!h||w>20000||h>20000||quint64(w)*h>100000000){if(error)*error="The image exceeds the 100 megapixel limit.";return {};}
    QVector<uint32_t> pixels(int(w*h));
    if(!TIFFReadRGBAImageOriented(file,w,h,pixels.data(),ORIENTATION_TOPLEFT,1)){if(error)*error="Cannot decode this TIFF page.";return {};}
    QImage image(int(w),int(h),QImage::Format_RGB32);if(image.isNull()){if(error)*error="Not enough memory to render the page.";return {};}
    for(uint32_t y=0;y<h;++y){auto *line=reinterpret_cast<QRgb*>(image.scanLine(int(y)));for(uint32_t x=0;x<w;++x){const auto rgba=pixels[int(y*w+x)];const int a=TIFFGetA(rgba);line[x]=qRgb(TIFFGetR(rgba)+255-a,TIFFGetG(rgba)+255-a,TIFFGetB(rgba)+255-a);}}
    float dx=0,dy=0;uint16_t unit=0;TIFFGetField(file,TIFFTAG_XRESOLUTION,&dx);TIFFGetField(file,TIFFTAG_YRESOLUTION,&dy);TIFFGetFieldDefaulted(file,TIFFTAG_RESOLUTIONUNIT,&unit);
    if(dx>0&&dy>0&&(unit==RESUNIT_INCH||unit==RESUNIT_CENTIMETER)){const double factor=unit==RESUNIT_INCH?1000/MmPerInch:100;image.setDotsPerMeterX(qRound(dx*factor));image.setDotsPerMeterY(qRound(dy*factor));}
    return image;
}
FaxDocumentResult convertTiff(const QString &source,const QString &destination,const std::shared_ptr<FaxConversionControl> &control){
    std::unique_ptr<TIFF,decltype(&TIFFClose)> input(openTiff(source,"r"),TIFFClose);
    if(!input)return failure(destination,"Cannot open this TIFF document.");
    bool standard=true;int pages=0;
    do{uint32_t w=0,h=0;uint16_t bits=0,samples=0,orientation=0,unit=0;float dx=0,dy=0;
        TIFFGetField(input.get(),TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(input.get(),TIFFTAG_IMAGELENGTH,&h);TIFFGetFieldDefaulted(input.get(),TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetFieldDefaulted(input.get(),TIFFTAG_SAMPLESPERPIXEL,&samples);TIFFGetFieldDefaulted(input.get(),TIFFTAG_ORIENTATION,&orientation);TIFFGetFieldDefaulted(input.get(),TIFFTAG_RESOLUTIONUNIT,&unit);TIFFGetField(input.get(),TIFFTAG_XRESOLUTION,&dx);TIFFGetField(input.get(),TIFFTAG_YRESOLUTION,&dy);
        standard&=w==FaxPageWidth&&h==FaxPageHeight&&bits==1&&samples==1&&orientation==ORIENTATION_TOPLEFT&&!TIFFIsTiled(input.get())&&unit==RESUNIT_INCH&&std::abs(dx-FaxDpiX)<0.1&&std::abs(dy-FaxDpiY)<0.1;
        if(++pages>MaxPages)return failure(destination,"A fax can contain at most 100 pages.");
    }while(TIFFReadDirectory(input.get()));
    if(cancelled(control))return failure(destination,"Conversion cancelled.",true);
    if(standard){QFile::remove(destination);if(!QFile::copy(source,destination))return failure(destination,"Cannot copy the fax document.");progress(control,pages,pages);return {true,false,pages,{}};}
    TiffWriter writer(destination);if(!writer.valid())return failure(destination,"Cannot create the fax document.");
    for(int page=0;page<pages;++page){if(cancelled(control))return failure(destination,"Conversion cancelled.",true);progress(control,page+1,pages);TIFFSetDirectory(input.get(),tdir_t(page));QString error;auto image=readTiffPage(input.get(),&error);
        if(image.isNull())return failure(destination,error);
        if(!writer.write(fitImage(image),page,pages))return failure(destination,"Cannot write the fax document.");
    }
    return {true,false,pages,{}};
}
// MuPDF's C exceptions use longjmp. No C++ objects are created inside fz_try.
class PdfDocument {
    QByteArray bytes_;
    fz_context *context_=nullptr;
    fz_document *document_=nullptr;
public:
    QString error;
    int pages=0;
    explicit PdfDocument(QByteArray bytes):bytes_(std::move(bytes)){
        context_=fz_new_context(nullptr,nullptr,128*1024*1024);if(!context_){error="Cannot create the PDF renderer.";return;}
        fz_stream *stream=nullptr;fz_var(stream);
        fz_try(context_){fz_register_document_handlers(context_);stream=fz_open_memory(context_,reinterpret_cast<const unsigned char*>(bytes_.constData()),size_t(bytes_.size()));document_=fz_open_document_with_stream(context_,"application/pdf",stream);
            if(fz_needs_password(context_,document_))fz_throw(context_,FZ_ERROR_ARGUMENT,"The PDF is password-protected. Save an unlocked copy before faxing.");
            pages=fz_count_pages(context_,document_);
        }fz_always(context_){fz_drop_stream(context_,stream);}fz_catch(context_){error=QString::fromUtf8(fz_caught_message(context_));}
    }
    ~PdfDocument(){if(context_){fz_drop_document(context_,document_);fz_drop_context(context_);}}
    QImage render(int number){
        fz_page *page=nullptr;fz_pixmap *pixmap=nullptr;fz_device *device=nullptr;fz_rect bounds{};fz_var(page);fz_var(pixmap);fz_var(device);
        fz_try(context_){page=fz_load_page(context_,document_,number);bounds=fz_bound_page(context_,page);
            if(!std::isfinite(bounds.x0)||!std::isfinite(bounds.x1)||!std::isfinite(bounds.y0)||!std::isfinite(bounds.y1)||bounds.x1<=bounds.x0||bounds.y1<=bounds.y0)fz_throw(context_,FZ_ERROR_ARGUMENT,"The PDF contains an invalid page size.");
        }fz_catch(context_){error=QString::fromUtf8(fz_caught_message(context_));fz_drop_page(context_,page);return {};}
        const auto target=fitPhysical(bounds.x1-bounds.x0,bounds.y1-bounds.y0);const int w=qMax(1,qRound(target.width())),h=qMax(1,qRound(target.height()));
        const float sx=float(w)/(bounds.x1-bounds.x0),sy=float(h)/(bounds.y1-bounds.y0);const fz_matrix transform{sx,0,0,sy,-bounds.x0*sx,-bounds.y0*sy};
        fz_try(context_){pixmap=fz_new_pixmap_with_bbox(context_,fz_device_rgb(context_),fz_irect{0,0,w,h},nullptr,0);fz_clear_pixmap_with_value(context_,pixmap,255);device=fz_new_draw_device(context_,fz_identity,pixmap);fz_run_page(context_,page,device,transform,nullptr);fz_close_device(context_,device);
        }fz_always(context_){fz_drop_device(context_,device);fz_drop_page(context_,page);}fz_catch(context_){error=QString::fromUtf8(fz_caught_message(context_));fz_drop_pixmap(context_,pixmap);return {};}
        QImage image(fz_pixmap_samples(context_,pixmap),w,h,fz_pixmap_stride(context_,pixmap),QImage::Format_RGB888);QImage canvas=whitePage();{QPainter p(&canvas);p.drawImage(target,image);}fz_drop_pixmap(context_,pixmap);return canvas;
    }
};
FaxDocumentResult convertPdf(QByteArray bytes,const QString &destination,const std::shared_ptr<FaxConversionControl> &control){
    PdfDocument pdf(std::move(bytes));if(!pdf.error.isEmpty())return failure(destination,pdf.error);
    if(pdf.pages<1||pdf.pages>MaxPages)return failure(destination,"A fax must contain between 1 and 100 pages.");
    TiffWriter writer(destination);if(!writer.valid())return failure(destination,"Cannot create the fax document.");
    for(int page=0;page<pdf.pages;++page){if(cancelled(control))return failure(destination,"Conversion cancelled.",true);progress(control,page+1,pdf.pages);auto image=pdf.render(page);if(image.isNull())return failure(destination,pdf.error);if(!writer.write(image,page,pdf.pages))return failure(destination,"Cannot write the fax document.");}
    return {true,false,pdf.pages,{}};
}
QString decodeText(QByteArray bytes,QString &error){
    QString text;
    if(bytes.startsWith("\xef\xbb\xbf"))bytes.remove(0,3);
    if(bytes.startsWith("\xff\xfe")||bytes.startsWith("\xfe\xff")){
        const bool little=quint8(bytes[0])==0xff;if(bytes.size()%2){error="Invalid UTF-16 text file.";return {};}
        for(int i=2;i<bytes.size();i+=2){const ushort value=little?ushort(quint8(bytes[i])|(quint8(bytes[i+1])<<8)):ushort((quint8(bytes[i])<<8)|quint8(bytes[i+1]));text.append(QChar(value));}
    }else{
        text=QString::fromUtf8(bytes);
        if(text.toUtf8()!=bytes){
            // Windows-1256 is the traditional Persian/Arabic Notepad encoding.
            static const ushort cp1256[128]={
                0x20ac,0x067e,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,0x02c6,0x2030,0x0679,0x2039,0x0152,0x0686,0x0698,0x0688,
                0x06af,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x06a9,0x2122,0x0691,0x203a,0x0153,0x200c,0x200d,0x06ba,
                0x00a0,0x060c,0x00a2,0x00a3,0x00a4,0x00a5,0x00a6,0x00a7,0x00a8,0x00a9,0x06be,0x00ab,0x00ac,0x00ad,0x00ae,0x00af,
                0x00b0,0x00b1,0x00b2,0x00b3,0x00b4,0x00b5,0x00b6,0x00b7,0x00b8,0x00b9,0x061b,0x00bb,0x00bc,0x00bd,0x00be,0x061f,
                0x06c1,0x0621,0x0622,0x0623,0x0624,0x0625,0x0626,0x0627,0x0628,0x0629,0x062a,0x062b,0x062c,0x062d,0x062e,0x062f,
                0x0630,0x0631,0x0632,0x0633,0x0634,0x0635,0x0636,0x00d7,0x0637,0x0638,0x0639,0x063a,0x0640,0x0641,0x0642,0x0643,
                0x00e0,0x0644,0x00e2,0x0645,0x0646,0x0647,0x0648,0x00e7,0x00e8,0x00e9,0x00ea,0x00eb,0x0649,0x064a,0x00ee,0x00ef,
                0x064b,0x064c,0x064d,0x064e,0x00f4,0x064f,0x0650,0x00f7,0x0651,0x00f9,0x0652,0x00fb,0x00fc,0x200e,0x200f,0x06d2
            };
            text.clear();for(unsigned char ch:bytes)text.append(QChar(ch<128?ch:cp1256[ch-128]));
        }
    }
    for(const auto ch:text)if((ch.unicode()<32&&ch!='\n'&&ch!='\r'&&ch!='\t'&&ch!='\f')||ch==QChar(0xfffd)){error="The TXT file contains invalid or binary text. Use UTF-8, UTF-16 or Windows-1256.";return {};}
    return text;
}
}
void initializeFaxDocumentFonts(){
    auto *app=qobject_cast<QGuiApplication*>(QCoreApplication::instance());if(!app||QThread::currentThread()!=app->thread())return;
    if(!documentFont.isEmpty())return;
    registerFaxDocumentResources();const int id=QFontDatabase::addApplicationFont(":/faxfonts/Vazirmatn-Regular.ttf");
    const auto families=QFontDatabase::applicationFontFamilies(id);documentFont=families.isEmpty()?QString("DejaVu Sans"):families.first();
}
FaxDocumentResult convertFaxText(const QString &input,const QString &destination,const std::shared_ptr<FaxConversionControl> &control){
    if(!qobject_cast<QGuiApplication*>(QCoreApplication::instance()))return failure(destination,"A GUI application is required to lay out fax text.");
    if(input.trimmed().isEmpty())return failure(destination,"Enter some text to fax.");
    if(input.size()>250000)return failure(destination,"Fax text is limited to 250,000 characters and 100 pages.");
    if(cancelled(control))return failure(destination,"Conversion cancelled.",true);
    initializeFaxDocumentFonts();QString text=input;text.replace("\r\n","\n");text.replace('\r','\n');
    QImage metrics(1,1,QImage::Format_RGB32);metrics.setDotsPerMeterX(qRound(LayoutDpi*1000/MmPerInch));metrics.setDotsPerMeterY(qRound(LayoutDpi*1000/MmPerInch));
    QTextDocument document;document.documentLayout()->setPaintDevice(&metrics);document.setDocumentMargin(0);document.setDefaultFont(QFont(documentFont.isEmpty()?"Vazirmatn":documentFont,11));
    QTextOption option;option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);option.setUseDesignMetrics(true);option.setTextDirection(Qt::LayoutDirectionAuto);document.setDefaultTextOption(option);
    document.setPageSize(QSizeF(190*LayoutDpi/MmPerInch,277*LayoutDpi/MmPerInch));
    const auto segments=text.split(QChar('\f'));QTextCursor cursor(&document);bool first=true;
    for(int n=0;n<segments.size();++n){const auto lines=segments[n].split('\n');for(int line=0;line<lines.size();++line){if(!first)cursor.insertBlock();first=false;QTextBlockFormat format;const bool rtl=lines[line].isRightToLeft();format.setLayoutDirection(rtl?Qt::RightToLeft:Qt::LeftToRight);format.setAlignment((rtl?Qt::AlignRight:Qt::AlignLeft)|Qt::AlignAbsolute);format.setPageBreakPolicy(n>0&&line==0?QTextFormat::PageBreak_AlwaysBefore:QTextFormat::PageBreak_Auto);cursor.setBlockFormat(format);cursor.insertText(lines[line]);}}
    const int pages=document.pageCount();if(pages<1||pages>MaxPages)return failure(destination,"This text exceeds the 100-page fax limit.");
    TiffWriter writer(destination);if(!writer.valid())return failure(destination,"Cannot create the fax document.");
    const auto area=printablePixels();
    for(int page=0;page<pages;++page){if(cancelled(control))return failure(destination,"Conversion cancelled.",true);progress(control,page+1,pages);auto image=whitePage();
        {QPainter p(&image);p.setRenderHint(QPainter::TextAntialiasing);p.setClipRect(area);p.translate(area.left(),area.top());p.scale(FaxDpiX/LayoutDpi,FaxDpiY/LayoutDpi);p.translate(0,-page*document.pageSize().height());
            QAbstractTextDocumentLayout::PaintContext context;context.palette.setColor(QPalette::Text,Qt::black);context.clip=QRectF(0,page*document.pageSize().height(),document.pageSize().width(),document.pageSize().height());document.documentLayout()->draw(&p,context);}
        if(!writer.write(image,page,pages))return failure(destination,"Cannot write the fax document.");
    }
    return {true,false,pages,{}};
}
FaxDocumentResult convertFaxDocument(const QString &source,const QString &destination,const std::shared_ptr<FaxConversionControl> &control){
    if(QFileInfo(source).absoluteFilePath()==QFileInfo(destination).absoluteFilePath())return {false,false,0,"The input and output files must differ."};
    QFile file(source);if(!file.open(QIODevice::ReadOnly))return failure(destination,"Cannot open the fax document.");
    if(file.size()>MaxFile)return failure(destination,"The document exceeds 100 MiB.");
    const auto signature=file.peek(1024);const auto suffix=QFileInfo(source).suffix().toLower();
    if(cancelled(control))return failure(destination,"Conversion cancelled.",true);
    if(signature.indexOf("%PDF-")>=0||suffix=="pdf")return convertPdf(file.readAll(),destination,control);
    if(signature.startsWith("II")||signature.startsWith("MM")){file.close();return convertTiff(source,destination,control);}
    if(suffix=="txt"){
        if(file.size()>1024*1024)return failure(destination,"TXT files are limited to 1 MiB.");
        QString error;const auto text=decodeText(file.readAll(),error);if(!error.isEmpty())return failure(destination,error);return convertFaxText(text,destination,control);
    }
    file.close();QImageReader reader(source);reader.setAutoTransform(true);const auto size=reader.size();
    if(!size.isValid()||size.width()>20000||size.height()>20000||qint64(size.width())*size.height()>100000000)return failure(destination,"Choose a PDF, TXT, TIFF, PNG or JPEG document (images up to 100 megapixels).");
    auto image=reader.read();if(image.isNull())return failure(destination,"Cannot decode this image.");
    if(reader.imageCount()>1)return failure(destination,"Use TIFF or PDF for multipage documents.");
    TiffWriter writer(destination);if(!writer.valid()||!writer.write(fitImage(image),0,1))return failure(destination,"Cannot write the fax document.");progress(control,1,1);return {true,false,1,{}};
}
bool prepareFaxDocument(const QString &source,const QString &destination,QString *error){auto result=convertFaxDocument(source,destination);if(error)*error=result.error;return result.ok;}
QImage faxDocumentPage(const QString &path,int page,QString *error){
    std::unique_ptr<TIFF,decltype(&TIFFClose)> input(openTiff(path,"r"),TIFFClose);
    if(!input||page<0||page>=MaxPages||!TIFFSetDirectory(input.get(),tdir_t(page))){if(error)*error="Cannot open this fax page.";return {};}
    return readTiffPage(input.get(),error);
}
}
