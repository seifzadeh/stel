#pragma once
// Small UDP registrar/B2BUA for native end-to-end tests. Each call leg has a
// different Call-ID, as with the production PBX; SDP/RTP is handled by MS2.
#include <QHostAddress>
#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QUdpSocket>
#include <QUuid>

class MiniPbx : public QObject {
public:
    struct Binding {QString contact;QHostAddress address;quint16 port=0;};
    explicit MiniPbx(QObject *parent=nullptr):QObject(parent) {
        socket_.bind(QHostAddress(QHostAddress::LocalHost),quint16(0));
        connect(&socket_,&QUdpSocket::readyRead,this,[this]{receive();});
    }
    quint16 port() const {return socket_.localPort();}
    QString server() const {return "sip:127.0.0.1:"+QString::number(port());}
    bool registered(const QString &user) const {return bindings_.contains(user);}
    int invites=0,answers=0,rejectedMessages=0;
    QList<QByteArray> messageBodies,messageFrom,messageTo,messageContentTypes;
    bool bareRequestUri=true;
private:
    struct Dialog {QString a,b,aId,bId;};
    struct Transaction {QHostAddress address;quint16 port;QByteArray id;QList<QByteArray> vias;};
    QUdpSocket socket_;
    QHash<QString,Binding> bindings_;
    QHash<QString,Dialog> dialogs_;
    QHash<QString,Transaction> transactions_;
    QHash<QString,QString> inviteBranches_;
    static QByteArray header(const QList<QByteArray> &lines,const QByteArray &name) {
        for(const auto &line:lines){const int c=line.indexOf(':');if(c>0&&line.left(c).trimmed().toLower()==name)return line.mid(c+1).trimmed();}return {};
    }
    static QString user(const QByteArray &value) {return QRegularExpression("sips?:([^@;> ]+)@").match(QString::fromUtf8(value)).captured(1);}
    static QString uri(const QByteArray &value) {return QRegularExpression("sips?:[^> ]+").match(QString::fromUtf8(value)).captured(0);}
    static QList<QByteArray> replace(QList<QByteArray> lines,const QByteArray &name,const QByteArray &value) {
        for(int n=lines.size()-1;n>0;--n){const int c=lines[n].indexOf(':');if(c>0&&lines[n].left(c).trimmed().toLower()==name)lines.removeAt(n);}
        if(!value.isEmpty())lines<<name+": "+value;
        return lines;
    }
    void send(const QList<QByteArray> &lines,const QByteArray &body,const QHostAddress &address,quint16 port) {
        QByteArray data;for(const auto &line:lines)data+=line+"\r\n";data+="\r\n"+body;socket_.writeDatagram(data,address,port);
    }
    void receive() {
        while(socket_.hasPendingDatagrams()) {
            QByteArray data(int(socket_.pendingDatagramSize()),'\0');QHostAddress address;quint16 port;socket_.readDatagram(data.data(),data.size(),&address,&port);
            const int split=data.indexOf("\r\n\r\n");if(split<0)continue;
            QList<QByteArray> lines;for(auto line:data.left(split).split('\n'))lines<<line.trimmed();if(lines.isEmpty())continue;
            const auto body=data.mid(split+4);
            if(lines[0].startsWith("REGISTER ")) {
                const auto name=user(header(lines,"from"));const auto contact=header(lines,"contact");
                const bool off=header(lines,"expires")=="0"||contact.contains("expires=0");
                if(off)bindings_.remove(name);else bindings_[name]={uri(contact),address,port};
                QList<QByteArray> response{"SIP/2.0 200 OK"};for(const auto &key:{"via","from","to","call-id","cseq"})response<<QByteArray(key)+": "+header(lines,key)+(QByteArray(key)=="to"?";tag=pbx":"");
                response<<"Contact: "+contact<<"Expires: "+QByteArray(off?"0":"600")<<"Content-Length: 0";send(response,{},address,port);continue;
            }
            const auto id=QString::fromUtf8(header(lines,"call-id"));
            if(lines[0].startsWith("SIP/2.0 ")) {
                const auto branch=QRegularExpression("branch=([^; ]+)").match(QString::fromUtf8(header(lines,"via"))).captured(1);
                if(!transactions_.contains(branch))continue;
                const auto transaction=transactions_[branch];
                lines=replace(lines,"via",{});for(const auto &via:transaction.vias)lines<<via;
                lines=replace(lines,"call-id",transaction.id);
                const auto contact=header(lines,"contact");if(!contact.isEmpty()){const auto name=user(contact).isEmpty()?user(header(lines,"to")):user(contact);lines=replace(lines,"contact",("<sip:"+name+"@127.0.0.1:"+QString::number(this->port())+">").toUtf8());}
                if(lines[0].startsWith("SIP/2.0 200")&&header(lines,"cseq").endsWith("INVITE"))++answers;
                send(lines,body,transaction.address,transaction.port);continue;
            }
            if(lines[0].startsWith("MESSAGE ")){
                if(header(lines,"content-type").startsWith("text/plain")){
                    messageBodies<<body;messageFrom<<header(lines,"from");messageTo<<header(lines,"to");messageContentTypes<<header(lines,"content-type");
                }
                const auto destination=bindings_.value(user(header(lines,"to")));
                if(!destination.port){
                    ++rejectedMessages;
                    QList<QByteArray> response{"SIP/2.0 404 Not Found"};
                    for(const auto &key:{"via","from","to","call-id","cseq"})response<<QByteArray(key)+": "+header(lines,key)+(QByteArray(key)=="to"?";tag=missing":"");
                    response<<"Content-Length: 0";send(response,{},address,port);continue;
                }
                const auto branch="z9hG4bK-message-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                QList<QByteArray> vias;for(const auto &line:lines)if(line.toLower().startsWith("via:"))vias<<line;
                transactions_[branch]={address,port,id.toUtf8(),vias};
                auto target=destination.contact;if(bareRequestUri)target.replace(QRegularExpression("(?<=sip:)[^@]+@"),"");
                lines[0]="MESSAGE "+target.toUtf8()+" SIP/2.0";lines=replace(lines,"route",{});
                lines=replace(lines,"via",("SIP/2.0/UDP 127.0.0.1:"+QString::number(this->port())+";branch="+branch).toUtf8());
                send(lines,body,destination.address,destination.port);continue;
            }
            if(!dialogs_.contains(id)) {
                if(!lines[0].startsWith("INVITE "))continue;
                const auto a=user(header(lines,"from")),b=user(header(lines,"to"));if(!bindings_.contains(a)||!bindings_.contains(b))continue;
                const auto second="pbx-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
                const Dialog dialog{a,b,id,second};dialogs_[id]=dialog;dialogs_[second]=dialog;++invites;
            }
            const auto dialog=dialogs_[id];const bool fromA=id==dialog.aId;
            const auto destination=bindings_.value(fromA?dialog.b:dialog.a);if(!destination.port)continue;
            const auto method=lines[0].split(' ').value(0);
            const auto inviteKey=id+':'+QString::fromUtf8(header(lines,"cseq").split(' ').value(0));
            auto branch="z9hG4bK-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            // CANCEL must address the forwarded INVITE transaction, including
            // its Via branch. Retransmitted INVITEs also retain that branch.
            if(method=="INVITE"||method=="CANCEL"){
                if(inviteBranches_.contains(inviteKey))branch=inviteBranches_.value(inviteKey);
                else if(method=="INVITE")inviteBranches_.insert(inviteKey,branch);
            }
            QList<QByteArray> vias;for(const auto &line:lines)if(line.toLower().startsWith("via:"))vias<<line;
            transactions_[branch]={address,port,id.toUtf8(),vias};
            auto target=destination.contact;
            if(bareRequestUri)target.replace(QRegularExpression("(?<=sip:)[^@]+@"),"");
            lines[0]=method+' '+target.toUtf8()+" SIP/2.0";
            lines=replace(lines,"route",{});lines=replace(lines,"via",("SIP/2.0/UDP 127.0.0.1:"+QString::number(this->port())+";branch="+branch).toUtf8());
            lines=replace(lines,"call-id",(fromA?dialog.bId:dialog.aId).toUtf8());
            if(!header(lines,"contact").isEmpty())lines=replace(lines,"contact",("<sip:"+(fromA?dialog.a:dialog.b)+"@127.0.0.1:"+QString::number(this->port())+">").toUtf8());
            send(lines,body,destination.address,destination.port);
        }
    }
};
