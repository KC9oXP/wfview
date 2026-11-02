// ic7610cwdecoder.h
//
// IC7610_V49_CW_DECODER (layout + pitch tracking reworked in v50;
// engine selector and WAV recorder added in v51: IC7610_V51_CW_HANDKEY;
// v54: IC7610_V54_CW_MERGE - the decoder is a panel (Ic7610CwDecoderPanel)
// embedded in wfview's CW Sender window; Ic7610CwDecoderWindow is a thin
// stand-alone wrapper used only when the CW window does not exist yet.
// IC7610_V54_CW_MULTI - multi-signal list.)
// CW (Morse) decoder window for wfview IC-7610 EXT builds.
//
//   RxAudioProcessor (converter thread)
//       -- cwTapAudio(mono, sampleRate) [queued] -->
//   Ic7610CwDecoderWorker (own QThread, runs ggmorse via Ic7610CwCore)
//       -- textDecoded / statsUpdated [queued] -->
//   Ic7610CwDecoderWindow (GUI thread)
//
// The tap is taken after NR/EQ/output gain and BEFORE the CW sidetone is
// mixed in, so the decoder copies the other station, not your own keying.

#pragma once

#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QVariant>
#include <QWidget>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

class QCheckBox;
class QColor;
class QCloseEvent;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QHideEvent;
class QThread;
class QTimer;

class Ic7610CwCore;
class Ic7610CwMulti;
class RxAudioProcessor;

class Ic7610CwDecoderWorker : public QObject
{
    Q_OBJECT
public:
    explicit Ic7610CwDecoderWorker(QObject *parent = nullptr);
    ~Ic7610CwDecoderWorker() override;

public slots:
    void pushAudio(QVector<float> mono, float sampleRate);
    // mode: 0 = follow radio (track radioHz +/-150 Hz), 1 = auto (200-1200 Hz),
    //       2 = fixed (fixedHz). radioHz <= 0 = not read from the radio yet.
    void setPitchMode(int mode, float fixedHz, float radioHz);
    void setSpeedWpm(float wpm);    // <= 0 = auto
    void setSquelch(bool on);
    void setEngine(int engine);     // 0 = machine (ggmorse), 1 = hand key
    void setMulti(bool on);         // multi-signal decoding on/off
    void resetDecoder();

    // WAV recorder: mono 16-bit PCM of exactly what the decoder hears.
    void startRecording(QString path, int maxSeconds);
    void stopRecording();

signals:
    void textDecoded(QString text);
    // seconds recorded so far; finished = file closed (path is final).
    void recordingProgress(QString path, int seconds, bool finished);
    // Multi-signal snapshot: list of QVariantMap {id, pitch, wpm, snr, active, text}.
    void multiUpdated(QVariantList channels);
    void statsUpdated(float pitchHz, float speedWpm, float snrDb,
                      bool locked, bool squelchOpen, float sampleRate);

private:
    void writeWavHeader(quint32 dataBytes);
    void finishRecording();

    std::unique_ptr<Ic7610CwCore> m_core;
    std::unique_ptr<Ic7610CwMulti> m_multi;
    QElapsedTimer m_statsTimer;
    QElapsedTimer m_multiTimer;
    bool m_multiDirty = false;

    QFile   m_recFile;
    QString m_recPath;
    qint64  m_recSamples   = 0;
    qint64  m_recMaxSamples = 0;
    quint32 m_recRate      = 0;
    int     m_recMaxSeconds = 60;
    int     m_recLastSec   = -1;
};

class Ic7610CwDecoderPanel : public QWidget
{
    Q_OBJECT
public:
    explicit Ic7610CwDecoderPanel(RxAudioProcessor *rxProc, QWidget *parent = nullptr);
    ~Ic7610CwDecoderPanel() override;

    // Turn decoding off (used when the host window closes).
    void stopDecoding();

    // Re-attach if wfview recreated its RX processor.
    void setRxProcessor(RxAudioProcessor *rxProc);

public slots:
    // CW pitch reported by the radio (CI-V 14 09), in Hz.
    void setRadioPitch(int hz);
    // Text you sent (from the CW Sender), shown inline as "[TX] ...".
    void appendSent(const QString &text);

signals:
    // Emitted every few seconds while decoding in "Follow radio" mode so
    // wfmain can re-read the radio's CW pitch (it is not polled otherwise).
    void radioPitchRequested();

protected:
    void hideEvent(QHideEvent *event) override;

private slots:
    void onTextDecoded(const QString &text);
    void onStatsUpdated(float pitchHz, float speedWpm, float snrDb,
                        bool locked, bool squelchOpen, float sampleRate);
    void onEnableToggled(bool on);
    void applyDecodeSettings();
    void onRecordClicked();
    void onRecordingProgress(const QString &path, int seconds, bool finished);
    void onMultiUpdated(const QVariantList &channels);

private:
    void connectTap();
    void disconnectTap();
    void appendColored(const QString &text, const QColor &color);
    QString clipFolder() const;

    QPointer<RxAudioProcessor> m_rxProc;
    QThread                *m_thread = nullptr;
    Ic7610CwDecoderWorker  *m_worker = nullptr;
    QMetaObject::Connection m_tapConn;

    QPlainTextEdit *m_text        = nullptr;
    QCheckBox      *m_enable      = nullptr;
    QCheckBox      *m_squelchChk  = nullptr;
    QComboBox      *m_pitchMode   = nullptr;
    QSpinBox       *m_pitchSpin   = nullptr;
    QComboBox      *m_speedMode   = nullptr;
    QSpinBox       *m_speedSpin   = nullptr;
    QComboBox      *m_engineCombo = nullptr;
    QPushButton    *m_recordBtn   = nullptr;
    QPushButton    *m_openClipsBtn = nullptr;
    QLabel         *m_recLabel    = nullptr;
    QCheckBox      *m_multiChk    = nullptr;
    QTableWidget   *m_multiTable  = nullptr;
    QPushButton    *m_clearBtn    = nullptr;
    QPushButton    *m_resyncBtn   = nullptr;
    QLabel         *m_status      = nullptr;
    QTimer         *m_pitchPoll   = nullptr;

    int  m_radioPitchHz = 0;
    bool m_recording    = false;
    QString m_txPending;            // sent text, shown as one [TX] line
    QTimer *m_txTimer   = nullptr;
};

// Stand-alone window (used only before wfview's CW Sender window exists).
class Ic7610CwDecoderWindow : public QDialog
{
    Q_OBJECT
public:
    explicit Ic7610CwDecoderWindow(RxAudioProcessor *rxProc, QWidget *parent = nullptr);

    void setRxProcessor(RxAudioProcessor *rxProc);
    Ic7610CwDecoderPanel *panel() const { return m_panel; }

public slots:
    void setRadioPitch(int hz);

signals:
    void radioPitchRequested();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    Ic7610CwDecoderPanel *m_panel = nullptr;
};
