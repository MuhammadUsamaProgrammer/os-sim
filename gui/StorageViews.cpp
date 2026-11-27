// StorageViews.cpp - disk block grid (see StorageViews.h).
#include "StorageViews.h"
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include "ModuleKit.h"
#include "Style.h"
#include "vfs.h"

DiskGridView::DiskGridView(QWidget* parent) : QWidget(parent) {
    anim_.setStartValue(1.0);
    anim_.setEndValue(0.0);
    anim_.setDuration(700);
    connect(&anim_, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { flashT_ = v.toDouble(); update(); });
    setMouseTracking(false);
}

void DiskGridView::setFs(const VirtualFs* fs) {
    fs_ = fs;
    lastGen_.clear();
    highlight_ = -1;
    evidence_.clear();
    refresh();
}

void DiskGridView::refresh() {
    flash_.clear();
    if (fs_) {
        const auto& b = fs_->blocks();
        bool any = false;
        flash_.assign(b.size(), false);
        if (lastGen_.size() == b.size())
            for (size_t i = 0; i < b.size(); ++i) if (b[i].gen != lastGen_[i]) { flash_[i] = true; any = true; }
        lastGen_.clear();
        for (const Block& x : b) lastGen_.push_back(x.gen);
        if (any) { anim_.stop(); anim_.start(); }
    }
    update();
}

void DiskGridView::setHighlight(int inode) { highlight_ = inode; update(); }
void DiskGridView::setEvidence(const std::map<int, bool>& e) { evidence_ = e; update(); }

int DiskGridView::cols() const {
    if (!fs_) return 1;
    int n = fs_->blockCount();
    double ratio = std::max(0.5, double(width()) / std::max(1, height() - 40));
    int c = (int)std::ceil(std::sqrt(n * ratio));
    return std::max(4, std::min(n, c));
}

QRectF DiskGridView::cell(int b) const {
    int c = cols();
    int rows = (fs_->blockCount() + c - 1) / c;
    double top = 22, bottom = 34;
    double cw = (width() - 8.0) / c, ch = std::max(8.0, (height() - top - bottom) / rows);
    double s = std::min(cw, ch * 1.6);
    return QRectF(4 + (b % c) * cw, top + (b / c) * ch, std::min(cw, s) - 2, ch - 2);
}

int DiskGridView::blockAt(const QPoint& p) const {
    if (!fs_) return -1;
    for (int b = 0; b < fs_->blockCount(); ++b) if (cell(b).contains(p)) return b;
    return -1;
}

void DiskGridView::mousePressEvent(QMouseEvent* e) {
    int b = blockAt(e->pos());
    if (b >= 0) emit blockClicked(b);
}

void DiskGridView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QColor ink = palette().color(QPalette::WindowText);
    p.setPen(ink);
    QFont f = font();
    p.drawText(QRectF(4, 0, width() - 8, 20), Qt::AlignLeft | Qt::AlignVCenter,
               title_.isEmpty() ? QString("Virtual disk") : title_);
    if (!fs_) { p.drawText(rect(), Qt::AlignCenter, "No virtual disk"); return; }
    const auto& blocks = fs_->blocks();
    const Inode* hl = fs_->inode(highlight_);
    std::map<int, int> logical;
    if (hl) for (size_t i = 0; i < hl->blocks.size(); ++i) logical[hl->blocks[i]] = (int)i;
    f.setPointSizeF(std::max(6.0, f.pointSizeF() * 0.8));
    p.setFont(f);
    for (int b = 0; b < (int)blocks.size(); ++b) {
        const Block& x = blocks[b];
        QRectF r = cell(b);
        QColor bg;
        QString label;
        switch (x.kind) {
            case BlockKind::Meta: bg = QColor("#5f6368"); label = b == 0 ? "SB" : b == 1 ? "BM" : "IT"; break;
            case BlockKind::Free: {
                bool old = false;
                for (uint8_t c : x.bytes) if (c) { old = true; break; }
                bg = old ? QColor("#ece6d6") : QColor("#fafafa");
                break;
            }
            case BlockKind::Data: bg = ui::pidColor(x.owner).lighter(125); break;
            case BlockKind::Index: bg = ui::pidColor(x.owner).darker(115); label = "I"; break;
        }
        p.setPen(QPen(QColor("#9aa0a6"), 1));
        p.setBrush(bg);
        p.drawRect(r);
        if (!flash_.empty() && flash_[b] && flashT_ > 0) {
            QColor fl = QColor("#ffd54f");
            fl.setAlphaF(0.85 * flashT_);
            p.fillRect(r, fl);
        }
        if (hl && (logical.count(b) || b == hl->indexBlock)) {
            p.setPen(QPen(ink, 2.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(1, 1, -1, -1));
            label = b == hl->indexBlock ? "I" : QString::number(logical[b]);
        }
        auto ev = evidence_.find(b);
        if (ev != evidence_.end()) {
            QColor c = ev->second ? ui::hitColor() : ui::faultColor();
            p.setPen(QPen(c, 3));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(2, 2, -2, -2));
            if (!ev->second) { p.drawLine(r.topLeft() + QPointF(4, 4), r.bottomRight() - QPointF(4, 4)); }
        }
        p.setPen(x.kind == BlockKind::Meta ? QColor(Qt::white) : ui::darkInk());
        if (r.height() > 14 && r.width() > 16) {
            QString t = label.isEmpty() ? QString::number(b) : label;
            if (label.isEmpty()) p.setPen(QColor("#80868b"));
            p.drawText(r, Qt::AlignCenter, t);
        }
    }
    // strategy structure of the selected file
    if (hl && hl->blocks.size() > 0) {
        if (fs_->strategy() == AllocStrategy::Linked) {
            p.setPen(QPen(ink, 1.6));
            for (size_t i = 0; i + 1 < hl->blocks.size(); ++i) {
                QPointF a = cell(hl->blocks[i]).center(), c = cell(hl->blocks[i + 1]).center();
                p.drawLine(a, c);
                QLineF l(c, a);
                l.setLength(7);
                QLineF w1 = l, w2 = l;
                w1.setAngle(l.angle() + 25);
                w2.setAngle(l.angle() - 25);
                p.drawLine(w1);
                p.drawLine(w2);
            }
        } else if (fs_->strategy() == AllocStrategy::Indexed && hl->indexBlock >= 0) {
            p.setPen(QPen(ink, 1.1, Qt::DashLine));
            QPointF a = cell(hl->indexBlock).center();
            for (int b : hl->blocks) p.drawLine(a, cell(b).center());
        }
    }
    // legend
    double y = height() - 30;
    struct L { QColor c; const char* t; };
    const L items[] = {{QColor("#5f6368"), "metadata"}, {QColor("#fafafa"), "free"}, {QColor("#ece6d6"), "free, old bytes kept"},
                       {ui::pidColor(3).lighter(125), "data (colour = inode)"}, {ui::pidColor(3).darker(115), "index"}};
    double x = 4;
    QFontMetrics fm(f);
    for (const L& it : items) {
        double w = 12 + fm.horizontalAdvance(it.t) + 12;
        if (x + w > width()) { x = 4; y += 14; }
        p.setPen(QColor("#9aa0a6"));
        p.setBrush(it.c);
        p.drawRect(QRectF(x, y + 2, 10, 10));
        p.setPen(ink);
        p.drawText(QPointF(x + 14, y + 11), it.t);
        x += w;
    }
}
