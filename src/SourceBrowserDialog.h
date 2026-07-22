#pragma once

#include <QDialog>
#include <QStringList>

class QLabel;
class QLineEdit;
class QListWidget;

class SourceBrowserDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit SourceBrowserDialog(const QString& projectRoot, QWidget* parent = nullptr);
    [[nodiscard]] QString selectedFile() const;

private:
    void scanProject();
    void applyFilter(const QString& text);
    void acceptCurrent();

    QString m_projectRoot;
    QStringList m_sourceFiles;
    QLineEdit* m_filterEdit = nullptr;
    QListWidget* m_fileList = nullptr;
    QLabel* m_countLabel = nullptr;
};
