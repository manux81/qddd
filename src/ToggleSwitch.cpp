#include "ToggleSwitch.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPaintEvent>

namespace {
constexpr int kTrackWidth = 42;
constexpr int kTrackHeight = 24;
constexpr int kThumbSize = 18;
constexpr int kTextGap = 12;
}

ToggleSwitch::ToggleSwitch(const QString& text, QWidget* parent)
    : QCheckBox(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(kTrackHeight);
}

QSize ToggleSwitch::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int textWidth = text().isEmpty() ? 0 : kTextGap + metrics.horizontalAdvance(text());
    return QSize(kTrackWidth + textWidth, qMax(kTrackHeight, metrics.height()));
}

QSize ToggleSwitch::minimumSizeHint() const
{
    return sizeHint();
}

void ToggleSwitch::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int trackY = (height() - kTrackHeight) / 2;
    const QRectF trackRect(0, trackY, kTrackWidth, kTrackHeight);
    const QColor trackColor = !isEnabled()
        ? QColor(QStringLiteral("#3a3a3a"))
        : (isChecked() ? QColor(QStringLiteral("#007acc"))
                       : QColor(QStringLiteral("#727272")));

    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(trackRect, kTrackHeight / 2.0, kTrackHeight / 2.0);

    const qreal thumbX = isChecked()
        ? kTrackWidth - kThumbSize - 3
        : 3;
    const qreal thumbY = trackY + (kTrackHeight - kThumbSize) / 2.0;
    painter.setBrush(isEnabled() ? Qt::white : QColor(QStringLiteral("#8a8a8a")));
    painter.drawEllipse(QRectF(thumbX, thumbY, kThumbSize, kThumbSize));

    if (!text().isEmpty()) {
        painter.setPen(isEnabled() ? QColor(QStringLiteral("#e6e6e6"))
                                   : QColor(QStringLiteral("#696969")));
        const QRect textRect(kTrackWidth + kTextGap, 0,
                             width() - kTrackWidth - kTextGap, height());
        painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text());
    }

    if (hasFocus()) {
        QPen focusPen(QColor(QStringLiteral("#ffffff")));
        focusPen.setWidth(1);
        painter.setPen(focusPen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(trackRect.adjusted(-1, -1, 1, 1),
                                kTrackHeight / 2.0, kTrackHeight / 2.0);
    }
}
