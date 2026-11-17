#pragma once
#include <QWidget>
#include "model.h"
namespace stel {
class RemoteControlServer;
class RemoteControlWidget : public QWidget {
    Q_OBJECT
public:
    RemoteControlWidget(RemoteControlServer &,QVector<Account> &,Preferences &,Store &,bool,QWidget *parent=nullptr);
};
}
