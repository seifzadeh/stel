#include "expansionwidget.h"
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

namespace stel {
ExpansionWidget::ExpansionWidget(SipEngine &engine,QVector<Account> &accounts,Preferences &prefs,Store &store,bool canSave,QWidget *parent)
    :QWidget(parent),engine_(engine),accounts_(accounts),prefs_(prefs),store_(store) {
    setObjectName("expansionSettings");auto *layout=new QVBoxLayout(this);
    auto *hint=new QLabel("Configure the 12 keys below the display. Speed Dial calls immediately; BLF monitors the extension and also dials on click. Right-click a key to edit it.");
    hint->setWordWrap(true);layout->addWidget(hint);
    table_=new QTableWidget(ExpansionKeyCount,6,this);table_->setObjectName("expansionTable");
    table_->setHorizontalHeaderLabels({"Key","Type","Label","Account","Extension / SIP address","Status"});
    table_->verticalHeader()->hide();table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setAlternatingRowColors(true);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2,QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch);
    table_->setColumnWidth(2,135);table_->setColumnWidth(4,150);layout->addWidget(table_,1);
    for(int n=0;n<ExpansionKeyCount;++n) {
        const auto key=prefs_.expansionKeys.value(n);table_->setRowHeight(n,38);
        table_->setItem(n,0,new QTableWidgetItem(QString::number(n+1)));
        auto *type=new QComboBox;type->setObjectName("expansionType"+QString::number(n));
        type->addItem("Disabled","disabled");type->addItem("Speed Dial","speed-dial");type->addItem("BLF","blf");
        type->setCurrentIndex(qMax(0,type->findData(key.type)));table_->setCellWidget(n,1,type);
        auto *label=new QLineEdit(key.label);label->setObjectName("expansionLabel"+QString::number(n));label->setMaxLength(80);label->setPlaceholderText("Key "+QString::number(n+1));table_->setCellWidget(n,2,label);
        auto *account=new QComboBox;account->setObjectName("expansionAccount"+QString::number(n));account->setMinimumContentsLength(12);account->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);table_->setCellWidget(n,3,account);
        auto *target=new QLineEdit(key.target);target->setObjectName("expansionTarget"+QString::number(n));target->setMaxLength(512);target->setPlaceholderText("e.g. 62224");table_->setCellWidget(n,4,target);
        auto *state=new QLabel;state->setObjectName("expansionState"+QString::number(n));table_->setCellWidget(n,5,state);
        const auto update=[=]{const bool enabled=type->currentData()!="disabled";account->setEnabled(enabled);target->setEnabled(enabled);};
        connect(type,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[=]{update();});update();
    }
    refreshAccounts();
    auto *row=new QHBoxLayout;auto *clear=new QPushButton("Clear selected key");clear->setObjectName("clearExpansionKey");clear->setEnabled(canSave);
    auto *save=new QPushButton("Apply expansion keys");save->setObjectName("applyExpansion");save->setEnabled(canSave);
    row->addWidget(clear);row->addStretch();row->addWidget(save);layout->addLayout(row);
    result_=new QLabel;result_->setObjectName("expansionResult");result_->setWordWrap(true);layout->addWidget(result_);
    if(!canSave)result_->setText("The original settings file is invalid and has been preserved.");
    connect(clear,&QPushButton::clicked,this,[this]{const int n=table_->currentRow();if(n<0)return;static_cast<QComboBox*>(table_->cellWidget(n,1))->setCurrentIndex(0);static_cast<QLineEdit*>(table_->cellWidget(n,2))->clear();static_cast<QLineEdit*>(table_->cellWidget(n,4))->clear();});
    connect(save,&QPushButton::clicked,this,&ExpansionWidget::apply);
    connect(&engine_,&SipEngine::blfChanged,this,&ExpansionWidget::showState);
    for(int n=0;n<ExpansionKeyCount;++n)showState(engine_.blfInfo(n));
}
void ExpansionWidget::refreshAccounts() {
    for(int n=0;n<ExpansionKeyCount;++n) {
        auto *combo=static_cast<QComboBox*>(table_->cellWidget(n,3));
        const auto selected=combo->count()?combo->currentData().toString():prefs_.expansionKeys.value(n).accountId;
        const QSignalBlocker blocker(combo);combo->clear();combo->addItem("Choose account","");
        for(const auto &account:accounts_)combo->addItem(account.label.isEmpty()?account.username+'@'+account.domain:account.label,account.id);
        if(!selected.isEmpty()&&combo->findData(selected)<0)combo->addItem("Removed account",selected);
        combo->setCurrentIndex(qMax(0,combo->findData(selected)));
    }
}
void ExpansionWidget::focusKey(int key) {
    refreshAccounts();if(key<0||key>=ExpansionKeyCount)return;
    table_->setCurrentCell(key,1);table_->scrollToItem(table_->item(key,0));table_->cellWidget(key,1)->setFocus();
}
void ExpansionWidget::showState(const BlfInfo &info) {
    if(info.key<0||info.key>=ExpansionKeyCount)return;
    auto *label=static_cast<QLabel*>(table_->cellWidget(info.key,5));
    const auto mode=prefs_.expansionKeys.value(info.key).type;
    label->setText(mode=="blf"?blfStateLabel(info.state):mode=="speed-dial"?"Speed Dial":"Disabled");
    label->setToolTip(info.detail);
    label->setStyleSheet(info.state==BlfState::Idle?"color:#138e52;":info.state==BlfState::Busy||info.state==BlfState::Ringing?"color:#cf3c40;":"color:#687b8a;");
}
void ExpansionWidget::apply() {
    auto next=prefs_;
    for(int n=0;n<ExpansionKeyCount;++n) {
        auto &key=next.expansionKeys[n];
        key.type=static_cast<QComboBox*>(table_->cellWidget(n,1))->currentData().toString();
        key.label=static_cast<QLineEdit*>(table_->cellWidget(n,2))->text().trimmed();
        key.accountId=static_cast<QComboBox*>(table_->cellWidget(n,3))->currentData().toString();
        key.target=static_cast<QLineEdit*>(table_->cellWidget(n,4))->text().trimmed();
        bool found=false;for(const auto &account:accounts_)found|=account.id==key.accountId;
        if(key.type!="disabled"&&(!found||!key.validate().isEmpty())) {
            result_->setText(QString("Key %1: %2").arg(n+1).arg(!found?"Choose an existing account.":key.validate()));focusKey(n);return;
        }
    }
    QString error;
    if(!next.validate().isEmpty()){result_->setText(next.validate());return;}
    if(!store_.save(accounts_,next,&error)){result_->setText(error);return;}
    prefs_=next;engine_.setExpansionKeys(prefs_.expansionKeys);emit keysChanged();
    for(int n=0;n<ExpansionKeyCount;++n)showState(engine_.blfInfo(n));
    result_->setText("Expansion keys saved. BLF requires dialog subscriptions / extension hints on the PBX.");
}
}
