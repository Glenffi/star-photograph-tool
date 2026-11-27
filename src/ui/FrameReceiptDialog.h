#pragma once

#include <QDialog>

#include <memory>

struct FrameReceipt;

class FrameReceiptDialog final : public QDialog {
    Q_OBJECT
public:
    explicit FrameReceiptDialog(const FrameReceipt& receipt, QWidget* parent = nullptr);
    ~FrameReceiptDialog() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    struct Ui;
    std::unique_ptr<Ui> m_ui;
};
