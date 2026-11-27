// MemAdvTab.h - the "Memory" area: the existing page-replacement tab (unchanged) plus the
// Phase 2 sub-tabs: Allocation & Fragmentation, Paging & Address Translation, Segmentation,
// Demand Paging, Swapping and Thrashing. All values come from core/memadv.h.
#pragma once
#include <QTabWidget>
#include <optional>
#include <vector>
#include "memadv.h"

class QComboBox; class QSpinBox; class QLineEdit; class QCheckBox; class QTableWidget; class QListWidget; class QLabel;
class PlaybackBar; class ModuleHeader; class AllocBarView; class TranslationView; class DemandPagingView; class SwapView;
class ThrashView; class MemoryTab;

class MemAdvArea : public QTabWidget {
    Q_OBJECT
public:
    explicit MemAdvArea(MemoryTab* pageReplacement, QWidget* parent = nullptr);
    MemoryTab* pageReplacement() const { return mem_; }
    // ---- actions (public for the self-test)
    bool loadAllocPreset(int i);
    bool rebuildAlloc();
    const std::vector<AllocResult>& allocResults() const { return alloc_; }
    bool loadPagingPreset(int i);
    bool rebuildPaging();
    const std::optional<XlateResult>& pagingResult() const { return pageX_; }
    bool loadSegPreset(int i);
    bool rebuildSeg();
    const std::optional<XlateResult>& segResult() const { return segX_; }
    bool loadDemandPreset(int i);
    bool rebuildDemand();
    const std::vector<DpResult>& demandResults() const { return dpAll_; }
    bool rebuildSwap();
    const std::optional<SwapResult>& swapResult() const { return swap_; }
    bool loadThrashPreset(int i);
    bool rebuildThrash();
    const std::optional<ThrashResult>& thrashA() const { return thA_; }
    const std::optional<ThrashResult>& thrashB() const { return thB_; }
    PlaybackBar* allocBar() const { return allocBar_; }
    PlaybackBar* pagingBar() const { return pageBar_; }
    PlaybackBar* demandBar() const { return dpBar_; }
    QString reportText() const;
    QString csvSection() const;
    class HeapLabPage* heapLab() const { return heap_; }   // Phase 3: Heap Laboratory
signals:
    void logMessage(const QString& line);
private:
    class HeapLabPage* heap_ = nullptr;
    QWidget* buildAlloc();
    QWidget* buildPaging();
    QWidget* buildSeg();
    QWidget* buildDemand();
    QWidget* buildSwap();
    QWidget* buildThrash();
    void showAlloc(int step, bool fwd);
    void showPaging(int step, bool fwd);
    void showSeg(int step, bool fwd);
    void showDemand(int step, bool fwd);
    void fillPageTable(const PagingConfig& c);
    bool readPaging(PagingConfig& c, QString& err) const;
    void fillSegTable(const std::vector<SegmentDesc>& s);
    bool readSegs(std::vector<SegmentDesc>& s, QString& err) const;

    MemoryTab* mem_;
    bool loading_ = false;
    // allocation
    ModuleHeader* allocHdr_;
    QComboBox *allocMode_, *allocFocus_, *allocPreset_, *blBlock_;
    QLineEdit *allocMem_, *allocOps_;
    QSpinBox *allocOs_, *blOffset_;
    QLabel *blResult_, *allocExplain_;
    QTableWidget* allocMetrics_;
    AllocBarView* allocView_;
    PlaybackBar* allocBar_;
    std::vector<AllocResult> alloc_;
    // paging
    ModuleHeader* pageHdr_;
    QComboBox *pageSize_, *pagePreset_;
    QSpinBox *pages_, *frames_, *addr_;
    QCheckBox *pageWrite_, *pageShared_;
    QTableWidget* pageTable_;
    QListWidget* pageSteps_;
    TranslationView* pageView_;
    PlaybackBar* pageBar_;
    PagingConfig pageCfg_;
    std::optional<XlateResult> pageX_;
    // segmentation
    ModuleHeader* segHdr_;
    QComboBox *segPreset_, *segAccess_;
    QSpinBox *segPhys_, *segNo_, *segOff_;
    QTableWidget* segTable_;
    QListWidget* segSteps_;
    TranslationView* segView_;
    PlaybackBar* segBar_;
    std::vector<SegmentDesc> segs_;
    std::optional<XlateResult> segX_;
    // demand paging
    ModuleHeader* dpHdr_;
    QComboBox *dpAlgo_, *dpPreset_;
    QLineEdit* dpRefs_;
    QSpinBox *dpFrames_, *dpAccess_, *dpPageIn_, *dpWb_;
    QListWidget* dpActions_;
    QTableWidget* dpStats_;
    DemandPagingView* dpView_;
    PlaybackBar* dpBar_;
    std::vector<DpResult> dpAll_;
    int dpIndex_ = 0;
    // swapping
    ModuleHeader* swHdr_;
    QSpinBox* swCap_;
    QLineEdit *swProcs_, *swOrder_;
    SwapView* swView_;
    PlaybackBar* swBar_;
    std::optional<SwapResult> swap_;
    // thrashing
    ModuleHeader* thHdr_;
    QComboBox *thMode_, *thAlgo_, *thPreset_;
    QSpinBox *thPages_, *thRepeats_, *thLen_, *thLocality_, *thSeed_, *thFramesA_, *thFramesB_, *thWindow_, *thWs_,
             *mpFrames_, *mpWs_, *mpDegree_, *mpRefs_, *mpFault_;
    ThrashView* thView_;
    PlaybackBar* thBar_;
    std::optional<ThrashResult> thA_, thB_;
    std::vector<MultiprogPoint> mp_;
};
