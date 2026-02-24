#include "CustomCalendarWidget.h"
#include <QSettings>
#include <QStyle>
#include <QAbstractItemModel>
#include <QContextMenuEvent>
#include <QEvent>
#include <QPainter>
#include <QTimer>

//#include "CustomDateDelegate.h"

CustomCalendarWidget::CustomCalendarWidget(QWidget* parent) : QCalendarWidget(parent), m_tableView(nullptr) {
    setupEventFilters();
    // QCalendarWidget internals may not be fully ready in ctor.
    // Retry once in next event loop to ensure right-click binding works.
    QTimer::singleShot(0, this, &CustomCalendarWidget::setupEventFilters);
}

void CustomCalendarWidget::setupEventFilters() {
    if (m_tableView) {
        return;
    }

    m_tableView = this->findChild<QTableView*>();
    if (m_tableView) {
        QWidget* viewport = m_tableView->viewport();
        viewport->installEventFilter(this);
    }
}

bool CustomCalendarWidget::eventFilter(QObject* watched, QEvent* event) {
    if (m_tableView && watched == m_tableView->viewport() && event->type() == QEvent::ContextMenu) {
        QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
        showContextMenu(contextEvent->pos());
        return true;
    }

    return QCalendarWidget::eventFilter(watched, event);
}

void CustomCalendarWidget::showEvent(QShowEvent* event) {
    QCalendarWidget::showEvent(event);
    setupEventFilters();
}

void CustomCalendarWidget::paintCell(QPainter* painter, const QRect& rect, const QDate& date) const
{
    __super::paintCell(painter, rect, date);



    if (date == selectedDate())
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 145, 255));

        painter->drawRoundedRect(rect.x(), rect.y() + 3, rect.width(), rect.height() - 6, 3, 3);
        painter->setPen(QColor(255, 255, 255));

        painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));
        painter->restore();
    }
    else if (date == QDate::currentDate())
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 161, 255));
        painter->drawRoundedRect(rect.x(), rect.y() + 3, rect.width(), rect.height() - 6, 3, 3);
        painter->setBrush(QColor(255, 255, 255));
        painter->drawRoundedRect(rect.x() + 1, rect.y() + 4, rect.width() - 2, rect.height() - 8, 2, 2);
        painter->setPen(QColor(0, 161, 255));

        painter->drawText(rect, Qt::AlignCenter, QString::number(date.day()));

        painter->restore();
    }
    painter->save();
    QFont font = painter->font();
    font.setPointSize(7);
    painter->setFont(font);
    painter->setPen(QPen(Qt::blue));

    const QVariantMap dayData = m_data.value(date);
    QRect eventRectDown = rect.adjusted(2, rect.height() / 2, -2, -2);
    QRect eventRectUp = rect.adjusted(2, -32, -2, -2);
    painter->drawText(eventRectUp, Qt::AlignCenter, dayData.value("arrivalTime").toString());
    painter->drawText(eventRectDown, Qt::AlignCenter, dayData.value("departureTime").toString());
    painter->restore();

}

void CustomCalendarWidget::setCustomData(const QDate& date, const QVariantMap& value)
{
    m_data[date] = value;
    updateCell(date); // 触发paintCell
}

void CustomCalendarWidget::clearCustomData(const QDate& date)
{
    m_data.remove(date);
    updateCell(date);
}


void CustomCalendarWidget::showContextMenu(const QPoint& pos) {
    // 获取点击位置对应的日期
    QDate clickedDate = dateAt(pos);
    if (!clickedDate.isValid()) {
        return;
    }

    // 检查该日期是否有记录
    QSettings settings;
    QString key = clickedDate.toString("yyyy-MM-dd");
    if (!settings.contains(key + "/arrival")) {
        return; // 没有记录，不显示菜单
    }

    // 创建右键菜单
    QMenu contextMenu(this);
    QAction* deleteAction = contextMenu.addAction(QString("删除 %1 的记录").arg(clickedDate.toString("yyyy-MM-dd")));
    deleteAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    // 显示菜单并处理选择
    QAction* selectedAction = contextMenu.exec(m_tableView->viewport()->mapToGlobal(pos));
    if (selectedAction == deleteAction) {
        emit deleteRequested(clickedDate);
    }
}

QDate CustomCalendarWidget::dateAt(const QPoint& pos) {
    if (!m_tableView) {
        return QDate();
    }

    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) {
        return QDate();
    }

    QAbstractItemModel* model = m_tableView->model();
    if (!model) {
        return QDate();
    }

    const QVariant dateVariant = model->data(index, Qt::UserRole);
    if (dateVariant.canConvert<QDate>()) {
        const QDate roleDate = dateVariant.toDate();
        if (roleDate.isValid()) {
            return roleDate;
        }
    }

    int year = yearShown();
    int month = monthShown();
    bool ok = false;
    int day = model->data(index, Qt::DisplayRole).toInt(&ok);
    if (!ok) {
        return QDate();
    }

    return QDate(year, month, day);
}
