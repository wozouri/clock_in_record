#include "CustomCalendarWidget.h"
#include "Data/AttendanceStorage.h"
#include <ElaMenu.h>

#include <QAbstractItemModel>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QRubberBand>
#include <algorithm>
#include <QTimer>


CustomCalendarWidget::CustomCalendarWidget(QWidget* parent) : QCalendarWidget(parent), m_tableView(nullptr) {
    setSelectionMode(QCalendarWidget::NoSelection);
    setAutoFillBackground(true);

    QPalette calendarPalette = palette();
    calendarPalette.setColor(QPalette::Window, QColor(QStringLiteral("#ffffff")));
    calendarPalette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    calendarPalette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#ffffff")));
    calendarPalette.setColor(QPalette::Text, QColor(QStringLiteral("#1f2937")));
    calendarPalette.setColor(QPalette::WindowText, QColor(QStringLiteral("#1f2937")));
    calendarPalette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#1f2937")));
    calendarPalette.setColor(QPalette::Highlight, QColor(QStringLiteral("#e7f1fb")));
    calendarPalette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#1f2937")));
    setPalette(calendarPalette);
    setStyleSheet(QStringLiteral(
        "QCalendarWidget { background: #ffffff; color: #1f2937; border: 1px solid #dce5ee; }"
        "QCalendarWidget QWidget#qt_calendar_navigationbar { background: #ffffff; border-bottom: 1px solid #dce5ee; }"
        "QCalendarWidget QToolButton { color: #29415f; background: transparent; border: 0; padding: 4px; }"
        "QCalendarWidget QToolButton:hover { background: #eaf1f7; border-radius: 4px; }"
        "QCalendarWidget QAbstractItemView { background: #ffffff; color: #1f2937;"
        " selection-background-color: #e7f1fb; selection-color: #1f2937; gridline-color: #dce5ee; }"
        "QCalendarWidget QMenu { background: #ffffff; color: #223550; border: 1px solid #cfdce8;"
        " border-radius: 5px; padding: 4px; }"
        "QCalendarWidget QMenu::item { padding: 6px 20px 6px 10px; border-radius: 3px; }"
        "QCalendarWidget QMenu::item:selected { background: #eaf3fb; color: #1769aa; }"));
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
        m_tableView->setPalette(palette());
        m_tableView->viewport()->setPalette(palette());
        m_tableView->viewport()->setAutoFillBackground(true);
        QWidget* viewport = m_tableView->viewport();
        m_selectionRubberBand = new QRubberBand(QRubberBand::Rectangle, viewport);
        m_selectionRubberBand->setStyleSheet(QStringLiteral(
            "QRubberBand { border: 1px solid #5b9bd5; background: rgba(91, 155, 213, 48); }"));
        m_selectionRubberBand->hide();
        viewport->installEventFilter(this);
    }
}

bool CustomCalendarWidget::eventFilter(QObject* watched, QEvent* event) {
    if (m_tableView && watched == m_tableView->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                m_dragStartDate = dateAt(mouseEvent->pos());
                m_dragStartPosition = mouseEvent->pos();
                m_dragModifiers = mouseEvent->modifiers();
                m_dragSelectionActive = false;
                if (m_selectionRubberBand) {
                    m_selectionRubberBand->hide();
                }
                return m_dragStartDate.isValid();
            }
        }
        else if (event->type() == QEvent::MouseMove) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (!m_dragStartDate.isValid() || !(mouseEvent->buttons() & Qt::LeftButton)) {
                return QCalendarWidget::eventFilter(watched, event);
            }

            if (!m_dragSelectionActive
                && (mouseEvent->pos() - m_dragStartPosition).manhattanLength() >= QApplication::startDragDistance()) {
                m_dragSelectionActive = true;
                m_selectionRubberBand->show();
            }
            if (m_dragSelectionActive) {
                const QRect selectionRect(m_dragStartPosition, mouseEvent->pos());
                m_selectionRubberBand->setGeometry(selectionRect.normalized().adjusted(0, 0, 1, 1));
            }
            return true;
        }
        else if (event->type() == QEvent::MouseButtonRelease) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && m_dragStartDate.isValid()) {
                if (m_dragSelectionActive) {
                    finishRubberBandSelection();
                }
                else {
                    selectDateFromClick(m_dragStartDate, m_dragModifiers);
                }
                m_dragStartDate = QDate();
                m_dragSelectionActive = false;
                return true;
            }
        }
        else if (event->type() == QEvent::MouseButtonDblClick) {
            QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QDate clickedDate = dateAt(mouseEvent->pos());
                if (clickedDate.isValid()) {
                    m_dragStartDate = QDate();
                    m_dragSelectionActive = false;
                    if (m_selectionRubberBand) {
                        m_selectionRubberBand->hide();
                    }
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
    const QVariantMap dayData = m_data.value(date);
    if (!dayData.isEmpty()) {
        painter->save();
        QFont font = painter->font();
        font.setPointSize(7);
        painter->setFont(font);
        painter->setPen(QPen(Qt::blue));

        const int lineHeight = qMin(14, qMax(10, rect.height() / 4));
        const QRect arrivalRect(rect.left() + 2, rect.top() + 4,
            rect.width() - 4, lineHeight);
        const QRect departureRect(rect.left() + 2, rect.bottom() - lineHeight - 3,
            rect.width() - 4, lineHeight);
        painter->drawText(arrivalRect, Qt::AlignCenter, dayData.value("arrivalTime").toString());
        painter->drawText(departureRect, Qt::AlignCenter, dayData.value("departureTime").toString());
        painter->restore();
    }

    if (dayData.value("hasNote").toBool()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor("#f59e0b"));
        painter->drawEllipse(QPoint(rect.right() - 8, rect.top() + 8), 3, 3);
        painter->restore();
    }

}

void CustomCalendarWidget::selectDateFromClick(const QDate& date, Qt::KeyboardModifiers modifiers)
{
    if (!date.isValid()) {
        return;
    }

    if ((modifiers & Qt::ShiftModifier) && m_selectionAnchorDate.isValid()) {
        selectDateRange(date, m_selectionAnchorDate, modifiers & Qt::ControlModifier);
    }
    else if (modifiers & Qt::ControlModifier) {
        toggleDateSelection(date);
    }
    else {
        setSingleSelection(date);
    }
}

QList<QDate> CustomCalendarWidget::datesInRect(const QRect& rect) const
{
    QList<QDate> dates;
    if (!m_tableView || !m_tableView->model()) {
        return dates;
    }

    QAbstractItemModel* model = m_tableView->model();
    for (int row = 0; row < model->rowCount(); ++row) {
        for (int column = 0; column < model->columnCount(); ++column) {
            const QModelIndex index = model->index(row, column);
            const QRect cellRect = m_tableView->visualRect(index);
            if (!cellRect.isEmpty() && rect.intersects(cellRect)) {
                const QDate date = dateAt(cellRect.center());
                if (date.isValid() && !dates.contains(date)) {
                    dates.append(date);
                }
            }
        }
    }
    return dates;
}

void CustomCalendarWidget::finishRubberBandSelection()
{
    const QRect selectionRect = m_selectionRubberBand->geometry();
    m_selectionRubberBand->hide();

    const QList<QDate> rubberBandDates = datesInRect(selectionRect);
    if (rubberBandDates.isEmpty()) {
        return;
    }

    QList<QDate> targetDates = (m_dragModifiers & Qt::ControlModifier)
        ? m_selectedDates
        : QList<QDate>();
    for (const QDate& date : rubberBandDates) {
        if (!targetDates.contains(date)) {
            targetDates.append(date);
        }
    }
    setSelectedDates(targetDates);
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
    for (const QDate& date : targetDates) {
        if (AttendanceStorage::hasArrivalRecord(date)) {
            deletableDates.append(date);
        }
    }

    if (deletableDates.isEmpty()) {
        return;
    }

    // Keep the business menu out of the calendar's QMenu stylesheet scope so
    // ElaMenu can render with its own theme.
    ElaMenu contextMenu;
    contextMenu.setMenuItemHeight(34);

    QAction* copyAction = nullptr;
    if (targetDates.size() == 1 && AttendanceStorage::hasArrivalRecord(targetDates.first())) {
        copyAction = contextMenu.addElaIconAction(ElaIconType::Clipboard, QStringLiteral("复制"));
        contextMenu.addSeparator();
    }

    QAction* deleteAction = nullptr;
    if (deletableDates.size() == 1) {
        deleteAction = contextMenu.addElaIconAction(ElaIconType::TrashCan,
            QStringLiteral("删除 %1 的记录").arg(deletableDates.first().toString(QStringLiteral("yyyy-MM-dd"))));
    }
    else {
        deleteAction = contextMenu.addElaIconAction(ElaIconType::TrashCan,
            QStringLiteral("删除选中的 %1 条记录").arg(deletableDates.size()));
    }

    QAction* selectedAction = contextMenu.exec(m_tableView->viewport()->mapToGlobal(pos));
    if (copyAction && selectedAction == copyAction) {
        emit copyRequested(targetDates.first());
    }
    else if (deleteAction && selectedAction == deleteAction) {
        emit deleteRequested(deletableDates);
    }
}

QDate CustomCalendarWidget::dateAt(const QPoint& pos) const {
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
