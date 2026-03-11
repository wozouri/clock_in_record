#include "CustomCalendarWidget.h"
#include <QSettings>
#include <QStyle>
#include <QAbstractItemModel>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <QTimer>

//#include "CustomDateDelegate.h"

CustomCalendarWidget::CustomCalendarWidget(QWidget* parent) : QCalendarWidget(parent), m_tableView(nullptr) {
    setSelectionMode(QCalendarWidget::NoSelection);
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
    if (m_tableView && watched == m_tableView->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QDate clickedDate = dateAt(mouseEvent->pos());
                if (clickedDate.isValid()) {
                    if ((mouseEvent->modifiers() & Qt::ShiftModifier) && m_selectionAnchorDate.isValid()) {
                        selectDateRange(clickedDate,
                            m_selectionAnchorDate,
                            mouseEvent->modifiers() & Qt::ControlModifier);
                    }
                    else if (mouseEvent->modifiers() & Qt::ControlModifier) {
                        toggleDateSelection(clickedDate);
                    }
                    else {
                        setSingleSelection(clickedDate);
                    }
                }
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QDate clickedDate = dateAt(mouseEvent->pos());
                if (clickedDate.isValid()) {
                    setSingleSelection(clickedDate);
                    emit dateDoubleClicked(clickedDate);
                }
            }
        }
        else if (event->type() == QEvent::ContextMenu) {
            QContextMenuEvent* contextEvent = static_cast<QContextMenuEvent*>(event);
            showContextMenu(contextEvent->pos());
            return true;
        }
    }

    return QCalendarWidget::eventFilter(watched, event);
}

void CustomCalendarWidget::showEvent(QShowEvent* event) {
    QCalendarWidget::showEvent(event);
    setupEventFilters();
}

void CustomCalendarWidget::paintCell(QPainter* painter, const QRect& rect, const QDate& date) const
{
    QCalendarWidget::paintCell(painter, rect, date);

    if (isDateSelected(date)) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(0, 145, 255, 215));

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

QList<QDate> CustomCalendarWidget::selectedDates() const
{
    QList<QDate> dates = m_selectedDates;
    std::sort(dates.begin(), dates.end());
    return dates;
}

void CustomCalendarWidget::setSelectedDates(const QList<QDate>& dates)
{
    QList<QDate> normalizedDates;
    for (const QDate& date : dates) {
        if (date.isValid() && !normalizedDates.contains(date)) {
            normalizedDates.append(date);
        }
    }
    std::sort(normalizedDates.begin(), normalizedDates.end());

    if (normalizedDates == selectedDates()) {
        return;
    }

    QList<QDate> datesToUpdate = m_selectedDates;
    for (const QDate& date : normalizedDates) {
        if (!datesToUpdate.contains(date)) {
            datesToUpdate.append(date);
        }
    }

    m_selectedDates = normalizedDates;
    m_selectionAnchorDate = m_selectedDates.isEmpty() ? QDate() : m_selectedDates.last();
    refreshSelection(datesToUpdate);
    emit selectionChanged();
}

void CustomCalendarWidget::clearSelection()
{
    if (m_selectedDates.isEmpty()) {
        return;
    }

    const QList<QDate> previousDates = m_selectedDates;
    m_selectedDates.clear();
    m_selectionAnchorDate = QDate();
    refreshSelection(previousDates);
    emit selectionChanged();
}

void CustomCalendarWidget::selectDateRange(const QDate& start, const QDate& end, bool additive)
{
    if (!start.isValid() || !end.isValid()) {
        return;
    }

    QList<QDate> rangeDates;
    const QDate firstDate = (start <= end) ? start : end;
    const QDate lastDate = (start <= end) ? end : start;
    for (QDate date = firstDate; date <= lastDate; date = date.addDays(1)) {
        rangeDates.append(date);
    }

    QList<QDate> targetDates = additive ? m_selectedDates : QList<QDate>();
    for (const QDate& date : rangeDates) {
        if (!targetDates.contains(date)) {
            targetDates.append(date);
        }
    }

    setSelectedDates(targetDates);
    m_selectionAnchorDate = end;
}

void CustomCalendarWidget::setSingleSelection(const QDate& date)
{
    QList<QDate> datesToUpdate = m_selectedDates;
    if (!datesToUpdate.contains(date)) {
        datesToUpdate.append(date);
    }

    const bool selectionUnchanged = (m_selectedDates.size() == 1 && m_selectedDates.first() == date);
    if (selectionUnchanged) {
        return;
    }

    m_selectedDates.clear();
    m_selectedDates.append(date);
    m_selectionAnchorDate = date;
    refreshSelection(datesToUpdate);
    emit selectionChanged();
}

void CustomCalendarWidget::toggleDateSelection(const QDate& date)
{
    if (!date.isValid()) {
        return;
    }

    if (m_selectedDates.contains(date)) {
        m_selectedDates.removeAll(date);
        if (m_selectionAnchorDate == date) {
            m_selectionAnchorDate = m_selectedDates.isEmpty() ? QDate() : m_selectedDates.last();
        }
    }
    else {
        m_selectedDates.append(date);
        m_selectionAnchorDate = date;
    }

    refreshSelection(QList<QDate>{ date });
    emit selectionChanged();
}

bool CustomCalendarWidget::isDateSelected(const QDate& date) const
{
    return m_selectedDates.contains(date);
}

void CustomCalendarWidget::refreshSelection(const QList<QDate>& datesToUpdate)
{
    for (const QDate& date : datesToUpdate) {
        updateCell(date);
    }
}


void CustomCalendarWidget::showContextMenu(const QPoint& pos) {
    // 获取点击位置对应的日期
    QDate clickedDate = dateAt(pos);
    if (!clickedDate.isValid()) {
        return;
    }

    QList<QDate> targetDates;
    if (m_selectedDates.size() > 1 && m_selectedDates.contains(clickedDate)) {
        targetDates = selectedDates();
    }
    else {
        targetDates.append(clickedDate);
    }

    QList<QDate> deletableDates;
    QSettings settings;
    for (const QDate& date : targetDates) {
        const QString key = date.toString("yyyy-MM-dd");
        if (settings.contains(key + "/arrival")) {
            deletableDates.append(date);
        }
    }

    if (deletableDates.isEmpty()) {
        return;
    }

    // 创建右键菜单
    QMenu contextMenu(this);
    QAction* deleteAction = nullptr;
    if (deletableDates.size() == 1) {
        deleteAction = contextMenu.addAction(QString("删除 %1 的记录").arg(deletableDates.first().toString("yyyy-MM-dd")));
    }
    else {
        deleteAction = contextMenu.addAction(QString("删除选中的 %1 条记录").arg(deletableDates.size()));
    }
    deleteAction->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    // 显示菜单并处理选择
    QAction* selectedAction = contextMenu.exec(m_tableView->viewport()->mapToGlobal(pos));
    if (selectedAction == deleteAction) {
        emit deleteRequested(deletableDates);
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
