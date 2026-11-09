#pragma once
#include "model.h"
#include <QDialog>
#include <QWidget>
#include <QMap>
class QLineEdit;
class QComboBox;
class QCheckBox;
class QSpinBox;
namespace stel {
class AccountEditor : public QWidget {
    Q_OBJECT
public:
    explicit AccountEditor(QWidget *parent = nullptr);
    void setAccount(const Account &);
    Account account() const;
    bool dirty() const { return dirty_; }
signals:
    void edited();
private:
    bool dirty_ = false;
    Account original_;
    QMap<QString,QLineEdit*> text_;
    QMap<QString,QCheckBox*> check_;
    QComboBox *transport_, *encryption_;
    QSpinBox *expiry_;
};
class AccountDialog : public QDialog {
public:
    explicit AccountDialog(const Account &, QWidget *parent = nullptr);
    Account account() const { return editor_->account(); }
private:
    AccountEditor *editor_;
};
}
