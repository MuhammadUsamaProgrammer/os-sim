// ModuleKit.cpp - playback bar, pseudocode view and module header.
#include "ModuleKit.h"
#include <QFontDatabase>
#include <QGroupBox>
#include <QScrollArea>
#include <QSplitter>
#include "PanelLayout.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QStyle>
#include <QToolButton>
#include "SampleRegistry.h"

// ================================================================ PlaybackBar

PlaybackBar::PlaybackBar(QWidget* parent) : QWidget(parent) {
    timer_ = new QTimer(this);
    auto mk = [this](const QString& text, const QString& tip) {
        auto* b = new QPushButton(text);
        b->setToolTip(tip);
        b->setAccessibleName(text);
        return b;
    };
    run_ = mk("Run", "Play the simulation step by step (restarts if at the end)");
    pause_ = mk("Pause", "Pause / resume playback");
    prev_ = mk("◀ Prev", "Previous step: restores the earlier snapshot");
    next_ = mk("Next ▶", "Next step");
    stop_ = mk("Stop", "Stop playback and return to the initial state (step 0)");
    reset_ = mk("Reset", "Rebuild the simulation from the current inputs");
    scrub_ = new QSlider(Qt::Horizontal);
    scrub_->setToolTip("Timeline: drag to any step");
    scrub_->setAccessibleName("Timeline scrubber");
    speed_ = new QSlider(Qt::Horizontal);
    speed_->setRange(60, 1500);
    speed_->setValue(600);
    speed_->setInvertedAppearance(true);       // right = faster
    speed_->setMaximumWidth(120);
    speed_->setToolTip("Animation speed (wall-clock only; results do not change)");
    speed_->setAccessibleName("Playback speed");
    stepLabel_ = new QLabel;
    speedLabel_ = new QLabel;
    // Two groups: the buttons, and the scrubber with step / speed. They sit in one row when the
    // bar is wide enough and in two rows otherwise (resizeEvent), so the bar never forces width.
    buttons_ = new QWidget;
    auto* bl = new QHBoxLayout(buttons_);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->setSpacing(6);
    for (QPushButton* b : {run_, pause_, prev_, next_, stop_, reset_}) bl->addWidget(b);
    scrubRow_ = new QWidget;
    auto* sl = new QHBoxLayout(scrubRow_);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(6);
    sl->addWidget(scrub_, 1);
    sl->addWidget(stepLabel_);
    sl->addWidget(new QLabel("Speed:"));
    sl->addWidget(speed_);
    sl->addWidget(speedLabel_);
    scrub_->setMinimumWidth(80);
    outer_ = new QBoxLayout(QBoxLayout::LeftToRight, this);
    outer_->setContentsMargins(0, 0, 0, 0);
    outer_->setSpacing(6);
    outer_->addWidget(buttons_);
    outer_->addWidget(scrubRow_, 1);

    connect(run_, &QPushButton::clicked, this, &PlaybackBar::run);
    connect(pause_, &QPushButton::clicked, this, &PlaybackBar::togglePause);
    connect(prev_, &QPushButton::clicked, this, &PlaybackBar::prev);
    connect(next_, &QPushButton::clicked, this, &PlaybackBar::next);
    connect(stop_, &QPushButton::clicked, this, &PlaybackBar::stop);
    connect(reset_, &QPushButton::clicked, this, [this] { timer_->stop(); paused_ = false; emit resetRequested(); });
    connect(scrub_, &QSlider::valueChanged, this, [this](int v) { if (v != pos_) go(v, v == pos_ + 1); });
    connect(speed_, &QSlider::valueChanged, this, [this](int v) { timer_->setInterval(v); updateUi(); });
    connect(timer_, &QTimer::timeout, this, [this] {
        if (pos_ >= length_) { timer_->stop(); updateUi(); emit finished(); return; }
        go(pos_ + 1, true);
        if (pos_ >= length_) { timer_->stop(); updateUi(); emit finished(); }
    });
    timer_->setInterval(speed_->value());
    updateUi();
}

QSize PlaybackBar::minimumSizeHint() const {
    QSize b = buttons_->minimumSizeHint(), s = scrubRow_->minimumSizeHint();
    return QSize(std::max(b.width(), s.width()), b.height() + s.height() + 6);
}

void PlaybackBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    int needed = buttons_->sizeHint().width() + scrubRow_->minimumSizeHint().width() + 160;
    auto dir = width() >= needed ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (outer_->direction() != dir) outer_->setDirection(dir);
}

void PlaybackBar::setLength(int steps) {
    timer_->stop();
    paused_ = false;
    length_ = std::max(0, steps);
    pos_ = 0;
    scrub_->blockSignals(true);
    scrub_->setRange(0, length_);
    scrub_->setValue(0);
    scrub_->blockSignals(false);
    updateUi();
}

void PlaybackBar::setPosition(int pos) { go(std::clamp(pos, 0, length_), false); }
bool PlaybackBar::playing() const { return timer_->isActive(); }
int PlaybackBar::intervalMs() const { return speed_->value(); }
void PlaybackBar::setIntervalMs(int ms) { speed_->setValue(ms); }
int PlaybackBar::animMs() const { return std::min(600, intervalMs() * 4 / 5); }

void PlaybackBar::go(int pos, bool forward) {
    pos_ = std::clamp(pos, 0, length_);
    scrub_->blockSignals(true);
    scrub_->setValue(pos_);
    scrub_->blockSignals(false);
    updateUi();
    emit positionChanged(pos_, forward);
}

void PlaybackBar::run() {
    if (length_ == 0) return;
    if (pos_ >= length_) go(0, false);
    paused_ = false;
    timer_->start();
    updateUi();
}

void PlaybackBar::pause() {
    if (timer_->isActive()) togglePause();
}

void PlaybackBar::togglePause() {
    if (timer_->isActive()) { timer_->stop(); paused_ = true; }
    else if (paused_ && pos_ < length_) { paused_ = false; timer_->start(); }
    updateUi();
}

void PlaybackBar::next() {
    if (timer_->isActive()) { timer_->stop(); paused_ = true; }
    if (pos_ < length_) go(pos_ + 1, true);
}

void PlaybackBar::prev() {
    if (timer_->isActive()) { timer_->stop(); paused_ = true; }
    if (pos_ > 0) go(pos_ - 1, false);
}

void PlaybackBar::stop() {
    timer_->stop();
    paused_ = false;
    go(0, false);
}

void PlaybackBar::updateUi() {
    bool play = timer_->isActive();
    run_->setEnabled(length_ > 0 && !play);
    pause_->setText(play ? "Pause" : "Resume");
    pause_->setEnabled(play || (paused_ && pos_ < length_));
    prev_->setEnabled(pos_ > 0);
    next_->setEnabled(pos_ < length_);
    stop_->setEnabled(play || pos_ > 0);
    stepLabel_->setText(QString("Step %1 / %2").arg(pos_).arg(length_));
    speedLabel_->setText(QString("%1 ms").arg(speed_->value()));
}

// ================================================================ PseudoCodeView

PseudoCodeView::PseudoCodeView(QWidget* parent) : QListWidget(parent) {
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setSelectionMode(QAbstractItemView::NoSelection);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName("Pseudocode");
}

void PseudoCodeView::setLines(const QStringList& lines) {
    clear();
    for (int i = 0; i < lines.size(); ++i) addItem(QString("%1  %2").arg(i + 1, 2).arg(lines[i]));
}

void PseudoCodeView::setCurrent(int line) {
    for (int i = 0; i < count(); ++i) {
        QListWidgetItem* it = item(i);
        bool on = i == line;
        QColor bg = ui::semRunning();
        bg.setAlpha(90);
        it->setBackground(on ? QBrush(bg) : QBrush());
        QFont f = it->font();
        f.setBold(on);
        it->setFont(f);
    }
    if (line >= 0 && line < count()) scrollToItem(item(line));
}

// ================================================================ ModuleHeader

ModuleHeader::ModuleHeader(const QString& title, const QString& explanation, QWidget* parent) : QWidget(parent), title_(title) {
    auto* t = new QLabel("<b>" + title.toHtmlEscaped() + "</b>");
    QFont f = t->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    t->setFont(f);
    badge_ = new QLabel;
    auto* e = new QLabel(explanation);
    e->setWordWrap(true);
    status_ = new QLabel;
    status_->setWordWrap(true);
    QFont sf = status_->font();
    sf.setBold(true);
    status_->setFont(sf);
    auto* top = new QHBoxLayout;
    top->addWidget(t);
    top->addWidget(badge_);
    top->addStretch();
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(2);
    l->addLayout(top);
    l->addWidget(e);
    l->addWidget(status_);
    setMode(false);
}

void ModuleHeader::setMode(bool live) {
    QColor c = live ? ui::semWaiting() : ui::semReady();
    badge_->setText(live ? "Live Linux Demo" : "Simulation");
    badge_->setStyleSheet(QString("background:%1; color:white; border-radius:4px; padding:1px 6px; font-weight:bold;")
                              .arg(c.name()));
}

void ModuleHeader::setBadge(const QString& text, const QColor& c) {
    badge_->setText(text);
    badge_->setStyleSheet(QString("background:%1; color:white; border-radius:4px; padding:1px 6px; font-weight:bold;").arg(c.name()));
}

void ModuleHeader::setStatus(const QString& text, const QColor& color) {
    status_->setText(text);
    status_->setStyleSheet(color.isValid() ? QString("color:%1").arg(color.name()) : QString());
}

QString ModuleHeader::status() const { return status_->text(); }

// ================================================================ layout helpers

QWidget* titledBox(const QString& title, QWidget* content) {
    auto* g = new QGroupBox(title);
    auto* l = new QVBoxLayout(g);
    l->setContentsMargins(6, 6, 6, 6);
    l->addWidget(content);
    return g;
}

void assembleModule(QWidget* owner, QWidget* header, QWidget* config, QWidget* center,
                    QWidget* inspector, PlaybackBar* bar, QWidget* bottom, const QString& layoutId) {
    QString id = layoutId;
    if (id.isEmpty()) {
        if (auto* h = qobject_cast<ModuleHeader*>(header)) id = h->title();
        else id = owner->metaObject()->className();
    }
    auto* lay = new QVBoxLayout(owner);
    lay->setContentsMargins(8, 8, 8, 8);
    lay->setSpacing(6);
    if (header) lay->addWidget(header);
    auto* top = new PanelSplitter(Qt::Horizontal, id + "/h");
    if (config) top->addPanel(new ConfigPanel(withSampleSlot(config)), PanelSplitter::Config);
    if (center) top->addPanel(center, PanelSplitter::Center);
    if (inspector) top->addPanel(ConfigPanel::wrap(inspector, "inspectorPanel"), PanelSplitter::Inspector, 1, 0.28);
    if (!bottom) {                          // no bottom pages: the playback bar sits right below
        lay->addWidget(top, 1);
        if (bar) lay->addWidget(bar);
        return;
    }
    auto* vsplit = new PanelSplitter(Qt::Vertical, id + "/v");
    vsplit->addPanel(top, PanelSplitter::Center, 3);
    auto* low = new QWidget;
    auto* ll = new QVBoxLayout(low);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(4);
    if (bar) ll->addWidget(bar);
    ll->addWidget(bottom, 1);
    vsplit->addPanel(ConfigPanel::wrap(low, "detailPanel"), PanelSplitter::Detail, 1);
    lay->addWidget(vsplit, 1);
}

// ================================================================ Load Sample

QWidget* withSampleSlot(QWidget* config) {
    auto* wrap = new QWidget;
    auto* l = new QVBoxLayout(wrap);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    auto* slot = new QWidget;
    slot->setObjectName("sampleSlot");
    auto* sl = new QVBoxLayout(slot);
    sl->setContentsMargins(0, 0, 0, 0);
    slot->hide();                                // shown once a control is installed
    l->addWidget(slot);
    l->addWidget(config, 1);
    return wrap;
}

bool installInSampleSlot(QWidget* page, QWidget* w) {
    auto* slot = page ? page->findChild<QWidget*>("sampleSlot") : nullptr;
    if (!slot || !slot->layout()) return false;
    slot->layout()->addWidget(w);
    slot->show();
    return true;
}

SampleButton::SampleButton(SampleRegistry* reg, const QString& module, QWidget* parent)
    : QWidget(parent), reg_(reg), module_(module) {
    setObjectName("sampleButton");
    button_ = new QToolButton;
    button_->setObjectName("loadSampleButton");
    button_->setText("Load Sample");
    button_->setAccessibleName("Load Sample");
    button_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    button_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button_->setPopupMode(QToolButton::InstantPopup);
    button_->setToolTip("Load a built-in example workload for this module (fills the inputs; you start the run). "
                        "Your own files: File > Open File...");
    button_->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    menu_ = new QMenu(button_);
    menu_->setToolTipsVisible(true);
    button_->setMenu(menu_);
    connect(menu_, &QMenu::aboutToShow, this, &SampleButton::rebuildMenu);
    current_ = new QLabel;
    current_->setObjectName("sampleCurrent");
    current_->setWordWrap(true);
    current_->setTextFormat(Qt::PlainText);
    current_->setText("No sample loaded - choose one to start quickly.");
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 2);
    l->setSpacing(3);
    l->addWidget(button_, 0, Qt::AlignLeft);
    l->addWidget(current_);
    connect(reg_, &SampleRegistry::loaded, this, [this](const SampleDef& d) { if (d.module == module_) showLoaded(d); });
    rebuildMenu();
    restyle();
}

void SampleButton::rebuildMenu() {
    menu_->clear();
    for (const QString& c : reg_->categories(module_)) {
        menu_->addSection(c);
        for (const SampleDef* d : reg_->forModule(module_)) {
            if (d->category != c) continue;
            QString id = d->id;
            QAction* a = menu_->addAction(d->mode.isEmpty() ? d->name : QString("%1   [%2]").arg(d->name, d->mode));
            a->setData(id);
            a->setToolTip(ui::sampleDetails(*d, false));
            a->setStatusTip(d->description);
            connect(a, &QAction::triggered, this, [this, id] { reg_->load(id); });
        }
    }
    if (menu_->isEmpty()) menu_->addAction("(no samples for this module)")->setEnabled(false);
    menu_->addSeparator();
    QAction* b = menu_->addAction("Browse samples (details, Load && Run)...");
    b->setToolTip("Compact sample browser with the full description of every sample");
    connect(b, &QAction::triggered, this, [this] { browse(); });
}

QStringList SampleButton::menuSampleIds() const {
    QStringList ids;
    for (QAction* a : menu_->actions()) if (!a->data().toString().isEmpty()) ids << a->data().toString();
    return ids;
}

QString SampleButton::currentText() const { return current_->text(); }

bool SampleButton::browse() {
    SampleDialog dlg(reg_, module_, title_, window());
    if (dlg.exec() != QDialog::Accepted || dlg.selectedId().isEmpty()) return false;
    return reg_->load(dlg.selectedId(), dlg.runRequested());
}

void SampleButton::showLoaded(const SampleDef& d) {
    // Name + a short description (the full text is in the tooltip) so the inputs keep their space.
    QString desc = d.description.size() > 110 ? d.description.left(107).trimmed() + "..." : d.description;
    current_->setText("Sample: " + d.name + " - " + desc);
    current_->setToolTip(ui::sampleDetails(d, false));
}

// Accent style: the palette's highlight colour (theme aware) with a readable text colour.
void SampleButton::restyle() {
    if (styling_) return;
    styling_ = true;
    QColor bg = QApplication::palette().color(QPalette::Highlight);
    QColor fg = ui::contrastRatio(QColor("#ffffff"), bg) >= ui::contrastRatio(QColor("#1f2328"), bg) ? QColor("#ffffff")
                                                                                                    : QColor("#1f2328");
    if (bg != bg_ || fg != fg_) {
        bg_ = bg;
        fg_ = fg;
        button_->setStyleSheet(QString(
            "QToolButton#loadSampleButton { background:%1; color:%2; font-weight:bold; border:1px solid %3;"
            " border-radius:4px; padding:4px 10px; }"
            "QToolButton#loadSampleButton:hover { background:%4; }"   /* darker: contrast never drops */
            "QToolButton#loadSampleButton:pressed { background:%3; }"
            "QToolButton#loadSampleButton::menu-indicator { subcontrol-position: right center; right:4px; }")
            .arg(bg.name(), fg.name(), bg.darker(125).name(), bg.darker(110).name()));
        current_->setStyleSheet("font-style: italic;");
    }
    styling_ = false;
}

void SampleButton::changeEvent(QEvent* e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange) restyle();
}

namespace ui {
// WCAG relative luminance contrast ratio (1..21) between two colours.
double contrastRatio(const QColor& a, const QColor& b) {
    auto lum = [](const QColor& c) {
        auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
    };
    double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}
}
