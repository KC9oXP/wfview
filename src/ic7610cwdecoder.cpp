// ic7610cwdecoder.cpp
//
// IC7610_V49_CW_DECODER (layout + pitch tracking reworked in v50;
// engine selector and WAV recorder added in v51: IC7610_V51_CW_HANDKEY;
// panel + CW Sender merge: IC7610_V54_CW_MERGE; multi-signal: IC7610_V54_CW_MULTI)
// See include/ic7610cwdecoder.h for the data flow.

#include "ic7610cwdecoder.h"
#include "ic7610cwcore.h"
#include "ic7610cwmulti.h"
#include "rxaudioprocessor.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QGridLayout>
#include <QBrush>
#include <QColor>
#include <QHeaderView>
#include <QHideEvent>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaType>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QTextCursor>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <string>

namespace {
constexpr int kPitchFollowRadio = 0;
constexpr int kPitchAuto        = 1;
constexpr int kPitchFixed       = 2;

constexpr int kSpeedAuto  = 0;
constexpr int kSpeedFixed = 1;

constexpr float kFollowSpanHz = 150.0f;  // Follow radio: search radio pitch +/- this

constexpr int kMaxTextChars  = 20000;   // trim the window beyond this
constexpr int kTrimTextChars = 5000;
}

// ===========================================================================
// Worker (runs in its own QThread)
// ===========================================================================

Ic7610CwDecoderWorker::Ic7610CwDecoderWorker(QObject *parent)
    : QObject(parent),
      m_core(new Ic7610CwCore())
{
    m_statsTimer.start();
    m_multiTimer.start();
}

Ic7610CwDecoderWorker::~Ic7610CwDecoderWorker()
{
    finishRecording();
}

void Ic7610CwDecoderWorker::pushAudio(QVector<float> mono, float sampleRate)
{
    if (mono.isEmpty()) return;

    // ---- WAV recorder ------------------------------------------------------
    if (m_recFile.isOpen()) {
        const quint32 rate = static_cast<quint32>(sampleRate + 0.5f);
        if (m_recRate == 0) {
            m_recRate = rate;
            m_recMaxSamples = static_cast<qint64>(rate) * m_recMaxSeconds;
            writeWavHeader(0);
        }
        if (rate != m_recRate) {
            finishRecording();          // audio format changed: close the clip
        } else {
            QByteArray pcm;
            pcm.resize(mono.size() * 2);
            char *d = pcm.data();
            int nWrite = 0;
            for (int i = 0; i < mono.size() && m_recSamples < m_recMaxSamples; ++i, ++nWrite) {
                const float v = qBound(-1.0f, mono[i], 1.0f);
                const qint16 q = static_cast<qint16>(qRound(v * 32767.0f));
                d[2 * i]     = static_cast<char>(q & 0xff);
                d[2 * i + 1] = static_cast<char>((q >> 8) & 0xff);
                ++m_recSamples;
            }
            m_recFile.write(pcm.constData(), nWrite * 2);
            const int sec = static_cast<int>(m_recSamples / m_recRate);
            if (sec != m_recLastSec) {
                m_recLastSec = sec;
                emit recordingProgress(m_recPath, sec, false);
            }
            if (m_recSamples >= m_recMaxSamples) finishRecording();
        }
    }

    std::string out;
    m_core->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate, out);
    if (!out.empty()) {
        emit textDecoded(QString::fromLatin1(out.data(), static_cast<int>(out.size())));
    }

    // ---- multi-signal ------------------------------------------------------
    if (m_multi) {
        m_multiDirty |= m_multi->feed(mono.constData(), static_cast<int>(mono.size()), sampleRate);
        if (m_multiTimer.elapsed() >= 300) {
            m_multiTimer.restart();
            QVariantList list;
            for (const auto &c : m_multi->channels()) {
                QVariantMap m;
                m.insert(QStringLiteral("id"), c.id);
                m.insert(QStringLiteral("pitch"), static_cast<double>(c.pitchHz));
                m.insert(QStringLiteral("wpm"), static_cast<double>(c.wpm));
                m.insert(QStringLiteral("snr"), static_cast<double>(c.snrDb));
                m.insert(QStringLiteral("active"), c.active);
                m.insert(QStringLiteral("text"), QString::fromLatin1(c.text.data(), static_cast<int>(c.text.size())));
                list.append(m);
            }
            emit multiUpdated(list);
            m_multiDirty = false;
        }
    }

    if (m_statsTimer.elapsed() >= 250) {
        m_statsTimer.restart();
        const Ic7610CwCore::Stats s = m_core->stats();
        emit statsUpdated(s.pitchHz, s.speedWpm, s.snrDb, s.locked, s.open,
                          m_core->sampleRate());
    }
}

void Ic7610CwDecoderWorker::setPitchMode(int mode, float fixedHz, float radioHz)
{
    switch (mode) {
    case kPitchFixed:
        m_core->setFixedPitch(fixedHz);
        break;
    case kPitchFollowRadio:
        if (radioHz > 0.0f) {
            m_core->setTrackRange(radioHz - kFollowSpanHz, radioHz + kFollowSpanHz, radioHz);
        } else {
            m_core->setTrackRange(200.0f, 1200.0f, -1.0f);   // radio pitch not known yet
        }
        break;
    default:
        m_core->setTrackRange(200.0f, 1200.0f, -1.0f);
        break;
    }
}

void Ic7610CwDecoderWorker::setSpeedWpm(float wpm) { m_core->setSpeedWpm(wpm); }
void Ic7610CwDecoderWorker::setSquelch(bool on)    { m_core->setSquelch(on); }
void Ic7610CwDecoderWorker::setEngine(int engine)
{
    m_core->setEngine(engine == 1 ? Ic7610CwCore::Engine::Hand : Ic7610CwCore::Engine::Machine);
}

void Ic7610CwDecoderWorker::startRecording(QString path, int maxSeconds)
{
    if (m_recFile.isOpen()) finishRecording();
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_recFile.setFileName(path);
    if (!m_recFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit recordingProgress(QString(), 0, true);
        return;
    }
    m_recPath = path;
    m_recSamples = 0;
    m_recRate = 0;                 // set from the first audio block
    m_recMaxSeconds = qBound(1, maxSeconds, 600);
    m_recMaxSamples = 0;
    m_recLastSec = -1;
    emit recordingProgress(m_recPath, 0, false);
}

void Ic7610CwDecoderWorker::stopRecording()
{
    if (m_recFile.isOpen()) finishRecording();
}

void Ic7610CwDecoderWorker::writeWavHeader(quint32 dataBytes)
{
    auto le32 = [](QByteArray &b, quint32 v) {
        for (int i = 0; i < 4; ++i) b.append(static_cast<char>((v >> (8 * i)) & 0xff));
    };
    auto le16 = [](QByteArray &b, quint16 v) {
        b.append(static_cast<char>(v & 0xff));
        b.append(static_cast<char>((v >> 8) & 0xff));
    };
    QByteArray h;
    h.append("RIFF", 4);
    le32(h, 36 + dataBytes);
    h.append("WAVEfmt ", 8);
    le32(h, 16);
    le16(h, 1);                    // PCM
    le16(h, 1);                    // mono
    le32(h, m_recRate);
    le32(h, m_recRate * 2);        // byte rate
    le16(h, 2);                    // block align
    le16(h, 16);                   // bits
    h.append("data", 4);
    le32(h, dataBytes);
    const qint64 pos = m_recFile.pos();
    m_recFile.seek(0);
    m_recFile.write(h);
    if (pos > 44) m_recFile.seek(pos);
}

void Ic7610CwDecoderWorker::finishRecording()
{
    if (!m_recFile.isOpen()) return;
    if (m_recRate == 0) {
        // No audio arrived: leave no empty file behind.
        m_recFile.close();
        m_recFile.remove();
        emit recordingProgress(QString(), 0, true);
        return;
    }
    writeWavHeader(static_cast<quint32>(m_recSamples * 2));
    m_recFile.close();
    emit recordingProgress(m_recPath, static_cast<int>(m_recSamples / m_recRate), true);
}
void Ic7610CwDecoderWorker::resetDecoder()         { m_core->reset(); }

void Ic7610CwDecoderWorker::setMulti(bool on)
{
    if (on && !m_multi) {
        m_multi.reset(new Ic7610CwMulti());
        m_multi->setRange(200.0f, 2000.0f);
    } else if (!on && m_multi) {
        m_multi.reset();
        emit multiUpdated(QVariantList());
    }
}

// ===========================================================================
// Window (GUI thread)
// ===========================================================================

Ic7610CwDecoderPanel::Ic7610CwDecoderPanel(RxAudioProcessor *rxProc, QWidget *parent)
    : QWidget(parent),
      m_rxProc(rxProc)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");
    qRegisterMetaType<QVariantList>("QVariantList");

    // IC7610_V50_CW_LAYOUT
    // qdarkstyle gives spin/combo boxes 5px padding all round, which clips
    // their text on this high-DPI setup (same issue as the v47 notch box).
    // Trim the vertical padding for this window only.
    setStyleSheet(QStringLiteral(
        "QSpinBox, QComboBox { padding-top: 0px; padding-bottom: 0px;"
        " padding-left: 4px; min-width: 0px; }"));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    const QFontMetrics fm(font());
    const int rowH = fm.height() + 12;
    auto fitCombo = [&](QComboBox *c) {
        int w = 0;
        for (int i = 0; i < c->count(); ++i) w = qMax(w, fm.horizontalAdvance(c->itemText(i)));
        c->setMinimumWidth(w + 48);      // text + arrow + padding
        c->setMinimumHeight(rowH);
    };
    auto fitSpin = [&](QSpinBox *s) {
        const QString widest = QString::number(s->maximum()) + s->suffix();
        s->setMinimumWidth(fm.horizontalAdvance(widest) + 44);   // text + arrows
        s->setMinimumHeight(rowH);
        s->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    };

    // --- row 1: on/off and actions ----------------------------------------
    auto *row1 = new QHBoxLayout();
    root->addLayout(row1);

    m_enable = new QCheckBox(tr("Decode"), this);
    m_enable->setToolTip(tr("Decode CW from the receive audio (after NR, before sidetone)."));
    row1->addWidget(m_enable);

    row1->addSpacing(16);
    m_squelchChk = new QCheckBox(tr("Squelch"), this);
    m_squelchChk->setChecked(true);
    m_squelchChk->setToolTip(tr("Print nothing while no steady CW tone is present\n"
                                "(stops random E/T/I letters on band noise)."));
    row1->addWidget(m_squelchChk);

    row1->addSpacing(16);
    row1->addWidget(new QLabel(tr("Decoder:"), this));
    m_engineCombo = new QComboBox(this);
    m_engineCombo->addItem(tr("Machine (keyer)"), 0);
    m_engineCombo->addItem(tr("Hand key"), 1);
    m_engineCombo->setToolTip(tr("Machine: best on keyer / computer-sent CW with even timing.\n"
                                 "Hand key: learns each sender's own dit, dah and spacing and\n"
                                 "copies uneven straight-key or bug sending. Prints a word at a time,\n"
                                 "about a second behind."));
    fitCombo(m_engineCombo);
    row1->addWidget(m_engineCombo);

    row1->addSpacing(16);
    m_multiChk = new QCheckBox(tr("Multi-signal"), this);
    m_multiChk->setToolTip(tr("Also decode every other CW signal in the receive passband\n"
                              "(widen the radio's filter to see more). One line per station;\n"
                              "double-click a line to make it the main decode."));
    row1->addWidget(m_multiChk);

    row1->addStretch(1);

    m_resyncBtn = new QPushButton(tr("Re-sync"), this);
    m_resyncBtn->setToolTip(tr("Restart pitch/speed tracking (e.g. after changing stations)."));
    m_resyncBtn->setMinimumHeight(rowH);
    row1->addWidget(m_resyncBtn);

    m_clearBtn = new QPushButton(tr("Clear"), this);
    m_clearBtn->setMinimumHeight(rowH);
    row1->addWidget(m_clearBtn);

    // --- row 2: pitch and speed --------------------------------------------
    auto *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    root->addLayout(grid);

    grid->addWidget(new QLabel(tr("Pitch:"), this), 0, 0);
    m_pitchMode = new QComboBox(this);
    m_pitchMode->addItem(tr("Follow radio"), kPitchFollowRadio);
    m_pitchMode->addItem(tr("Auto"),         kPitchAuto);
    m_pitchMode->addItem(tr("Fixed"),        kPitchFixed);
    m_pitchMode->setToolTip(tr("Follow radio: track the strongest tone within +/-150 Hz\n"
                               "of the IC-7610 CW pitch (works when not exactly zero-beat).\n"
                               "Auto: track the strongest steady tone from 200 to 1200 Hz.\n"
                               "Fixed: decode exactly at the value at right."));
    fitCombo(m_pitchMode);
    grid->addWidget(m_pitchMode, 0, 1);

    m_pitchSpin = new QSpinBox(this);
    m_pitchSpin->setRange(200, 1200);
    m_pitchSpin->setSingleStep(10);
    m_pitchSpin->setValue(600);
    m_pitchSpin->setSuffix(tr(" Hz"));
    fitSpin(m_pitchSpin);
    grid->addWidget(m_pitchSpin, 0, 2);

    grid->addItem(new QSpacerItem(24, 1), 0, 3);

    grid->addWidget(new QLabel(tr("Speed:"), this), 0, 4);
    m_speedMode = new QComboBox(this);
    m_speedMode->addItem(tr("Auto"),  kSpeedAuto);
    m_speedMode->addItem(tr("Fixed"), kSpeedFixed);
    fitCombo(m_speedMode);
    grid->addWidget(m_speedMode, 0, 5);

    m_speedSpin = new QSpinBox(this);
    m_speedSpin->setRange(5, 60);
    m_speedSpin->setValue(20);
    m_speedSpin->setSuffix(tr(" WPM"));
    fitSpin(m_speedSpin);
    grid->addWidget(m_speedSpin, 0, 6);

    grid->setColumnStretch(7, 1);

    // --- decoded text ------------------------------------------------------
    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(mono.pointSizeF() * 1.25);
    m_text->setFont(mono);
    m_text->setMinimumHeight(QFontMetrics(mono).lineSpacing() * 6);
    root->addWidget(m_text, 2);

    // --- multi-signal list (IC7610_V54_CW_MULTI) ----------------------------
    m_multiTable = new QTableWidget(0, 3, this);
    m_multiTable->setHorizontalHeaderLabels({tr("Pitch"), tr("WPM"), tr("Decoded")});
    m_multiTable->verticalHeader()->setVisible(false);
    m_multiTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_multiTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_multiTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_multiTable->setWordWrap(false);
    m_multiTable->setFont(mono);
    m_multiTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_multiTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_multiTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_multiTable->setMinimumHeight(QFontMetrics(mono).lineSpacing() * 5);
    m_multiTable->setToolTip(tr("Every CW signal found in the passband. Double-click a line\n"
                                "to decode that station in the main window (Fixed pitch)."));
    m_multiTable->setVisible(false);
    root->addWidget(m_multiTable, 2);

    // --- recorder row (IC7610_V51_CW_HANDKEY) -------------------------------
    auto *recRow = new QHBoxLayout();
    root->addLayout(recRow);
    m_recordBtn = new QPushButton(tr("Record 60 s"), this);
    m_recordBtn->setToolTip(tr("Save 60 seconds of the audio the decoder hears to a WAV file,\n"
                               "for testing the decoder on real signals."));
    m_recordBtn->setMinimumHeight(rowH);
    m_recordBtn->setEnabled(false);
    recRow->addWidget(m_recordBtn);
    m_openClipsBtn = new QPushButton(tr("Open clips folder"), this);
    m_openClipsBtn->setMinimumHeight(rowH);
    recRow->addWidget(m_openClipsBtn);
    m_recLabel = new QLabel(this);
    recRow->addWidget(m_recLabel, 1);

    // --- status line -------------------------------------------------------
    m_status = new QLabel(tr("Off"), this);
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(fm.height() * 2 + 4);   // room for a wrapped line
    root->addWidget(m_status);


    // --- worker thread -----------------------------------------------------
    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("ic7610CwDecoder"));
    m_worker = new Ic7610CwDecoderWorker();
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &Ic7610CwDecoderWorker::textDecoded,
            this, &Ic7610CwDecoderPanel::onTextDecoded);
    connect(m_worker, &Ic7610CwDecoderWorker::statsUpdated,
            this, &Ic7610CwDecoderPanel::onStatsUpdated);
    connect(m_worker, &Ic7610CwDecoderWorker::recordingProgress,
            this, &Ic7610CwDecoderPanel::onRecordingProgress);
    connect(m_worker, &Ic7610CwDecoderWorker::multiUpdated,
            this, &Ic7610CwDecoderPanel::onMultiUpdated);
    m_thread->start(QThread::LowPriority);

    // --- radio pitch re-read while following the radio ---------------------
    m_pitchPoll = new QTimer(this);
    m_pitchPoll->setInterval(3000);
    connect(m_pitchPoll, &QTimer::timeout, this, [this]() {
        if (m_enable->isChecked()
                && m_pitchMode->currentData().toInt() == kPitchFollowRadio) {
            emit radioPitchRequested();
        }
    });

    // --- UI wiring ---------------------------------------------------------
    connect(m_enable, &QCheckBox::toggled, this, &Ic7610CwDecoderPanel::onEnableToggled);
    connect(m_squelchChk, &QCheckBox::toggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection, Q_ARG(bool, on));
    });
    connect(m_pitchMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_pitchMode->currentData().toInt() == kPitchFollowRadio) emit radioPitchRequested();
        applyDecodeSettings();
    });
    connect(m_pitchSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &Ic7610CwDecoderPanel::applyDecodeSettings);
    connect(m_speedMode, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &Ic7610CwDecoderPanel::applyDecodeSettings);
    connect(m_speedSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &Ic7610CwDecoderPanel::applyDecodeSettings);
    connect(m_clearBtn, &QPushButton::clicked, m_text, &QPlainTextEdit::clear);
    connect(m_engineCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        QMetaObject::invokeMethod(m_worker, "setEngine", Qt::QueuedConnection,
                                  Q_ARG(int, m_engineCombo->currentData().toInt()));
    });
    connect(m_recordBtn, &QPushButton::clicked, this, &Ic7610CwDecoderPanel::onRecordClicked);
    connect(m_multiChk, &QCheckBox::toggled, this, [this](bool on) {
        m_multiTable->setVisible(on);
        if (!on) m_multiTable->setRowCount(0);
        QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection, Q_ARG(bool, on));
    });
    connect(m_multiTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem *it = m_multiTable->item(row, 0);
        if (!it) return;
        const int hz = it->data(Qt::UserRole).toInt();
        if (hz < 200 || hz > 1200) return;
        m_pitchSpin->setValue(hz);
        m_pitchMode->setCurrentIndex(m_pitchMode->findData(kPitchFixed));
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        applyDecodeSettings();
        appendColored(tr("\n[now decoding %1 Hz]\n").arg(hz), QColor(120, 170, 255));
    });
    connect(m_openClipsBtn, &QPushButton::clicked, this, [this]() {
        const QString dir = clipFolder();
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(m_resyncBtn, &QPushButton::clicked, this, [this]() {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        applyDecodeSettings();
    });

    applyDecodeSettings();
}

Ic7610CwDecoderPanel::~Ic7610CwDecoderPanel()
{
    disconnectTap();
    if (m_rxProc) m_rxProc->setCwTapEnabled(false);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
    }
}

void Ic7610CwDecoderPanel::setRxProcessor(RxAudioProcessor *rxProc)
{
    if (rxProc == m_rxProc.data()) return;
    const bool on = m_enable->isChecked();
    disconnectTap();
    if (m_rxProc) m_rxProc->setCwTapEnabled(false);
    m_rxProc = rxProc;
    if (on) connectTap();
}

void Ic7610CwDecoderPanel::setRadioPitch(int hz)
{
    if (hz < 200 || hz > 1200) return;
    if (hz == m_radioPitchHz) return;
    m_radioPitchHz = hz;
    if (m_pitchMode->currentData().toInt() == kPitchFollowRadio) {
        applyDecodeSettings();
    }
}

void Ic7610CwDecoderPanel::stopDecoding()
{
    m_enable->setChecked(false);
}

void Ic7610CwDecoderPanel::hideEvent(QHideEvent *event)
{
    // Closing the host window stops decoding so no CPU is spent in the
    // background. Minimising (a spontaneous hide) keeps decoding.
    if (!event->spontaneous()) stopDecoding();
    QWidget::hideEvent(event);
}

void Ic7610CwDecoderPanel::connectTap()
{
    if (!m_rxProc) {
        m_status->setText(tr("No RX audio processor - connect to the radio first."));
        return;
    }
    if (!m_tapConn) {
        m_tapConn = connect(m_rxProc.data(), &RxAudioProcessor::cwTapAudio,
                            m_worker, &Ic7610CwDecoderWorker::pushAudio,
                            Qt::QueuedConnection);
    }
    m_rxProc->setCwTapEnabled(true);
}

void Ic7610CwDecoderPanel::disconnectTap()
{
    if (m_tapConn) {
        QObject::disconnect(m_tapConn);
        m_tapConn = QMetaObject::Connection();
    }
}

void Ic7610CwDecoderPanel::onEnableToggled(bool on)
{
    if (on) {
        QMetaObject::invokeMethod(m_worker, "resetDecoder", Qt::QueuedConnection);
        applyDecodeSettings();
        QMetaObject::invokeMethod(m_worker, "setSquelch", Qt::QueuedConnection,
                                  Q_ARG(bool, m_squelchChk->isChecked()));
        QMetaObject::invokeMethod(m_worker, "setEngine", Qt::QueuedConnection,
                                  Q_ARG(int, m_engineCombo->currentData().toInt()));
        QMetaObject::invokeMethod(m_worker, "setMulti", Qt::QueuedConnection,
                                  Q_ARG(bool, m_multiChk->isChecked()));
        connectTap();
        m_recordBtn->setEnabled(true);
        if (m_pitchMode->currentData().toInt() == kPitchFollowRadio) {
            emit radioPitchRequested();
        }
        m_pitchPoll->start();
        if (m_rxProc) m_status->setText(tr("Listening..."));
    } else {
        if (m_recording)
            QMetaObject::invokeMethod(m_worker, "stopRecording", Qt::QueuedConnection);
        m_recordBtn->setEnabled(false);
        m_pitchPoll->stop();
        if (m_rxProc) m_rxProc->setCwTapEnabled(false);
        disconnectTap();
        m_status->setText(tr("Off"));
    }
}

void Ic7610CwDecoderPanel::applyDecodeSettings()
{
    const int pm = m_pitchMode->currentData().toInt();
    m_pitchSpin->setEnabled(pm == kPitchFixed);
    if (pm == kPitchFollowRadio && m_radioPitchHz > 0) {
        m_pitchSpin->blockSignals(true);
        m_pitchSpin->setValue(m_radioPitchHz);
        m_pitchSpin->blockSignals(false);
    }

    const bool fixedSpeed = (m_speedMode->currentData().toInt() == kSpeedFixed);
    m_speedSpin->setEnabled(fixedSpeed);
    const float wpm = fixedSpeed ? static_cast<float>(m_speedSpin->value()) : -1.0f;

    QMetaObject::invokeMethod(m_worker, "setPitchMode", Qt::QueuedConnection,
                              Q_ARG(int, pm),
                              Q_ARG(float, static_cast<float>(m_pitchSpin->value())),
                              Q_ARG(float, static_cast<float>(m_radioPitchHz)));
    QMetaObject::invokeMethod(m_worker, "setSpeedWpm", Qt::QueuedConnection, Q_ARG(float, wpm));
}

void Ic7610CwDecoderPanel::onTextDecoded(const QString &text)
{
    if (!m_enable->isChecked()) return;

    QScrollBar *sb = m_text->verticalScrollBar();
    const bool atBottom = (sb->value() >= sb->maximum() - 4);

    QTextCursor c(m_text->document());
    c.movePosition(QTextCursor::End);
    c.insertText(text, QTextCharFormat());   // default colour (after any [TX] line)

    const int chars = m_text->document()->characterCount();
    if (chars > kMaxTextChars) {
        QTextCursor t(m_text->document());
        t.movePosition(QTextCursor::Start);
        t.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, kTrimTextChars);
        t.removeSelectedText();
    }

    if (atBottom) sb->setValue(sb->maximum());
}

void Ic7610CwDecoderPanel::onStatsUpdated(float pitchHz, float speedWpm, float snrDb,
                                           bool locked, bool squelchOpen, float sampleRate)
{
    if (!m_enable->isChecked()) return;

    QString pitchSrc;
    switch (m_pitchMode->currentData().toInt()) {
    case kPitchFollowRadio:
        if (m_radioPitchHz <= 0)  pitchSrc = tr("radio pitch not read yet - searching");
        else if (locked)          pitchSrc = tr("locked, radio %1 Hz").arg(m_radioPitchHz);
        else                      pitchSrc = tr("searching near radio %1 Hz").arg(m_radioPitchHz);
        break;
    case kPitchFixed:
        pitchSrc = tr("fixed");
        break;
    default:
        pitchSrc = locked ? tr("locked") : tr("searching");
        break;
    }

    const QString pitchText = (pitchHz > 0.0f) ? QString::number(qRound(pitchHz)) : QStringLiteral("--");

    m_status->setText(tr("Pitch %1 Hz (%2)   Speed %3 WPM   Signal %4 dB%5   Audio %6 kHz")
                      .arg(pitchText)
                      .arg(pitchSrc)
                      .arg(speedWpm, 0, 'f', 1)
                      .arg(snrDb, 0, 'f', 0)
                      .arg(squelchOpen ? QString() : tr(" (squelched)"))
                      .arg(sampleRate / 1000.0f, 0, 'f', 1));
}

// ---- WAV recorder (IC7610_V51_CW_HANDKEY) ---------------------------------

QString Ic7610CwDecoderPanel::clipFolder() const
{
    // This build lives under D:\wfbuild testing; keep clips with it when
    // that folder exists, otherwise use Documents.
    const QString buildRoot = QStringLiteral("D:/wfbuild testing");
    if (QDir(buildRoot).exists()) return buildRoot + QStringLiteral("/cw-clips");
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + QStringLiteral("/wfview-cw-clips");
}

void Ic7610CwDecoderPanel::onRecordClicked()
{
    if (m_recording) {
        QMetaObject::invokeMethod(m_worker, "stopRecording", Qt::QueuedConnection);
        return;
    }
    const QString name = QStringLiteral("cw-%1-%2Hz.wav")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
            .arg(m_radioPitchHz > 0 ? m_radioPitchHz : m_pitchSpin->value());
    const QString path = clipFolder() + QLatin1Char('/') + name;
    m_recording = true;
    m_recordBtn->setText(tr("Stop recording"));
    QMetaObject::invokeMethod(m_worker, "startRecording", Qt::QueuedConnection,
                              Q_ARG(QString, path), Q_ARG(int, 60));
}

void Ic7610CwDecoderPanel::onRecordingProgress(const QString &path, int seconds, bool finished)
{
    if (finished) {
        m_recording = false;
        m_recordBtn->setText(tr("Record 60 s"));
        if (path.isEmpty())
            m_recLabel->setText(tr("Recording failed (no audio, or folder not writable)."));
        else
            m_recLabel->setText(tr("Saved %1 s: %2").arg(seconds).arg(QDir::toNativeSeparators(path)));
        return;
    }
    m_recLabel->setText(tr("Recording... %1 s").arg(seconds));
}

// ---- multi-signal list (IC7610_V54_CW_MULTI) --------------------------------

void Ic7610CwDecoderPanel::onMultiUpdated(const QVariantList &channels)
{
    if (!m_multiChk->isChecked() || !m_enable->isChecked()) {
        m_multiTable->setRowCount(0);
        return;
    }
    const int keepSel = m_multiTable->currentRow() >= 0 && m_multiTable->item(m_multiTable->currentRow(), 0)
            ? m_multiTable->item(m_multiTable->currentRow(), 0)->data(Qt::UserRole + 1).toInt() : -1;
    m_multiTable->setRowCount(channels.size());
    const QFontMetrics fm(m_multiTable->font());
    const int textCol = qMax(10, m_multiTable->columnWidth(2) / qMax(1, fm.horizontalAdvance(QLatin1Char('M'))) - 1);
    for (int r = 0; r < channels.size(); ++r) {
        const QVariantMap m = channels.at(r).toMap();
        const int hz = qRound(m.value(QStringLiteral("pitch")).toDouble());
        const double wpm = m.value(QStringLiteral("wpm")).toDouble();
        const bool active = m.value(QStringLiteral("active")).toBool();
        QString text = m.value(QStringLiteral("text")).toString().trimmed();
        if (text.size() > textCol) text = QStringLiteral("...") + text.right(textCol - 3);

        auto put = [&](int col, const QString &s, Qt::Alignment al) {
            QTableWidgetItem *it = m_multiTable->item(r, col);
            if (!it) { it = new QTableWidgetItem(); m_multiTable->setItem(r, col, it); }
            it->setText(s);
            it->setTextAlignment(al | Qt::AlignVCenter);
            it->setForeground(active ? QBrush() : QBrush(QColor(140, 140, 140)));
            return it;
        };
        QTableWidgetItem *p = put(0, QStringLiteral("%1 Hz").arg(hz), Qt::AlignRight);
        p->setData(Qt::UserRole, hz);
        p->setData(Qt::UserRole + 1, m.value(QStringLiteral("id")).toInt());
        put(1, wpm > 0.0 ? QString::number(wpm, 'f', 0) : QStringLiteral("--"), Qt::AlignRight);
        put(2, text, Qt::AlignLeft);
        if (keepSel >= 0 && m.value(QStringLiteral("id")).toInt() == keepSel) m_multiTable->selectRow(r);
    }
}

// ---- sent text (IC7610_V54_CW_MERGE) ----------------------------------------

void Ic7610CwDecoderPanel::appendColored(const QString &text, const QColor &color)
{
    QScrollBar *sb = m_text->verticalScrollBar();
    const bool atBottom = (sb->value() >= sb->maximum() - 4);
    QTextCharFormat f;
    f.setForeground(color);
    QTextCursor c(m_text->document());
    c.movePosition(QTextCursor::End);
    c.insertText(text, f);
    if (atBottom) sb->setValue(sb->maximum());
}

void Ic7610CwDecoderPanel::appendSent(const QString &text)
{
    // "Send immediately" mode sends one character at a time; collect them
    // and show one [TX] line once sending pauses.
    if (text.isEmpty()) return;
    m_txPending += text;
    if (!m_txTimer) {
        m_txTimer = new QTimer(this);
        m_txTimer->setSingleShot(true);
        m_txTimer->setInterval(1500);
        connect(m_txTimer, &QTimer::timeout, this, [this]() {
            const QString t = m_txPending.simplified().toUpper();
            m_txPending.clear();
            if (t.isEmpty()) return;
            const QString doc = m_text->toPlainText();
            const QString lead = (doc.isEmpty() || doc.endsWith(QLatin1Char('\n')))
                    ? QString() : QStringLiteral("\n");
            appendColored(lead + QStringLiteral("[TX] ") + t + QStringLiteral("\n"),
                          QColor(255, 170, 60));
        });
    }
    m_txTimer->start();
}

// ===========================================================================
// Stand-alone window wrapper
// ===========================================================================

Ic7610CwDecoderWindow::Ic7610CwDecoderWindow(RxAudioProcessor *rxProc, QWidget *parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("ic7610CwDecoderWin"));
    setWindowTitle(tr("CW Decoder"));
    setModal(false);
    auto *lay = new QVBoxLayout(this);
    m_panel = new Ic7610CwDecoderPanel(rxProc, this);
    m_panel->setObjectName(QStringLiteral("ic7610CwDecoderPanelStandalone"));
    lay->addWidget(m_panel);
    connect(m_panel, &Ic7610CwDecoderPanel::radioPitchRequested,
            this, &Ic7610CwDecoderWindow::radioPitchRequested);
    const QFontMetrics fm(font());
    resize(fm.horizontalAdvance(QLatin1Char('M')) * 64, fm.height() * 26);
}

void Ic7610CwDecoderWindow::setRxProcessor(RxAudioProcessor *rxProc) { m_panel->setRxProcessor(rxProc); }
void Ic7610CwDecoderWindow::setRadioPitch(int hz) { m_panel->setRadioPitch(hz); }

void Ic7610CwDecoderWindow::closeEvent(QCloseEvent *event)
{
    m_panel->stopDecoding();
    QDialog::closeEvent(event);
}
