#pragma once

#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <functional>

class QScreen;
class QWidget;
class QWindow;

namespace ScreenLayout {
// Call before QApplication so Qt and native window frames use the same scale.
void enableHighDpiSupport();
QScreen* screenForWidget(const QWidget* widget);
QRect fittedGeometry(const QRect& desired, const QRect& available, int margin = 0);
QRect popupGeometry(const QRect& anchor, const QSize& size, const QRect& available, int gap = 4);
}

// Coalesce native/Qt screen notifications after the DPI transition completes.
// Delay fitting until a title-bar drag ends, so crossing a screen edge stays smooth.
class WindowScreenTracker final : public QObject {
public:
    WindowScreenTracker(QWidget* window, std::function<void()> refresh, bool preserveNormalSize = false);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void trackHandle();
    void trackScreen(QScreen* screen);
    void scheduleRefresh();
    QPointer<QWidget> m_window;
    QPointer<QWindow> m_handle;
    QTimer m_timer;
    QTimer m_sizeTimer;
    std::function<void()> m_refresh;
    QPointer<QScreen> m_lastScreen;
    QSize m_normalSize;
    qreal m_lastDpr = 1;
    bool m_preserveNormalSize = false;
    bool m_restoreSize = false;
};
