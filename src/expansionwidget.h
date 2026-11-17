#pragma once
#include "sipengine.h"
#include <QWidget>
class QTableWidget;
class QLabel;
namespace stel {
class ExpansionWidget : public QWidget {
    Q_OBJECT
public:
    ExpansionWidget(SipEngine &,QVector<Account> &,Preferences &,Store &,bool,QWidget *parent=nullptr);
    void focusKey(int);
    void refreshAccounts();
signals:
    void keysChanged();
private:
    SipEngine &engine_;
    QVector<Account> &accounts_;
    Preferences &prefs_;
    Store &store_;
    QTableWidget *table_;
    QLabel *result_;
    void showState(const BlfInfo &);
    void apply();
};
}
