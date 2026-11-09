#pragma once
#include <QImage>
#include <QWidget>
#include <linphone/linphonecore.h>
namespace stel {
// Software display sink: SDK threads publish the latest frame, Qt paints it.
void installQtVideoRenderer(LinphoneCore *);
class VideoView : public QWidget {
public:
    explicit VideoView(QWidget *parent=nullptr);
    ~VideoView() override;
    quintptr surfaceId();
protected:
    void paintEvent(QPaintEvent *) override;
private:
    quintptr surface_=0;
    QImage frame_;
};
}
