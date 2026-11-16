#pragma once
#include <QString>

namespace stel {
struct PagingRequest {
    bool requested = false;
    QString headerName;
};
// Recognize the explicit SIP auto-answer markers used by Yealink-compatible
// PBXs. Ring-tone URLs and ordinary distinctive-ring names are not requests.
PagingRequest pagingRequest(const QString &alertInfo,const QString &callInfo = {});
}
