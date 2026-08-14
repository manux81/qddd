#pragma once

#include <QCheckBox>

class ToggleSwitch final : public QCheckBox
{
public:
    explicit ToggleSwitch(const QString& text = QString(), QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};
