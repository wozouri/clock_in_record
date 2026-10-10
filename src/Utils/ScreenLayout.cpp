#include "ScreenLayout.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWidget>
#include <QWindow>

#ifdef Q_OS_WIN
#include <Windows.h>
#endif

void ScreenLayout::enableHighDpiSupport()
{
#ifdef Q_OS_WIN
    if (const auto user32 = GetModuleHandleW(L"user32.dll")) {
        using SetContext = BOOL(WINAPI*)(HANDLE);
        const auto setContext = reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (!setContext || !setContext(reinterpret_cast<HANDLE>(-4))) SetProcessDPIAware();
    }
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
#endif
}

QScreen* ScreenLayout::screenForWidget(const QWidget* widget)
{
    const auto* window = widget ? widget->window() : nullptr;
    // Qt's virtual desktop can contain gaps when monitors use different scales.
    // The native window's screen is more reliable than screenAt(window center).
    if (window && window->windowHandle() && window->windowHandle()->screen())
        return window->windowHandle()->screen();
    if (window) {
        if (auto* screen = QGuiApplication::screenAt(window->frameGeometry().center())) return screen;
    }
    return QGuiApplication::primaryScreen();
}

QRect ScreenLayout::fittedGeometry(const QRect& desired, const QRect& available, int margin)
{
    const QRect bounds = available.adjusted(margin, margin, -margin, -margin);
    if (bounds.isEmpty()) return desired;
    const QSize size(qMin(desired.width(), bounds.width()), qMin(desired.height(), bounds.height()));
    const QPoint origin(qBound(bounds.left(), desired.left(), bounds.right() - size.width() + 1),
        qBound(bounds.top(), desired.top(), bounds.bottom() - size.height() + 1));
    return QRect(origin, size);
}

QRect ScreenLayout::popupGeometry(const QRect& anchor, const QSize& size, const QRect& available, int gap)
{
    QRect target(QPoint(anchor.right() - size.width() + 1, anchor.bottom() + 1 + gap), size);
    if (target.bottom() > available.bottom()) target.moveTop(anchor.top() - gap - size.height());
    return fittedGeometry(target, available);
}

WindowScreenTracker::WindowScreenTracker(QWidget* window, std::function<void()> refresh, bool preserveNormalSize)
    : QObject(window), m_window(window), m_refresh(std::move(refresh))
    , m_normalSize(window->size()), m_preserveNormalSize(preserveNormalSize)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(80);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (!m_window || !m_window->isVisible() || m_window->isMinimized()) return;
        bool dragging = QApplication::mouseButtons() & Qt::LeftButton;
#ifdef Q_OS_WIN
        dragging = dragging || (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
#endif
        if (dragging) {
            m_timer.start();
            return;
        }
        // Qt 5 + native Ela frames can reapply the DPI scale during a monitor
        // transition. Keep the last stable logical size, then fit it to the new
        // work area. Ordinary user resizing still updates the remembered size.
        if (m_preserveNormalSize && m_restoreSize && !m_window->isMaximized() && !m_window->isFullScreen())
            m_window->resize(m_normalSize);
        m_refresh();
        m_lastScreen = ScreenLayout::screenForWidget(m_window);
        m_lastDpr = m_window->devicePixelRatioF();
        m_restoreSize = false;
        // Fitting to a small monitor is automatic; do not overwrite the user's
        // preferred size, so it can be restored on a larger monitor later.
        m_sizeTimer.stop();
    });
    m_sizeTimer.setSingleShot(true);
    connect(&m_sizeTimer, &QTimer::timeout, this, [this] {
        if (m_window && !m_timer.isActive() && !m_restoreSize && !m_window->isMaximized()
            && !m_window->isMinimized() && !m_window->isFullScreen()) {
            m_normalSize = m_window->size();
            m_lastDpr = m_window->devicePixelRatioF();
        }
    });
    window->installEventFilter(this);
    for (auto* screen : QGuiApplication::screens()) trackScreen(screen);
    connect(qApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        trackScreen(screen);
        scheduleRefresh();
    });
    connect(qApp, &QGuiApplication::screenRemoved, this, [this] { scheduleRefresh(); });
}

void WindowScreenTracker::trackHandle()
{
    auto* handle = m_window ? m_window->windowHandle() : nullptr;
    if (handle == m_handle) return;
    if (m_handle) disconnect(m_handle, nullptr, this, nullptr);
    m_handle = handle;
    if (handle) {
        if (!m_lastScreen) {
            m_lastScreen = handle->screen();
            m_lastDpr = handle->devicePixelRatio();
        }
        connect(handle, &QWindow::screenChanged, this, [this] {
            m_restoreSize = true;
            scheduleRefresh();
        });
    }
}

void WindowScreenTracker::trackScreen(QScreen* screen)
{
    connect(screen, &QScreen::availableGeometryChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QScreen::geometryChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { scheduleRefresh(); });
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { scheduleRefresh(); });
}

void WindowScreenTracker::scheduleRefresh()
{
    if (m_window && (ScreenLayout::screenForWidget(m_window) != m_lastScreen
        || !qFuzzyCompare(m_window->devicePixelRatioF(), m_lastDpr))) m_restoreSize = true;
    m_timer.start();
}

bool WindowScreenTracker::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_window && event->type() == QEvent::Move && m_window->isVisible()
        && !m_window->isMaximized() && !m_window->isFullScreen()) {
        if (auto* screen = ScreenLayout::screenForWidget(m_window)) {
            if (!screen->availableGeometry().contains(m_window->frameGeometry())) scheduleRefresh();
        }
    }
    if (watched == m_window && event->type() == QEvent::Resize) {
        m_sizeTimer.start(0);
        if (!m_window->isMaximized() && !m_window->isFullScreen()) {
            if (auto* screen = ScreenLayout::screenForWidget(m_window)) {
                const int margin = m_preserveNormalSize ? 16 : 24;
                const QSize limit = screen->availableGeometry().size() - QSize(margin,margin);
                if (m_window->width() > limit.width() || m_window->height() > limit.height()) scheduleRefresh();
            }
        }
    }
    if (watched == m_window && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange
        || event->type() == QEvent::WindowStateChange || event->type() == QEvent::ScreenChangeInternal)) {
        trackHandle();
        scheduleRefresh();
    }
    return QObject::eventFilter(watched, event);
}
