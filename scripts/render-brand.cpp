// Regenerate distributable PNG/ICO assets from the vector master, using Qt.
#include <QApplication>
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QTextStream>

static QImage render(const QString &path,int size) {
    QImageReader reader(path);reader.setScaledSize(QSize(size,size));auto image=reader.read();
    if(image.isNull())qFatal("Cannot render SVG: %s",qPrintable(reader.errorString()));return image;
}
static QString pathData(const QPainterPath &path) {
    QString output;QTextStream s(&output);s.setRealNumberPrecision(5);
    for(int n=0;n<path.elementCount();++n){const auto e=path.elementAt(n);
        if(e.isMoveTo())s<<"M"<<e.x<<" "<<e.y;
        else if(e.isLineTo())s<<"L"<<e.x<<" "<<e.y;
        else if(e.type==QPainterPath::CurveToElement){const auto b=path.elementAt(++n),c=path.elementAt(++n);s<<"C"<<e.x<<" "<<e.y<<" "<<b.x<<" "<<b.y<<" "<<c.x<<" "<<c.y;}
    }return output;
}
int main(int argc,char **argv) {
    QApplication app(argc,argv);if(argc!=2)return 2;const QDir directory(QString::fromLocal8Bit(argv[1]));
    const auto master=directory.filePath("stel.svg");QDir().mkpath(directory.filePath("icons"));
    const QList<int> sizes{16,20,24,32,40,48,64,128,256,512,1024};QVector<QByteArray> iconFrames;
    for(int size:sizes){const auto picture=render(master,size);if(!picture.save(directory.filePath(QString("icons/stel-%1.png").arg(size))))return 3;
        if(size<=256){QByteArray frame;QBuffer buffer(&frame);buffer.open(QIODevice::WriteOnly);picture.save(&buffer,"PNG");iconFrames<<frame;}}
    QFile ico(directory.filePath("stel.ico"));if(!ico.open(QIODevice::WriteOnly))return 3;QDataStream output(&ico);output.setByteOrder(QDataStream::LittleEndian);
    output<<quint16(0)<<quint16(1)<<quint16(iconFrames.size());quint32 offset=6+16*iconFrames.size();
    for(int n=0;n<iconFrames.size();++n){const int size=sizes[n];output<<quint8(size==256?0:size)<<quint8(size==256?0:size)<<quint8(0)<<quint8(0)<<quint16(1)<<quint16(32)<<quint32(iconFrames[n].size())<<offset;offset+=iconFrames[n].size();}
    for(const auto &frame:iconFrames)ico.write(frame);ico.close();
    // Outline the wordmark, so exported SVG files do not depend on installed fonts.
    QFont font("DejaVu Sans");font.setPixelSize(104);font.setWeight(QFont::Bold);font.setLetterSpacing(QFont::AbsoluteSpacing,-5);
    QPainterPath letters;letters.addText(0,0,font,"stel");const auto bounds=letters.boundingRect();
    QFile source(master);source.open(QIODevice::ReadOnly);QString mark=QString::fromUtf8(source.readAll());const int start=mark.indexOf('>')+1;mark=mark.mid(start,mark.lastIndexOf("</svg>")-start);
    const int width=int(184+bounds.width());const auto svg=QString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"160\" viewBox=\"0 0 %1 160\"><title>Stel wordmark</title><g transform=\"translate(16 16)\">%2</g><path fill=\"#132C3B\" fill-rule=\"evenodd\" transform=\"translate(%3 %4)\" d=\"%5\"/></svg>\n").arg(width).arg(mark).arg(168-bounds.left()).arg(80-bounds.center().y()).arg(pathData(letters));
    QFile wordmark(directory.filePath("stel-wordmark.svg"));if(!wordmark.open(QIODevice::WriteOnly))return 3;wordmark.write(svg.toUtf8());wordmark.close();
    QImageReader wordReader(wordmark.fileName());wordReader.setScaledSize(QSize(width*2,320));const auto word=wordReader.read();if(word.isNull()||!word.save(directory.filePath("stel-wordmark.png")))return 3;
    QImage preview(1280,720,QImage::Format_ARGB32_Premultiplied);preview.fill(QColor("#F2F5F7"));QPainter p(&preview);p.setRenderHint(QPainter::Antialiasing);p.setRenderHint(QPainter::SmoothPixmapTransform);
    QFont title("DejaVu Sans",16,QFont::Bold);p.setFont(title);p.setPen(QColor("#132C3B"));p.drawText(56,64,"stel / visual identity");
    p.setPen(Qt::NoPen);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(48,96,1184,280),24,24);p.drawImage(QRectF(88,132,200,200),render(master,400));
    p.drawImage(QRectF(352,162,word.width()/2.0,160),word);
    p.setBrush(QColor("#132C3B"));p.drawRoundedRect(QRectF(48,408,566,252),24,24);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(638,408,594,252),24,24);
    p.setFont(QFont("DejaVu Sans",11));p.setPen(QColor("#A9C3CA"));p.drawText(80,448,"ONE MARK. EVERY SIZE.");
    int x=80;for(int size:{16,24,32,48,64,96}){p.drawImage(QRectF(x,584-size,size,size),render(master,size*2));p.setPen(QColor("#D6E4E9"));p.drawText(x,624,QString::number(size)+" px");x+=80;}
    p.setPen(QColor("#526B7C"));p.drawText(670,448,"TWO VOICES. ONE CONNECTION.");
    p.setPen(Qt::NoPen);int swatch=670;for(const auto &color:{"#132C3B","#29CCB1","#F0FAF8"}){p.setBrush(QColor(color));p.drawRoundedRect(QRectF(swatch,488,150,92),12,12);p.setPen(QColor("#526B7C"));p.drawText(swatch,624,color);p.setPen(Qt::NoPen);swatch+=173;}
    p.end();if(!preview.save(directory.filePath("../docs/stel-brand-preview.png")))return 3;
    QTextStream(stdout)<<"Generated vector wordmark, PNG sizes, ICO and brand preview.\n";return 0;
}
