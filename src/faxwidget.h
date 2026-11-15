#pragma once
#include "sipengine.h"
#include "faxdocument.h"
#include <QWidget>
class QComboBox;class QLineEdit;class QLabel;class QPushButton;class QTableWidget;
class QTimer;
namespace stel {
class FaxWidget : public QWidget {
    Q_OBJECT
public:
    explicit FaxWidget(SipEngine &,QWidget *parent=nullptr);
    ~FaxWidget() override;
    void setAccounts(const QVector<Account> &);
    bool receiveCall(const CallInfo &);
private:
    SipEngine &engine_;
    QComboBox *account_,*protocol_,*source_;
    QLineEdit *target_,*document_;
    QLabel *activity_;
    QPushButton *send_,*cancel_,*open_,*save_;
    QTableWidget *received_;
    QWidget *fileInput_,*textInput_;
    QLabel *textSummary_;
    QPushButton *preview_;
    QTimer *conversionProgress_;
    QString textDraft_;
    std::shared_ptr<FaxConversionControl> conversion_;
    QString active_;
    void refresh();
    void send();
    void editText();
    void prepare(bool sendAfter);
    void updateFax(const FaxInfo &);
    QString selectedPath() const;
};
}
