#pragma once
#include <QFrame>
#include <QPointer>
class QLabel;
class QVBoxLayout;
class QPushButton;
namespace stel {
// A drawer inside the phone LCD. Call views lend their existing input panels
// here, so inline and expanded controls share state and validation.
class InlineCallHost : public QFrame {
    Q_OBJECT
public:
    explicit InlineCallHost(QWidget *parent=nullptr);
    void present(QWidget *,const QString &title);
    void dismiss();
    void setExpansionAvailable(bool);
signals:
    void backRequested();
    void windowRequested();
private:
    QLabel *title_;
    QVBoxLayout *content_;
    QWidget *contentWidget_;
    QPushButton *window_;
    QPointer<QWidget> current_;
};
}
