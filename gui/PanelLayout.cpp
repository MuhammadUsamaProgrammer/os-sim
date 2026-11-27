// PanelLayout.cpp - responsive configuration panels and role-aware splitters.
#include "PanelLayout.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSettings>
#include <QStyle>
#include <algorithm>
#include <numeric>
#include "Style.h"

// ================================================================ responsive policies

void ui::makeResponsive(QWidget* root) {
    for (QFormLayout* f : root->findChildren<QFormLayout*>()) {
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);          // label above field when narrow
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        f->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (root->layout()) if (auto* f = qobject_cast<QFormLayout*>(root->layout())) {
        f->setRowWrapPolicy(QFormLayout::WrapLongRows);
        f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    }
    const int chars = 26;
    for (QLabel* l : root->findChildren<QLabel*>()) {
        if (l->wordWrap() || l->text().size() < chars) continue;
        l->setWordWrap(true);                                    // long text wraps instead of widening the panel
        l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }
    for (QComboBox* c : root->findChildren<QComboBox*>()) {
        if (c->property("ossimResponsive").toBool()) continue;
        c->setProperty("ossimResponsive", true);
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(8);
        c->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto sync = [c] {
            c->setToolTip(c->currentText());
            int w = 0;
            for (int i = 0; i < c->count(); ++i) w = std::max(w, c->fontMetrics().horizontalAdvance(c->itemText(i)));
            c->view()->setMinimumWidth(w + 40);                  // the pop-up shows the whole text
        };
        QObject::connect(c, &QComboBox::currentTextChanged, c, sync);
        QObject::connect(c->model(), &QAbstractItemModel::rowsInserted, c, sync);
        sync();
    }
}

QList<PanelSplitter*> ui::panelSplitters(QWidget* root) {
    return root ? root->findChildren<PanelSplitter*>() : QList<PanelSplitter*>{};
}

// ================================================================ ConfigPanel

// ================================================================ FitScrollArea

FitScrollArea::FitScrollArea(QWidget* content, QWidget* parent) : QScrollArea(parent) {
    setWidget(content);
    setWidgetResizable(false);                                   // sizing is done by fit()
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);         // only when the content cannot shrink more
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->installEventFilter(this);
    content->installEventFilter(this);
}

void FitScrollArea::fit() {
    QWidget* w = widget();
    if (!w || fitting_) return;
    fitting_ = true;
    QSize vp = viewport()->size();
    QSize mn = w->minimumSizeHint().expandedTo(w->minimumSize());
    int width = std::max(vp.width(), mn.width());
    int minH = mn.height();
    if (QLayout* l = w->layout(); l && l->hasHeightForWidth()) minH = std::max(minH, l->totalMinimumHeightForWidth(width));
    QSize target(width, std::max(vp.height(), minH));
    if (w->size() != target) w->resize(target);
    fitting_ = false;
}

bool FitScrollArea::eventFilter(QObject* o, QEvent* e) {
    if ((o == viewport() && e->type() == QEvent::Resize) || (o == widget() && e->type() == QEvent::LayoutRequest))
        QTimer::singleShot(0, this, &FitScrollArea::fit);        // after the layout has settled
    return QScrollArea::eventFilter(o, e);
}

void FitScrollArea::resizeEvent(QResizeEvent* e) {
    QScrollArea::resizeEvent(e);
    fit();
}

// ================================================================ ConfigPanel

ConfigPanel::ConfigPanel(QWidget* content, QWidget* parent) : FitScrollArea(content, parent) {
    setObjectName("configPanel");
    setAccessibleName("Configuration panel");
    content->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    ui::makeResponsive(content);
}

ConfigPanel* ConfigPanel::wrap(QWidget* content, const QString& name) {
    auto* p = new ConfigPanel(content);
    p->setObjectName(name);
    p->setAccessibleName(name);
    return p;
}

QSize ConfigPanel::sizeHint() const {
    int sb = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    QSize c = widget() ? widget()->sizeHint() : QSize(200, 200);
    return QSize(c.width() + sb + 4, std::min(c.height(), 600));
}

QSize ConfigPanel::minimumSizeHint() const {
    int sb = style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    return QSize(fontMetrics().averageCharWidth() * 18 + sb, fontMetrics().height() * 4);
}

// ================================================================ PageScroll

PageScroll::PageScroll(QWidget* page, QWidget* parent) : FitScrollArea(page, parent) {
    setObjectName("pageScroll");
}

QSize PageScroll::minimumSizeHint() const {
    return QSize(fontMetrics().averageCharWidth() * 40, fontMetrics().height() * 12);
}

QSize PageScroll::sizeHint() const { return widget() ? widget()->sizeHint() : QSize(800, 600); }

// ================================================================ PanelHandle

PanelHandle::PanelHandle(Qt::Orientation o, QSplitter* parent) : QSplitterHandle(o, parent) {
    setToolTip("Drag to resize - double-click to restore the default layout");
    setAttribute(Qt::WA_Hover);
}

void PanelHandle::enterEvent(QEnterEvent*) { hover_ = true; update(); }
void PanelHandle::leaveEvent(QEvent*) { hover_ = false; update(); }

void PanelHandle::paintEvent(QPaintEvent*) {
    QPainter p(this);
    QColor c = hover_ ? palette().highlight().color() : palette().mid().color();
    if (!hover_) c.setAlpha(150);
    QRect r = rect();
    if (orientation() == Qt::Horizontal) {
        p.fillRect(QRect(r.center().x() - (hover_ ? 1 : 0), r.top(), hover_ ? 3 : 1, r.height()), c);
        for (int k = -2; k <= 2; ++k) p.fillRect(QRect(r.center().x() - 1, r.center().y() + k * 6 - 1, 3, 3), c);   // grip
    } else {
        p.fillRect(QRect(r.left(), r.center().y() - (hover_ ? 1 : 0), r.width(), hover_ ? 3 : 1), c);
        for (int k = -2; k <= 2; ++k) p.fillRect(QRect(r.center().x() + k * 6 - 1, r.center().y() - 1, 3, 3), c);
    }
}

void PanelHandle::mouseDoubleClickEvent(QMouseEvent*) {
    if (auto* s = qobject_cast<PanelSplitter*>(splitter())) s->resetLayout();
}

// ================================================================ PanelSplitter

PanelSplitter::PanelSplitter(Qt::Orientation o, const QString& id, QWidget* parent) : QSplitter(o, parent), id_(id) {
    setObjectName("panelSplitter:" + id);
    setHandleWidth(std::max(7, fontMetrics().height() / 2));
    setChildrenCollapsible(false);
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, &PanelSplitter::save);
    connect(this, &QSplitter::splitterMoved, this, [this] { if (applied_ && !focus_) saveTimer_.start(); });
}

QSplitterHandle* PanelSplitter::createHandle() { return new PanelHandle(orientation(), this); }

int PanelSplitter::addPanel(QWidget* w, Role role, int weight, double maxShare) {
    addWidget(w);
    int i = count() - 1;
    roles_ << role;
    weights_ << weight;
    maxShare_ << maxShare;
    setCollapsible(i, role == Config || role == Inspector);      // side panels may be collapsed, the view never
    setStretchFactor(i, role == Center ? 3 : role == Detail ? weight : 0);
    return i;
}

bool PanelSplitter::hasPanel(Role r) const { return roles_.contains(r); }
QWidget* PanelSplitter::panel(Role r) const {
    int i = roles_.indexOf(r);
    return i >= 0 ? widget(i) : nullptr;
}

// Side panels get their preferred size (capped), the centre / detail panels share the rest.
QList<int> PanelSplitter::defaultSizes() const {
    const bool horiz = orientation() == Qt::Horizontal;
    int total = (horiz ? width() : height()) - handleWidth() * std::max(0, count() - 1);
    if (total <= 0) total = horiz ? 1000 : 600;
    QList<int> s;
    int used = 0, weightSum = 0;
    for (int i = 0; i < count(); ++i) {
        QWidget* w = widget(i);
        if (w->isHidden()) { s << 0; continue; }
        if (roles_[i] == Config || roles_[i] == Inspector) {
            int pref = horiz ? w->sizeHint().width() : w->sizeHint().height();
            int mn = horiz ? w->minimumSizeHint().width() : w->minimumSizeHint().height();
            int v = std::clamp(pref, mn, std::max(mn, int(total * maxShare_[i])));
            s << v;
            used += v;
        } else {
            s << -1;
            weightSum += weights_[i];
        }
    }
    int rest = std::max(0, total - used);
    // the centre must keep at least its minimum: shrink the side panels if needed
    int centerMin = 0;
    for (int i = 0; i < count(); ++i)
        if (s[i] < 0) centerMin += horiz ? widget(i)->minimumSizeHint().width() : widget(i)->minimumSizeHint().height();
    if (rest < centerMin && used > 0) {
        double f = std::max(0.0, double(total - centerMin)) / used;
        used = 0;
        for (int i = 0; i < count(); ++i)
            if (s[i] > 0) {
                int mn = horiz ? widget(i)->minimumSizeHint().width() : widget(i)->minimumSizeHint().height();
                s[i] = std::max(mn, int(s[i] * f));
                used += s[i];
            }
        rest = std::max(0, total - used);
    }
    for (int i = 0; i < count(); ++i)
        if (s[i] < 0) s[i] = weightSum > 0 ? rest * weights_[i] / weightSum : rest;
    return s;
}

void PanelSplitter::resetLayout() {
    if (focus_) setFocusView(false);
    for (int i = 0; i < count(); ++i) widget(i)->setVisible(true);
    if (!id_.isEmpty() && !ui::selfTestMode()) QSettings("OS-Sim", "OS-Sim").remove("layout/" + id_);
    setSizes(defaultSizes());
}

void PanelSplitter::save() {
    if (id_.isEmpty() || ui::selfTestMode()) return;
    QVariantList v;
    for (int x : sizes()) v << x;
    QSettings("OS-Sim", "OS-Sim").setValue("layout/" + id_, v);
}

// Restores saved sizes only when they still make sense for the current window.
bool PanelSplitter::restore() {
    if (id_.isEmpty() || ui::selfTestMode()) return false;
    QVariantList v = QSettings("OS-Sim", "OS-Sim").value("layout/" + id_).toList();
    if (v.size() != count()) return false;
    const bool horiz = orientation() == Qt::Horizontal;
    int total = horiz ? width() : height();
    QList<int> s;
    int sum = 0;
    for (int i = 0; i < count(); ++i) {
        int x = v[i].toInt();
        if (x < 0) return false;
        int mn = horiz ? widget(i)->minimumSizeHint().width() : widget(i)->minimumSizeHint().height();
        if (roles_[i] == Center && x < mn) return false;          // the view must stay usable
        s << x;
        sum += x;
    }
    if (sum < total / 2 || sum > total * 2) return false;         // saved for a very different window
    setSizes(s);
    return true;
}

void PanelSplitter::showEvent(QShowEvent* e) {
    QSplitter::showEvent(e);
    if (!applied_ && (orientation() == Qt::Horizontal ? width() : height()) > 50) {
        applied_ = true;
        if (!restore()) setSizes(defaultSizes());
    }
}

void PanelSplitter::resizeEvent(QResizeEvent* e) {
    QSplitter::resizeEvent(e);
    if (!applied_ && isVisible() && (orientation() == Qt::Horizontal ? width() : height()) > 50) {
        applied_ = true;
        if (!restore()) setSizes(defaultSizes());
    }
}

void PanelSplitter::setPanelVisible(Role r, bool on) {
    for (int i = 0; i < count(); ++i)
        if (roles_[i] == r) widget(i)->setVisible(on);
}

bool PanelSplitter::panelVisible(Role r) const {
    for (int i = 0; i < count(); ++i)
        if (roles_[i] == r) return !widget(i)->isHidden();
    return false;
}

void PanelSplitter::setFocusView(bool on) {
    if (on == focus_) return;
    if (on) {
        hiddenBeforeFocus_.clear();
        for (int i = 0; i < count(); ++i) {
            hiddenBeforeFocus_ << widget(i)->isHidden();
            if (roles_[i] != Center) widget(i)->setVisible(false);
        }
    } else {
        for (int i = 0; i < count() && i < hiddenBeforeFocus_.size(); ++i) widget(i)->setVisible(!hiddenBeforeFocus_[i]);
    }
    focus_ = on;
}
