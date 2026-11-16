#include "paging.h"
#include <QRegularExpression>
#include <QStringList>

namespace stel {
namespace {
enum class Decision { None, Answer, Manual };
QString value(QString text) {
    text=text.trimmed();
    if(text.size()>=2&&text.front()=='"'&&text.back()=='"')text=text.mid(1,text.size()-2);
    return text.simplified().toLower();
}
bool marker(const QString &text) {
    return text=="auto answer"||text=="auto-answer"||text=="alert-autoanswer"||text=="intercom";
}
Decision classify(QString header) {
    if(header.isEmpty())return Decision::None;
    if(header.size()>4096)return Decision::Manual;
    for(const auto c:header)if(c=='\r'||c=='\n'||c.isNull())return Decision::Manual;
    // The FreePBX spelling <Auto Answer> is supported; a URL containing an
    // auto-answer-looking substring must never authorize automatic pickup.
    static const QRegularExpression angle("<([^<>]*)>");
    QString outside;int position=0;
    const auto matches=angle.globalMatch(header);auto it=matches;
    while(it.hasNext()) {
        const auto match=it.next();outside+=header.mid(position,match.capturedStart()-position);
        const auto token=value(match.captured(1));outside+=marker(token)?token:QString(" ");
        position=match.capturedEnd();
    }
    outside+=header.mid(position);
    if(outside.contains('<')||outside.contains('>'))return Decision::Manual;
    QStringList tokens;QString token;bool quoted=false,escaped=false;
    for(const auto c:outside) {
        if(escaped){token+=c;escaped=false;continue;}
        if(c=='\\'&&quoted){token+=c;escaped=true;continue;}
        if(c=='"')quoted=!quoted;
        if(!quoted&&(c==';'||c==',')){tokens<<token;token.clear();}else token+=c;
    }
    if(quoted||escaped)return Decision::Manual;
    tokens<<token;bool answer=false;
    for(const auto &part:tokens) {
        const auto split=part.indexOf('=');
        if(split<0){answer|=marker(value(part));continue;}
        const auto key=value(part.left(split)),parameter=value(part.mid(split+1));
        if(key=="answer-after"||key=="delay") {
            if(parameter.isEmpty())return Decision::Manual;
            for(const auto c:parameter)if(c!='0')return Decision::Manual;
            if(key=="answer-after")answer=true;
        } else if(key=="info")answer|=marker(parameter);
        else if(key=="intercom") {
            if(parameter!="true")return Decision::Manual;
            answer=true;
        }
    }
    return answer?Decision::Answer:Decision::None;
}
}
PagingRequest pagingRequest(const QString &alertInfo,const QString &callInfo) {
    const auto call=classify(callInfo);
    if(call==Decision::Answer)return {true,"Call-Info"};
    if(call==Decision::Manual)return {};
    return classify(alertInfo)==Decision::Answer?PagingRequest{true,"Alert-Info"}:PagingRequest{};
}
}
