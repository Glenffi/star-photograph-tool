#include "FrameReceiptDialog.h"

#include "FileDialogUtils.h"
#include "core/RawImageLoader.h"
#include "workers/FrameReceipt.h"

#include <QAbstractTableModel>
#include <QBuffer>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPainter>
#include <QPointer>
#include <QPromise>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTableView>
#include <QThreadPool>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace {

using Outcome = FrameReceipt::Outcome;

enum class Filter { All, Used, Manual, Skipped, NotSubmitted, Unconfirmed };
constexpr std::array kFilters{Filter::All, Filter::Used, Filter::Manual, Filter::Skipped,
                              Filter::NotSubmitted, Filter::Unconfirmed};

Filter group(Outcome outcome) {
    switch (outcome) {
    case Outcome::Used: return Filter::Used;
    case Outcome::ManualExcluded: return Filter::Manual;
    case Outcome::NotSubmitted: return Filter::NotSubmitted;
    case Outcome::QualityRejected:
    case Outcome::RawLoadFailed:
    case Outcome::NoStars:
    case Outcome::AlignmentFailed:
    case Outcome::ResamplingFailed:
    case Outcome::OtherSkipped: return Filter::Skipped;
    case Outcome::Unconfirmed: return Filter::Unconfirmed;
    }
    return Filter::Unconfirmed;
}

QString groupText(Filter filter) {
    switch (filter) {
    case Filter::All: return QCoreApplication::translate("Nebulens", "全部原片");
    case Filter::Used: return QCoreApplication::translate("Nebulens", "已采用");
    case Filter::Manual: return QCoreApplication::translate("Nebulens", "手动排除");
    case Filter::Skipped: return QCoreApplication::translate("Nebulens", "处理跳过");
    case Filter::NotSubmitted: return QCoreApplication::translate("Nebulens", "未提交");
    case Filter::Unconfirmed: return QCoreApplication::translate("Nebulens", "未确认");
    }
    return {};
}

QString alignmentReason(const QStringList& codes, bool tooltip) {
    const auto hasAny = [&](std::initializer_list<const char*> candidates) {
        return std::any_of(candidates.begin(), candidates.end(), [&](const char* code) {
            return codes.contains(QLatin1String(code));
        });
    };
    QStringList reasons;
    if (hasAny({"rms-above-2px", "p95-above-3.5px", "outer-p95-above-4px",
                "cell-rms-above-3px", "cell-trimmed-p95-above-4.5px"})) {
        reasons.append(QCoreApplication::translate("Nebulens", "星点对齐后的偏差过大"));
    }
    if (hasAny({"insufficient-field-coverage", "low-grid-coverage", "insufficient-outer-grid-coverage"})) {
        reasons.append(QCoreApplication::translate("Nebulens", "匹配星点未覆盖足够画面"));
    }
    if (hasAny({"insufficient-matches", "low-match-coverage"})) {
        reasons.append(QCoreApplication::translate("Nebulens", "匹配星点太少"));
    }
    if (!reasons.isEmpty()) return reasons.join(tooltip ? QStringLiteral("\n") : QStringLiteral(" · "));
    if (hasAny({"alignment-quality-gate"})) {
        return QCoreApplication::translate("Nebulens", "星点对齐未达到质量要求");
    }
    if (hasAny({"no-fit-model"})) {
        return QCoreApplication::translate("Nebulens", "星点匹配无法确定对齐方式");
    }
    return QCoreApplication::translate("Nebulens", "无法与参考帧对齐");
}

QString reasonText(const FrameReceipt::Row& row, bool tooltip = false) {
    switch (row.outcome) {
    case Outcome::Used: return {};
    case Outcome::ManualExcluded:
        return QCoreApplication::translate("Nebulens", "处理前手动取消选用");
    case Outcome::NotSubmitted:
        return QCoreApplication::translate("Nebulens", "未交给本次处理");
    case Outcome::QualityRejected:
        return QCoreApplication::translate("Nebulens", "未通过质量筛选");
    case Outcome::RawLoadFailed:
        return QCoreApplication::translate("Nebulens", "无法读取 RAW 原片");
    case Outcome::NoStars:
        return QCoreApplication::translate("Nebulens", "可用于对齐的星点太少");
    case Outcome::AlignmentFailed:
        return alignmentReason(row.reasonCodes, tooltip);
    case Outcome::ResamplingFailed:
        return QCoreApplication::translate("Nebulens", "无法将原片变换到输出画面");
    case Outcome::OtherSkipped:
        return QCoreApplication::translate("Nebulens", "处理时跳过，未记录具体原因");
    case Outcome::Unconfirmed:
        return QCoreApplication::translate("Nebulens", "未确认是否用于成品");
    }
    return {};
}

QString fileLabel(int sourceRow, const QString& path) {
    const QString name = QFileInfo(path).fileName();
    return QStringLiteral("#%1  %2").arg(sourceRow + 1).arg(name.isEmpty()
        ? QCoreApplication::translate("Nebulens", "未记录原片路径") : name);
}

bool hasQualityScore(const FrameReceipt::Row& row) {
    return std::isfinite(row.qualityScore) && row.qualityScore != -1.0;
}

class ReceiptModel final : public QAbstractTableModel {
public:
    enum Column { Source, Result, Reason, Stars, Quality, Output, ColumnCount };

    ReceiptModel(const FrameReceipt& receipt, QObject* parent)
        : QAbstractTableModel(parent), m_receipt(receipt) {
        setFilter(Filter::All);
    }

    const FrameReceipt& receipt() const { return m_receipt; }
    int sourceRow(int row) const { return m_visibleRows.value(row, -1); }
    int viewRow(int source) const { return m_visibleRows.indexOf(source); }

    int preferredRow() const {
        for (int row = 0; row < rowCount(); ++row) {
            if (m_receipt.rows.at(sourceRow(row)).outcome != Outcome::Used) return row;
        }
        return m_visibleRows.isEmpty() ? -1 : 0;
    }

    void setFilter(Filter filter) {
        beginResetModel();
        m_visibleRows.clear();
        for (int row = 0; row < m_receipt.rows.size(); ++row) {
            if (filter == Filter::All || group(m_receipt.rows.at(row).outcome) == filter) {
                m_visibleRows.append(row);
            }
        }
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : m_visibleRows.size();
    }

    int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : ColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
        case Source: return QCoreApplication::translate("Nebulens", "原片");
        case Result: return QCoreApplication::translate("Nebulens", "用片结果");
        case Reason: return QCoreApplication::translate("Nebulens", "原因");
        case Stars: return QCoreApplication::translate("Nebulens", "星点数");
        case Quality: return QCoreApplication::translate("Nebulens", "质量评分");
        case Output: return QCoreApplication::translate("Nebulens", "输出序号");
        }
        return {};
    }

    QVariant data(const QModelIndex& index, int role) const override {
        const int source = sourceRow(index.row());
        if (source < 0) return {};
        const auto& row = m_receipt.rows.at(source);
        const int column = index.column();
        if (column == Source) {
            switch (role) {
            case Qt::UserRole: return row.sourcePath;
            case Qt::UserRole + 1: return static_cast<int>(row.outcome);
            case Qt::UserRole + 2: return row.reference;
            case Qt::UserRole + 3: return row.outputNumber;
            case Qt::ToolTipRole: return row.sourcePath.toHtmlEscaped();
            }
        }
        if (role == Qt::ToolTipRole && column == Reason) return reasonText(row, true);
        if (role == Qt::TextAlignmentRole && column >= Stars) return int(Qt::AlignCenter);
        if (role != Qt::DisplayRole) return {};
        switch (column) {
        case Source:
            return row.reference
                ? QStringLiteral("[%1] %2").arg(QCoreApplication::translate("Nebulens", "参考"),
                                                fileLabel(source, row.sourcePath))
                : fileLabel(source, row.sourcePath);
        case Result: return groupText(group(row.outcome));
        case Reason: return reasonText(row);
        case Stars: return row.detectedStars >= 0 ? QVariant(row.detectedStars) : QVariant();
        case Quality:
            return hasQualityScore(row) ? QLocale().toString(row.qualityScore, 'f', 2) : QString();
        case Output: return row.outputNumber > 0 ? QVariant(row.outputNumber) : QVariant();
        }
        return {};
    }

    void languageChanged() {
        emit headerDataChanged(Qt::Horizontal, 0, ColumnCount - 1);
        if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, ColumnCount - 1));
    }

private:
    const FrameReceipt m_receipt;
    QVector<int> m_visibleRows;
};

enum class PreviewState { NoReference, NoSelection, NoPath, Loading, Missing, Unreadable, DecodeFailed, Ready };

QString previewText(PreviewState state) {
    switch (state) {
    case PreviewState::NoReference: return QCoreApplication::translate("Nebulens", "本次没有参考帧");
    case PreviewState::NoSelection: return QCoreApplication::translate("Nebulens", "未选择原片");
    case PreviewState::NoPath: return QCoreApplication::translate("Nebulens", "未记录原片路径");
    case PreviewState::Loading: return QCoreApplication::translate("Nebulens", "正在读取预览…");
    case PreviewState::Missing: return QCoreApplication::translate("Nebulens", "找不到原片文件");
    case PreviewState::Unreadable: return QCoreApplication::translate("Nebulens", "无法读取原片文件");
    case PreviewState::DecodeFailed: return QCoreApplication::translate("Nebulens", "无法解码原片预览");
    case PreviewState::Ready: return {};
    }
    return {};
}

constexpr int kPreviewEdge = 720;

struct PreviewResult {
    PreviewState state;
    QImage image;
};

QSize previewSize(const QSize& size) {
    if (std::max(size.width(), size.height()) <= kPreviewEdge) return size;
    return size.scaled(kPreviewEdge, kPreviewEdge, Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

QImage readImage(QImageReader& reader) {
    reader.setAutoTransform(true);
    const auto size = reader.size();
    const auto scaled = previewSize(size);
    if (size.isValid() && scaled != size) reader.setScaledSize(scaled);
    QImage image = reader.read();
    const auto target = previewSize(image.size());
    return target == image.size() ? image : image.scaled(target, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

PreviewResult readPreview(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) return {PreviewState::Missing, {}};
    QFile file(path);
    if (!info.isFile() || !file.open(QIODevice::ReadOnly)) return {PreviewState::Unreadable, {}};

    QImage image;
    const auto suffix = info.suffix().toLatin1().toLower();
    constexpr std::array rasterFormats{"png", "jpg", "jpeg", "bmp", "tif", "tiff", "webp"};
    if (std::any_of(rasterFormats.begin(), rasterFormats.end(), [&](const char* format) {
            return suffix == format;
        })) {
        QImageReader reader(&file);
        image = readImage(reader);
    } else {
        file.close();
        RawImageLoader loader;
        RawImageLoader::PreviewData preview;
        if (loader.loadPreview(path, kPreviewEdge, preview)) {
            if (preview.encoding == RawImageLoader::PreviewData::Encoding::Jpeg) {
                QBuffer jpeg;
                jpeg.setData(QByteArray::fromRawData(reinterpret_cast<const char*>(preview.bytes.data()),
                                                    static_cast<qsizetype>(preview.bytes.size())));
                jpeg.open(QIODevice::ReadOnly);
                QImageReader reader(&jpeg, QByteArrayLiteral("jpeg"));
                image = readImage(reader);
            } else {
                const QImage borrowed(preview.bytes.data(), preview.width, preview.height,
                                      preview.width * 3, QImage::Format_RGB888);
                const auto size = previewSize(borrowed.size());
                image = size == borrowed.size() ? borrowed.copy()
                    : borrowed.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
        }
    }
    return {image.isNull() ? PreviewState::DecodeFailed : PreviewState::Ready, std::move(image)};
}

template <typename Result, typename Work>
void runInBackground(QFutureWatcher<Result>& watcher, Work work) {
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    watcher.setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, work = std::move(work)] {
        if (!promise->isCanceled()) {
            try {
                promise->addResult(work());
            } catch (...) {
                // A failed worker finishes without a result; the UI uses its ordinary failure state.
            }
        }
        promise->finish();
    });
}

class PreviewPane final : public QLabel {
public:
    explicit PreviewPane(QWidget* parent) : QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        setBackgroundRole(QPalette::Base);
        setAutoFillBackground(true);
        setMargin(8);
        setMinimumHeight(104);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

        connect(&m_watcher, &QFutureWatcher<PreviewResult>::finished, this, [this] {
            m_running = false;
            if (m_loadingRevision == m_revision) {
                const auto result = m_watcher.future().isResultReadyAt(0)
                    ? m_watcher.result() : PreviewResult{PreviewState::DecodeFailed, {}};
                m_image = result.image;
                m_state = result.state;
                retranslate();
            } else {
                startIfShown();
            }
        });
    }

    ~PreviewPane() override { m_watcher.cancel(); }

    void setSource(const QString& path, PreviewState emptyState) {
        m_path = path;
        m_emptyState = emptyState;
        invalidate();
        startIfShown();
    }

    void retranslate() {
        setText(previewText(m_state));
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QLabel::paintEvent(event);
        const QRect area = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
        if (m_image.isNull() || area.isEmpty()) return;
        QRect target(QPoint(), m_image.size().scaled(area.size(), Qt::KeepAspectRatio));
        target.moveCenter(area.center());
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(target, m_image);
    }

    void showEvent(QShowEvent* event) override {
        QLabel::showEvent(event);
        m_shown = true;
        startIfShown();
    }

    void hideEvent(QHideEvent* event) override {
        m_shown = false;
        invalidate();
        QLabel::hideEvent(event);
    }

private:
    void invalidate() {
        ++m_revision;
        m_image = {};
        m_state = m_path.isEmpty() ? m_emptyState : PreviewState::Loading;
        if (m_running) m_watcher.cancel();
        retranslate();
    }

    void startIfShown() {
        // One read per pane at a time; while it runs, keep only the newest selection.
        if (!m_shown || m_running || m_path.isEmpty()) return;
        m_loadingRevision = m_revision;
        m_running = true;
        runInBackground(m_watcher, [path = m_path] { return readPreview(path); });
    }

    QString m_path;
    PreviewState m_emptyState = PreviewState::NoSelection;
    PreviewState m_state = PreviewState::NoSelection;
    QImage m_image;
    QFutureWatcher<PreviewResult> m_watcher;
    quint64 m_revision = 0;
    quint64 m_loadingRevision = 0;
    bool m_shown = false;
    bool m_running = false;
};

class PreviewTitle final : public QLabel {
public:
    explicit PreviewTitle(QWidget* parent) : QLabel(parent) {
        setTextFormat(Qt::PlainText);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QFont title = font();
        title.setBold(true);
        setFont(title);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(palette().color(foregroundRole()));
        painter.drawText(contentsRect(), Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine,
                         fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

struct SaveResult {
    bool ok;
    QString error;
};

QString saveErrorText(const QString& code) {
    if (code == QLatin1String("invalid-receipt-path")) {
        return QCoreApplication::translate("Nebulens", "请选择可写入的本地 JSON 文件。");
    }
    if (code == QLatin1String("receipt-unavailable")) {
        return QCoreApplication::translate("Nebulens", "本次没有可导出的交付回执。");
    }
    return QCoreApplication::translate("Nebulens", "请检查所选位置的写入权限和剩余空间。");
}

}  // namespace

struct FrameReceiptDialog::Ui : QObject {
    Ui(FrameReceiptDialog* dialog, const FrameReceipt& receipt) : owner(dialog) {
        owner->setObjectName(QStringLiteral("frameReceiptDialog"));
        auto* layout = new QVBoxLayout(owner);
        layout->setContentsMargins(16, 12, 16, 12);
        layout->setSpacing(8);

        auto* comparison = new QHBoxLayout;
        comparison->setSpacing(12);
        reference = addPreview(comparison, "receiptReferenceTitle", "receiptReferencePreview");
        selected = addPreview(comparison, "receiptSelectedTitle", "receiptSelectedPreview");
        layout->addLayout(comparison, 5);

        auto* controls = new QHBoxLayout;
        filter = new QComboBox(owner);
        filter->setObjectName(QStringLiteral("receiptFrameFilter"));
        filter->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        for (const auto mode : kFilters) filter->addItem(groupText(mode), static_cast<int>(mode));
        controls->addWidget(filter);
        controls->addStretch();
        filterCount = new QLabel(owner);
        filterCount->setObjectName(QStringLiteral("receiptFrameCount"));
        controls->addWidget(filterCount);
        layout->addLayout(controls);

        model = new ReceiptModel(receipt, owner);
        table = new QTableView(owner);
        table->setObjectName(QStringLiteral("receiptFrameTable"));
        table->setModel(model);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setAlternatingRowColors(true);
        table->setWordWrap(false);
        table->setShowGrid(false);
        table->verticalHeader()->hide();
        auto* header = table->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::ResizeToContents);
        header->setResizeContentsPrecision(0);
        header->setSectionResizeMode(ReceiptModel::Source, QHeaderView::Stretch);
        header->setSectionResizeMode(ReceiptModel::Reason, QHeaderView::Stretch);
        header->setSectionResizeMode(ReceiptModel::Result, QHeaderView::Fixed);
        table->setColumnHidden(ReceiptModel::Stars,
            std::none_of(receipt.rows.cbegin(), receipt.rows.cend(), [](const FrameReceipt::Row& row) {
                return row.detectedStars >= 0;
            }));
        table->setColumnHidden(ReceiptModel::Quality,
            std::none_of(receipt.rows.cbegin(), receipt.rows.cend(), hasQualityScore));
        table->setColumnHidden(ReceiptModel::Output, !receipt.sequence);
        layout->addWidget(table, 6);

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, owner);
        save = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
        save->setObjectName(QStringLiteral("receiptSaveDiagnostics"));
        save->setAutoDefault(false);
        save->setEnabled(!receipt.rows.isEmpty());
        close = buttons->button(QDialogButtonBox::Close);
        close->setAutoDefault(false);
        layout->addWidget(buttons);

        for (int row = 0; row < receipt.rows.size(); ++row) {
            if (receipt.rows.at(row).reference) {
                referenceRow = row;
                break;
            }
        }
        setSource(reference, referenceRow, PreviewState::NoReference);
        QObject::connect(filter, &QComboBox::currentIndexChanged, this, [this] { filterRows(); });
        QObject::connect(table->selectionModel(), &QItemSelectionModel::currentChanged, this,
                         [this] { syncSelection(); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, owner, &QDialog::reject);
        QObject::connect(save, &QPushButton::clicked, this, [this] { saveDiagnostics(); });
        QObject::connect(&saver, &QFutureWatcher<SaveResult>::finished, this, [this] {
            save->setEnabled(true);
            const auto result = saver.future().isResultReadyAt(0) ? saver.result() : SaveResult{false, {}};
            if (result.ok || !owner->isVisible()) return;
            auto* message = new QMessageBox(QMessageBox::Warning, owner->windowTitle(),
                QCoreApplication::translate("Nebulens", "无法保存匿名诊断"), QMessageBox::Ok, owner);
            message->setTextFormat(Qt::PlainText);
            message->setInformativeText(saveErrorText(result.error));
            message->setAttribute(Qt::WA_DeleteOnClose);
            message->open();
        });

        chooseRow(model->preferredRow());
        retranslate();
        owner->resize(920, 540);
    }

    struct Pane {
        PreviewTitle* title;
        PreviewPane* image;
    };

    Pane addPreview(QHBoxLayout* comparison, const char* titleName, const char* imageName) {
        auto* column = new QVBoxLayout;
        column->setSpacing(4);
        auto* title = new PreviewTitle(owner);
        title->setObjectName(QString::fromLatin1(titleName));
        auto* image = new PreviewPane(owner);
        image->setObjectName(QString::fromLatin1(imageName));
        column->addWidget(title);
        column->addWidget(image, 1);
        comparison->addLayout(column, 1);
        return {title, image};
    }

    void setSource(Pane pane, int source, PreviewState absent) {
        if (source < 0) pane.image->setSource({}, absent);
        else pane.image->setSource(model->receipt().rows.at(source).sourcePath, PreviewState::NoPath);
    }

    void updateTitle(Pane pane, int source, const QString& heading) {
        QString text = heading;
        if (source >= 0) {
            text = QStringLiteral("%1 · %2").arg(heading,
                fileLabel(source, model->receipt().rows.at(source).sourcePath));
        }
        pane.title->setText(text);
        pane.title->setToolTip(text.toHtmlEscaped());
        pane.image->setAccessibleName(text);
    }

    void syncSelection() {
        const int source = model->sourceRow(table->currentIndex().row());
        if (source == selectedRow) return;
        selectedRow = source;
        setSource(selected, selectedRow, PreviewState::NoSelection);
        updateTitle(selected, selectedRow, QCoreApplication::translate("Nebulens", "所选原片"));
    }

    void chooseRow(int row) {
        if (row >= 0) {
            const auto index = model->index(row, ReceiptModel::Source);
            table->setCurrentIndex(index);
            table->scrollTo(index);
        }
        syncSelection();
    }

    void filterRows() {
        const int previous = selectedRow;
        {
            const QSignalBlocker blocker(table->selectionModel());
            model->setFilter(static_cast<Filter>(filter->currentData().toInt()));
        }
        const int row = model->viewRow(previous);
        chooseRow(row >= 0 ? row : model->preferredRow());
        updateCount();
    }

    void updateCount() {
        filterCount->setText(QCoreApplication::translate("Nebulens", "显示 %1 / %2 张")
                             .arg(model->rowCount()).arg(model->receipt().rows.size()));
    }

    void retranslate() {
        owner->setWindowTitle(QCoreApplication::translate("Nebulens", "用片明细"));
        table->setAccessibleName(owner->windowTitle());
        filter->setAccessibleName(QCoreApplication::translate("Nebulens", "按用片结果筛选"));
        for (int i = 0; i < static_cast<int>(kFilters.size()); ++i) filter->setItemText(i, groupText(kFilters[i]));
        model->languageChanged();
        auto* header = table->horizontalHeader();
        int width = header->sectionSizeHint(ReceiptModel::Result);
        for (const auto mode : kFilters) {
            if (mode != Filter::All) width = std::max(width, table->fontMetrics().horizontalAdvance(groupText(mode)) + 24);
        }
        header->resizeSection(ReceiptModel::Result, width);
        updateCount();
        updateTitle(reference, referenceRow, QCoreApplication::translate("Nebulens", "参考原片"));
        updateTitle(selected, selectedRow, QCoreApplication::translate("Nebulens", "所选原片"));
        reference.image->retranslate();
        selected.image->retranslate();
        save->setText(QCoreApplication::translate("Nebulens", "导出匿名诊断…"));
        save->setToolTip(QCoreApplication::translate("Nebulens", "只保存本地匿名文件，不包含照片、文件名或路径。"));
        close->setText(QCoreApplication::translate("Nebulens", "关闭"));
    }

    void saveDiagnostics() {
        const QPointer<FrameReceiptDialog> guard(owner);
        const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        const QDir directory(QFileInfo(documents).isDir() ? documents : QDir::homePath());
        const QString path = FileDialogUtils::getSaveFileName(owner,
            QCoreApplication::translate("Nebulens", "导出匿名诊断"),
            directory.filePath(QStringLiteral("frame-receipt.json")),
            QCoreApplication::translate("Nebulens", "JSON 文件 (*.json)"), QStringLiteral("json"));
        if (!guard || !guard->isVisible() || path.isEmpty()) return;

        save->setEnabled(false);
        runInBackground(saver, [receipt = model->receipt(), path] {
            SaveResult result;
            result.ok = receipt.saveAnonymous(path, result.error);
            return result;
        });
    }

    FrameReceiptDialog* const owner;
    ReceiptModel* model = nullptr;
    QTableView* table = nullptr;
    QComboBox* filter = nullptr;
    QLabel* filterCount = nullptr;
    QPushButton* save = nullptr;
    QPushButton* close = nullptr;
    Pane reference{};
    Pane selected{};
    int referenceRow = -1;
    int selectedRow = -2;
    QFutureWatcher<SaveResult> saver;
};

FrameReceiptDialog::FrameReceiptDialog(const FrameReceipt& receipt, QWidget* parent)
    : QDialog(parent) {
    m_ui = std::make_unique<Ui>(this, receipt);
}

FrameReceiptDialog::~FrameReceiptDialog() = default;

void FrameReceiptDialog::changeEvent(QEvent* event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange && m_ui) m_ui->retranslate();
}
