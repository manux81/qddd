#include "SourceBrowserDialog.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

SourceBrowserDialog::SourceBrowserDialog(const QString& projectRoot, QWidget* parent)
    : QDialog(parent)
    , m_projectRoot(QDir::cleanPath(projectRoot))
{
    setWindowTitle(tr("Project Source Files"));
    resize(760, 560);

    auto* layout = new QVBoxLayout(this);
    auto* rootLabel = new QLabel(tr("Project: %1").arg(m_projectRoot), this);
    rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rootLabel->setWordWrap(true);
    layout->addWidget(rootLabel);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText(tr("Filter by file name or path..."));
    m_filterEdit->setClearButtonEnabled(true);
    layout->addWidget(m_filterEdit);

    m_fileList = new QListWidget(this);
    m_fileList->setAlternatingRowColors(true);
    layout->addWidget(m_fileList, 1);

    m_countLabel = new QLabel(this);
    layout->addWidget(m_countLabel);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Open | QDialogButtonBox::Cancel, this);
    auto* external = buttons->addButton(tr("Open External File..."),
                                        QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);

    connect(m_filterEdit, &QLineEdit::textChanged,
            this, &SourceBrowserDialog::applyFilter);
    connect(m_filterEdit, &QLineEdit::returnPressed,
            this, &SourceBrowserDialog::acceptCurrent);
    connect(m_fileList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { acceptCurrent(); });
    connect(buttons, &QDialogButtonBox::accepted,
            this, &SourceBrowserDialog::acceptCurrent);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
    connect(external, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, tr("Open Source File"), m_projectRoot,
            tr("Source files (*.c *.cc *.cpp *.cxx *.h *.hh *.hpp *.s *.S *.asm *.inc);;All files (*)"));
        if (file.isEmpty())
            return;
        m_fileList->clear();
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(file), m_fileList);
        item->setData(Qt::UserRole, QFileInfo(file).absoluteFilePath());
        m_fileList->setCurrentItem(item);
        accept();
    });

    scanProject();
    m_filterEdit->setFocus();
}

QString SourceBrowserDialog::selectedFile() const
{
    const auto* item = m_fileList->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void SourceBrowserDialog::scanProject()
{
    static const QStringList extensions = {
        QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("cpp"),
        QStringLiteral("cxx"), QStringLiteral("h"), QStringLiteral("hh"),
        QStringLiteral("hpp"), QStringLiteral("s"), QStringLiteral("asm"),
        QStringLiteral("inc")
    };
    static const QStringList ignoredDirectories = {
        QStringLiteral(".git"), QStringLiteral("build"), QStringLiteral("dist"),
        QStringLiteral("node_modules"), QStringLiteral(".cache")
    };

    QDir root(m_projectRoot);
    QDirIterator iterator(m_projectRoot, QDir::Files | QDir::NoSymLinks,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString absolute = iterator.next();
        const QString relative = root.relativeFilePath(absolute);
        const QStringList pathParts = relative.split(QLatin1Char('/'));
        bool ignored = false;
        for (const QString& directory : ignoredDirectories) {
            if (pathParts.contains(directory, Qt::CaseInsensitive)) {
                ignored = true;
                break;
            }
        }
        if (ignored)
            continue;
        const QString suffix = QFileInfo(absolute).suffix();
        if (!extensions.contains(suffix, Qt::CaseInsensitive))
            continue;
        m_sourceFiles.append(QFileInfo(absolute).absoluteFilePath());
    }
    std::sort(m_sourceFiles.begin(), m_sourceFiles.end(),
              [&root](const QString& lhs, const QString& rhs) {
        return root.relativeFilePath(lhs).compare(root.relativeFilePath(rhs),
                                                  Qt::CaseInsensitive) < 0;
    });
    applyFilter(QString());
}

void SourceBrowserDialog::applyFilter(const QString& text)
{
    m_fileList->clear();
    const QString needle = text.trimmed();
    QDir root(m_projectRoot);
    for (const QString& absolute : m_sourceFiles) {
        const QString relative = root.relativeFilePath(absolute);
        if (!needle.isEmpty() && !relative.contains(needle, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(QDir::toNativeSeparators(relative), m_fileList);
        item->setData(Qt::UserRole, absolute);
        item->setToolTip(absolute);
    }
    if (m_fileList->count() > 0)
        m_fileList->setCurrentRow(0);
    m_countLabel->setText(tr("%1 of %2 source files")
                          .arg(m_fileList->count()).arg(m_sourceFiles.size()));
}

void SourceBrowserDialog::acceptCurrent()
{
    if (m_fileList->currentItem())
        accept();
}
