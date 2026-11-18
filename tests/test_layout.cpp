#include "windowlayout.h"
#include "mainwindow.h"
#include "deskphone.h"
#include "calldialog.h"
#include <QApplication>
#include <QDialogButtonBox>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QtTest>
using namespace stel;
class LayoutTests : public QObject {
    Q_OBJECT
    static bool fits(QWidget &window){return window.screen()->availableGeometry().contains(window.frameGeometry());}
    static bool inside(QWidget &window,QWidget *control){return control&&control->isVisible()&&window.rect().contains(QRect(control->mapTo(&window,QPoint()),control->size()));}
    static void seed(Store &store){Preferences prefs;prefs.inputDevice=dummyMicTone();prefs.outputDevice=dummySpeaker();Account a;a.id=a.username="layout";a.domain="localhost";a.registerEnabled=false;a.transport="udp";a.rememberPassword=false;QVERIFY(store.save({a},prefs));}
private slots:
    void mainWindowIsCompactAndRecoverable(){
        QTemporaryDir directory;Store store(directory.path());seed(store);MainWindow window(directory.path());QVERIFY(window.ready());window.show();QTRY_VERIFY(fits(window));QVERIFY(window.width()<=420);QVERIFY(window.height()<=315);QCOMPARE(window.font().pixelSize(),6);
        auto *desk=window.findChild<DeskPhone*>();QVERIFY(desk);for(auto *button:{desk->settings,desk->audioCall,desk->hangup,desk->speaker})QVERIFY(inside(window,button));
        auto *target=window.findChild<QLineEdit*>("dialTarget");QVERIFY(target);QTest::mouseClick(desk->findChild<QPushButton*>("deskKey_5"),Qt::LeftButton);QCOMPARE(target->text(),QString("5"));
        window.resize(2500,2000);QTRY_VERIFY(fits(window));window.move(10000,-10000);QTRY_VERIFY(fits(window));
    }
    void allSettingsPagesKeepCloseReachable(){
        QTemporaryDir directory;Store store(directory.path());seed(store);MainWindow window(directory.path());QVERIFY(window.ready());window.show();window.showSettings();
        auto *dialog=window.findChild<QDialog*>("settingsDialog");auto *pages=window.findChild<QTabWidget*>("settingsPages");auto *scroll=window.findChild<QScrollArea*>("settingsScroll");QVERIFY(dialog);QVERIFY(pages);QVERIFY(scroll);
        auto *close=dialog->findChild<QDialogButtonBox*>("settingsButtons")->button(QDialogButtonBox::Close);
        QVERIFY(dialog->width()<=420);QVERIFY(dialog->height()<=300);
        auto *username=dialog->findChild<QLineEdit*>("username");QVERIFY(username);QVERIFY(username->height()<=15);
        if(dialog->width()>=uiPixels(600))QTRY_VERIFY(inside(*dialog,dialog->findChild<QPushButton*>("registerAccount")));
        for(int n=0;n<pages->count();++n){pages->setCurrentIndex(n);QTest::qWait(50);QTRY_VERIFY(fits(*dialog));QVERIFY2(inside(*dialog,close),qPrintable(pages->tabText(n)));}
        for(const auto &name:{"registerAccount","apiApply"}){
            pages->setCurrentIndex(QString(name)=="apiApply"?7:0);auto *button=dialog->findChild<QPushButton*>(name);QVERIFY(button);if(auto *page=qobject_cast<QScrollArea*>(pages->currentWidget()))page->ensureWidgetVisible(button);scroll->ensureWidgetVisible(button);QTest::qWait(50);
            QVERIFY2(scroll->viewport()->rect().contains(QRect(button->mapTo(scroll->viewport(),QPoint()),button->size())),qPrintable(QString("%1: button %2,%3 %4x%5; viewport %6x%7; settings %8x%9").arg(name).arg(button->mapTo(scroll->viewport(),QPoint()).x()).arg(button->mapTo(scroll->viewport(),QPoint()).y()).arg(button->width()).arg(button->height()).arg(scroll->viewport()->width()).arg(scroll->viewport()->height()).arg(scroll->widget()->width()).arg(scroll->widget()->height())));
        }
        close->click();QVERIFY(window.isVisible());
    }
    void expandedCallKeepsFooterReachable(){
        QTemporaryDir directory;Store store(directory.path());seed(store);MainWindow window(directory.path());QVERIFY(window.ready());window.show();auto *engine=window.findChild<SipEngine*>();
        CallInfo info;info.id="layout-call";info.incoming=true;info.video=true;info.rawState=LinphoneCallIncomingReceived;info.remoteName="Incoming video call";
        CallDialog call(*engine,info,&window);call.enableDocking();call.show();QTRY_VERIFY(fits(call));
        QVERIFY(call.width()<=230);QVERIFY(call.height()<=290);
        for(const auto &name:{"callClose","callDock"})QVERIFY(inside(call,call.findChild<QPushButton*>(name)));
        call.findChild<QPushButton*>("callDock")->click();QVERIFY(!call.isVisible());
    }
};
int main(int argc,char **argv){configureUiDpi();QApplication app(argc,argv);compactApplicationFont(app);LayoutTests tests;return QTest::qExec(&tests,argc,argv);}
#include "test_layout.moc"
