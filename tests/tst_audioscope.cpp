#include "audioscope.h"

#include <QtTest>
#include <cmath>
#include <random>

class AudioScopeTest : public QObject
{
    Q_OBJECT

private slots:
    void toneLandsInTheRightBin();
    void noiseFloorSitsNearFloorByte();
    void spanFollowsSettingAndNyquist();
    void framesComeAtTheRequestedRate();
    void placementUpperLowerAndCw();
    void spanPerMode();
};

namespace
{
std::vector<float> tone(double sr, double hz, double amp, int n, double noiseAmp = 0.0, unsigned seed = 1)
{
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, 1.0);
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = float(amp * std::sin(2.0 * 3.14159265358979323846 * hz * i / sr) + noiseAmp * nd(rng));
    return v;
}
} // namespace

void AudioScopeTest::toneLandsInTheRightBin()
{
    AudioScope s;
    s.setSampleRate(48000.0);
    s.setSpanHz(3500.0);
    auto in = tone(48000.0, 1000.0, 0.3, 48000, 0.001);
    std::vector<AudioScope::Frame> frames;
    s.process(in.data(), int(in.size()), frames);
    QVERIFY(!frames.empty());
    const auto &f = frames.back();
    QVERIFY(std::abs(f.hzPerBin - 48000.0 / AudioScope::fftSize) < 1e-9);
    int peak = 0;
    for (int k = 1; k < int(f.bins.size()); ++k)
        if (f.bins[k] > f.bins[peak])
            peak = k;
    QVERIFY2(std::abs(peak * f.hzPerBin - 1000.0) <= f.hzPerBin, qPrintable(QString::number(peak * f.hzPerBin)));
    QVERIFY(f.bins[peak] > 140); // tone ~78 dB above the noise: above +70 dB (byte 140)
}

void AudioScopeTest::noiseFloorSitsNearFloorByte()
{
    AudioScope s;
    s.setSampleRate(48000.0);
    auto in = tone(48000.0, 1000.0, 0.0, 96000, 0.01);
    std::vector<AudioScope::Frame> frames;
    s.process(in.data(), int(in.size()), frames);
    QVERIFY(!frames.empty());
    std::vector<int> v(frames.back().bins.begin() + 1, frames.back().bins.end());
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    const int median = v[v.size() / 2];
    QVERIFY2(std::abs(median - AudioScope::floorByte) <= 6, qPrintable(QString::number(median)));

    // Level does not matter: 20 dB louder noise gives the same picture.
    AudioScope s2;
    s2.setSampleRate(48000.0);
    auto loud = tone(48000.0, 1000.0, 0.0, 96000, 0.1);
    std::vector<AudioScope::Frame> frames2;
    s2.process(loud.data(), int(loud.size()), frames2);
    std::vector<int> v2(frames2.back().bins.begin() + 1, frames2.back().bins.end());
    std::nth_element(v2.begin(), v2.begin() + v2.size() / 2, v2.end());
    QVERIFY(std::abs(v2[v2.size() / 2] - median) <= 6);
}

void AudioScopeTest::spanFollowsSettingAndNyquist()
{
    AudioScope s;
    s.setSampleRate(48000.0);
    s.setSpanHz(2000.0);
    auto in = tone(48000.0, 700.0, 0.1, 8192);
    std::vector<AudioScope::Frame> frames;
    s.process(in.data(), int(in.size()), frames);
    QVERIFY(!frames.empty());
    const double top = (frames.back().bins.size() - 1) * frames.back().hzPerBin;
    QVERIFY(top >= 2000.0 - frames.back().hzPerBin && top <= 2000.0 + frames.back().hzPerBin);

    AudioScope low;
    low.setSampleRate(8000.0);
    low.setSpanHz(8000.0);
    QVERIFY(low.spanHz() < 4000.0);
}

void AudioScopeTest::framesComeAtTheRequestedRate()
{
    AudioScope s;
    s.setSampleRate(48000.0);
    s.setFps(20);
    auto in = tone(48000.0, 1000.0, 0.1, 48000 * 2);
    std::vector<AudioScope::Frame> frames;
    s.process(in.data(), int(in.size()), frames);
    // Two seconds at 20 fps, minus the first FFT fill (4096 samples).
    QVERIFY2(frames.size() >= 37 && frames.size() <= 40, qPrintable(QString::number(frames.size())));
}

void AudioScopeTest::placementUpperLowerAndCw()
{
    auto usb = audioScopePlacement(14.074, 3000.0, false, false, 0.0);
    QCOMPARE(usb.reversed, false);
    QVERIFY(std::abs(usb.startMHz - 14.074) < 1e-9);
    QVERIFY(std::abs(usb.endMHz - 14.077) < 1e-9);

    auto lsb = audioScopePlacement(7.200, 3000.0, true, false, 0.0);
    QCOMPARE(lsb.reversed, true);
    QVERIFY(std::abs(lsb.startMHz - 7.197) < 1e-9);
    QVERIFY(std::abs(lsb.endMHz - 7.200) < 1e-9);

    // CW-U, pitch 700: a signal on the dial is heard at 700 Hz.
    auto cwu = audioScopePlacement(14.030, 2000.0, false, true, 700.0);
    QVERIFY(std::abs(cwu.startMHz - (14.030 - 0.0007)) < 1e-9);
    QVERIFY(std::abs(cwu.endMHz - (14.030 + 0.0013)) < 1e-9);
    // CW-L: mirror image.
    auto cwl = audioScopePlacement(14.030, 2000.0, true, true, 700.0);
    QCOMPARE(cwl.reversed, true);
    QVERIFY(std::abs(cwl.startMHz - (14.030 - 0.0013)) < 1e-9);
    QVERIFY(std::abs(cwl.endMHz - (14.030 + 0.0007)) < 1e-9);
}

void AudioScopeTest::spanPerMode()
{
    QCOMPARE(audioScopeSpanForMode(false, false, false), 3500.0);
    QCOMPARE(audioScopeSpanForMode(true, false, false), 2000.0);
    QCOMPARE(audioScopeSpanForMode(false, true, false), 5000.0);
    QCOMPARE(audioScopeSpanForMode(false, false, true), 8000.0);
}

int runAudioScopeTests(int argc, char **argv)
{
    AudioScopeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_audioscope.moc"
