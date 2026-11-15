#pragma once
#include <QImage>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

namespace stel {
// T.4's 1728-pel scan line with an A4 sheet centred inside it. The unequal
// horizontal/vertical resolutions are intentional: artwork uses millimetres.
constexpr int FaxPageWidth=1728, FaxPageHeight=2292;
constexpr double FaxDpiX=204.0, FaxDpiY=196.0;
struct FaxConversionControl {
    std::atomic_bool cancelled{false};
    std::atomic_int page{0},total{0};
};
struct FaxDocumentResult {
    bool ok=false,cancelled=false;
    int pages=0;
    QString error;
};
void initializeFaxDocumentFonts(); // GUI thread, before starting a conversion worker.
FaxDocumentResult convertFaxDocument(const QString &source,const QString &destination,
    const std::shared_ptr<FaxConversionControl> &control={});
FaxDocumentResult convertFaxText(const QString &text,const QString &destination,
    const std::shared_ptr<FaxConversionControl> &control={});
FaxDocumentResult joinFaxDocuments(const QStringList &sources,const QString &destination,
    const std::shared_ptr<FaxConversionControl> &control={});
bool prepareFaxDocument(const QString &source,const QString &destination,QString *error);
QImage faxDocumentPage(const QString &path,int page,QString *error=nullptr);
}
