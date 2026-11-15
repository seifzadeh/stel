#pragma once
#include "sipengine.h"
#include "faxdocument.h"
#include <QWidget>
class QComboBox;class QLineEdit;class QLabel;class QPushButton;class QTableWidget;
class QTimer;
class QCheckBox;class QTemporaryDir;
namespace stel {
class DeskFaxSurface;
class DeskButton;
class FaxWidget : public QWidget {
    Q_OBJECT
public:
    explicit FaxWidget(SipEngine &,QWidget *parent=nullptr,bool desk=false);
    ~FaxWidget() override;
    void setAccounts(const QVector<Account> &);
    bool receiveCall(const CallInfo &);
    void setDestination(const QString &);
signals:
    void contactRequested();
private:
    SipEngine &engine_;
    QComboBox *account_,*protocol_,*source_;
    QLineEdit *target_,*document_;
    QLabel *activity_;
    QPushButton *send_,*cancel_,*open_,*save_;
    QTableWidget *received_,*sent_=nullptr;
    QWidget *fileInput_,*textInput_;
    QLabel *textSummary_;
    QPushButton *preview_;
    QTimer *conversionProgress_;
    QString textDraft_;
    std::shared_ptr<FaxConversionControl> conversion_;
    QString active_;
    DeskFaxSurface *desk_=nullptr;
    DeskButton *deskTabs_[3]={};
    QCheckBox *cover_=nullptr;
    QString coverText_;
    int deskPage_=0,previewPage_=0,previewPages_=0;
    std::shared_ptr<QTemporaryDir> previewDirectory_;
    QString previewPath_;
    void buildDesk(QPushButton *,QPushButton *);
    void showDeskPage(int);
    void updateDeskPreview();
    void refresh();
    void send();
    void editText();
    void prepare(bool sendAfter,bool inlineOnly=false);
    void updateFax(const FaxInfo &);
    QString selectedPath() const;
};
}
