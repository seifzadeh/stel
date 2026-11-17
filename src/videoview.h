#pragma once
#include <QImage>
#include <QWidget>
#include <QPointer>
#include <linphone/linphonecore.h>
namespace stel {
// Software display sink: SDK threads publish the latest frame, Qt paints it.
void installQtVideoRenderer(LinphoneCore *);
class VideoView : public QWidget {
    Q_OBJECT
public:
    explicit VideoView(QWidget *parent=nullptr);
    ~VideoView() override;
    quintptr surfaceId();
    void setFrameSource(VideoView *);
    void clearFrame();
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
private:
    quintptr surface_=0;
    QImage frame_;
    QPointer<VideoView> frameSource_;
};
}
