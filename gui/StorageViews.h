// StorageViews.h - QPainter view of the virtual disk of core/vfs.h (display only).
//   DiskGridView: one cell per block, coloured by kind (metadata / index / data per inode / free),
//   free blocks that still hold old bytes are tinted; the selected file shows its logical block
//   numbers plus its strategy structure (next-pointer arrows for linked allocation, index lines
//   for indexed allocation); recovery evidence is drawn as green (intact) / red (overwritten)
//   frames. Blocks whose generation changed since the last update flash briefly (real change).
#pragma once
#include <QVariantAnimation>
#include <QWidget>
#include <map>
#include <vector>

class VirtualFs;

class DiskGridView : public QWidget {
    Q_OBJECT
public:
    explicit DiskGridView(QWidget* parent = nullptr);
    void setFs(const VirtualFs* fs);              // nullptr = empty; call refresh() after changes
    void refresh();                               // repaint and flash changed blocks
    void setHighlight(int inode);                 // -1 = none
    void setEvidence(const std::map<int, bool>& intactByBlock);   // block -> intact?
    void setTitle(const QString& t) { title_ = t; update(); }
    int blockAt(const QPoint& p) const;
    QSize sizeHint() const override { return QSize(460, 280); }
    QSize minimumSizeHint() const override { return QSize(200, 140); }
signals:
    void blockClicked(int block);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    QRectF cell(int b) const;
    int cols() const;
    const VirtualFs* fs_ = nullptr;
    int highlight_ = -1;
    std::map<int, bool> evidence_;
    std::vector<int> lastGen_;
    std::vector<bool> flash_;
    double flashT_ = 0;
    QVariantAnimation anim_;
    QString title_;
};
