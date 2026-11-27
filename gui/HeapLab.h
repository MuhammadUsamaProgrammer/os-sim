// HeapLab.h - Memory > Heap Laboratory (Phase 3): application-level heap allocation in a bounded
// arena (core/heap.h), distinct from the OS-level contiguous allocation / paging pages.
//   HeapMapView  address-ordered heap map: header (dark), payload (colour per handle), padding
//                (hatched), free blocks (light) with free-list arrows; bytes-per-row zoom; blocks that
//                changed in the last step flash, a realloc move is drawn as an arrow. Buddy mode adds
//                the buddy tree (one row per block size).
//   HeapLabPage  configuration (family, fit, capacity, alignment, preset / script, manual operations),
//                playback over the deterministic session (Previous = undo view, a new operation after
//                Previous starts a new branch), block inspector with payload bytes, metrics, history,
//                integrity check and outstanding allocations (potential leaks).
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include "heap.h"

class QComboBox; class QSpinBox; class QLineEdit; class QPlainTextEdit; class QTableWidget; class QLabel; class QSlider;
class PlaybackBar; class ModuleHeader;

class HeapMapView : public QWidget {
    Q_OBJECT
public:
    explicit HeapMapView(QWidget* parent = nullptr);
    void setStep(const HeapStep* cur, const HeapStep* prev, HeapFamily family, int capacity, int animMs);
    void setBytesPerRow(int b) { bytesPerRow_ = b; update(); }
    void setSelected(int offset) { selected_ = offset; update(); }
    QSize sizeHint() const override { return QSize(620, 380); }
    QSize minimumSizeHint() const override { return QSize(240, 160); }
signals:
    void blockClicked(int offset);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    QRectF byteRect(int from, int to, int row) const;
    QList<QRectF> spanRects(int from, int to) const;
    const HeapStep* cur_ = nullptr;
    std::vector<HeapBlock> prevBlocks_;
    HeapFamily family_ = HeapFamily::FreeList;
    int capacity_ = 1024, bytesPerRow_ = 256, selected_ = -1;
    double t_ = 1;
    QVariantAnimation anim_;
};

class HeapLabPage : public QWidget {
    Q_OBJECT
public:
    explicit HeapLabPage(QWidget* parent = nullptr);
    bool loadPreset(int i);
    bool rebuild();                                   // from the configuration + script
    bool applyOp(const QString& line);                // manual operation (appended at the current step)
    bool undo();                                      // removes the last operation
    void setFit(FitAlgo a);
    void setFamily(HeapFamily f);
    const HeapSession& session() const { return session_; }
    const HeapStep* current() const;
    PlaybackBar* playback() const { return bar_; }
    void selectBlock(int offset);
    QString reportText() const;
signals:
    void logMessage(const QString& line);
private:
    void onStep(int pos, bool forward);
    void showInspector();
    bool readConfig(HeapConfig& c, QString& err) const;
    ModuleHeader* header_;
    QComboBox *family_, *fit_, *align_, *preset_, *op_;
    QSpinBox *capacity_, *size_, *count_, *zoom_;
    QLineEdit *name_, *text_;
    QPlainTextEdit *script_, *bytes_;
    QLabel *presetText_, *blockInfo_, *metrics_;
    QTableWidget *history_, *leaks_;
    QPlainTextEdit* integrity_;
    HeapMapView* map_;
    PlaybackBar* bar_;
    HeapSession session_;
    std::vector<std::string> lines_;
    int selected_ = -1;
    bool loading_ = false;
};
