#include "audioscope.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr double kPi = 3.14159265358979323846;

// In-place iterative radix-2 complex FFT; n must be a power of two.
void fftInPlace(std::vector<double> &re, std::vector<double> &im)
{
    const int n = int(re.size());
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / len;
        const double wr = std::cos(ang);
        const double wi = std::sin(ang);
        for (int i = 0; i < n; i += len)
        {
            double cr = 1.0;
            double ci = 0.0;
            const int half = len / 2;
            for (int k = 0; k < half; ++k)
            {
                const int a = i + k;
                const int b = a + half;
                const double tr = re[b] * cr - im[b] * ci;
                const double ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }
}
} // namespace

AudioScope::AudioScope()
{
    m_window.resize(fftSize);
    for (int i = 0; i < fftSize; ++i)
        m_window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * i / (fftSize - 1));
    m_ring.assign(fftSize, 0.0f);
    m_re.assign(fftSize, 0.0);
    m_im.assign(fftSize, 0.0);
    setSampleRate(m_sampleRate);
}

void AudioScope::setSampleRate(double sampleRate)
{
    if (sampleRate < 1000.0)
        sampleRate = 1000.0;
    m_sampleRate = sampleRate;
    setFps(m_fps);
    reset();
}

void AudioScope::setSpanHz(double spanHz)
{
    const double maxSpan = m_sampleRate / 2.0 * 0.98;
    if (spanHz < 200.0)
        spanHz = 200.0;
    if (spanHz > maxSpan)
        spanHz = maxSpan;
    m_spanHz = spanHz;
}

void AudioScope::setFps(int fps)
{
    m_fps = std::max(1, std::min(60, fps));
    m_hop = std::max(1, int(std::lround(m_sampleRate / m_fps)));
}

void AudioScope::reset()
{
    std::fill(m_ring.begin(), m_ring.end(), 0.0f);
    m_filled = 0;
    m_writePos = 0;
    m_sinceLast = 0;
    m_haveFloor = false;
    m_floorDb = -90.0;
    setSpanHz(m_spanHz);
}

uint8_t AudioScope::levelToByte(double db, double floorDb)
{
    const double v = floorByte + (db - floorDb) * ((140.0 - floorByte) / dbRange);
    if (v <= 0.0)
        return 0;
    if (v >= 255.0)
        return 255;
    return uint8_t(std::lround(v));
}

int AudioScope::process(const float *samples, int count, std::vector<Frame> &out)
{
    int produced = 0;
    for (int i = 0; i < count; ++i)
    {
        float s = samples[i];
        if (!std::isfinite(s))
            s = 0.0f;
        m_ring[m_writePos] = s;
        m_writePos = (m_writePos + 1) % fftSize;
        if (m_filled < fftSize)
            ++m_filled;
        if (++m_sinceLast >= m_hop && m_filled >= fftSize)
        {
            m_sinceLast = 0;
            Frame frame;
            runFft(frame);
            out.push_back(std::move(frame));
            ++produced;
        }
    }
    return produced;
}

void AudioScope::runFft(Frame &frame)
{
    for (int j = 0; j < fftSize; ++j)
    {
        m_re[j] = m_window[j] * double(m_ring[(m_writePos + j) % fftSize]);
        m_im[j] = 0.0;
    }
    fftInPlace(m_re, m_im);

    const double hzPerBin = m_sampleRate / fftSize;
    int bins = int(m_spanHz / hzPerBin) + 1;
    bins = std::max(2, std::min(bins, fftSize / 2));

    // Hann window: a full-scale sine gives |X| = N/4.
    const double norm = fftSize / 4.0;
    m_db.resize(bins);
    for (int k = 0; k < bins; ++k)
    {
        const double mag = std::sqrt(m_re[k] * m_re[k] + m_im[k] * m_im[k]) / norm;
        m_db[k] = 20.0 * std::log10(mag + 1e-12);
    }

    // Noise floor: median of the frame, smoothed so the picture does not pump.
    std::vector<double> sorted(m_db.begin() + 1, m_db.end());
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    double median = sorted[sorted.size() / 2];
    if (median < -150.0)
        median = -150.0;
    if (!m_haveFloor)
    {
        m_floorDb = median;
        m_haveFloor = true;
    }
    else
    {
        m_floorDb += 0.1 * (median - m_floorDb);
    }

    frame.hzPerBin = hzPerBin;
    frame.bins.resize(bins);
    for (int k = 0; k < bins; ++k)
        frame.bins[k] = levelToByte(m_db[k], m_floorDb);
}

AudioScopePlacement audioScopePlacement(double dialMHz, double spanHz, bool lowerSideband, bool cw, double pitchHz)
{
    // Audio frequency f is heard from RF = dial + f (upper) or dial - f (lower).
    // In CW the dial shows the frequency of a signal heard at the pitch, so
    // the audio axis is shifted by the pitch.
    const double offset = cw ? pitchHz : 0.0;
    AudioScopePlacement p;
    if (lowerSideband)
    {
        p.startMHz = dialMHz + (offset - spanHz) / 1e6;
        p.endMHz = dialMHz + offset / 1e6;
        p.reversed = true;
    }
    else
    {
        p.startMHz = dialMHz - offset / 1e6;
        p.endMHz = dialMHz + (spanHz - offset) / 1e6;
        p.reversed = false;
    }
    return p;
}

double audioScopeSpanForMode(bool cw, bool am, bool fm)
{
    if (cw)
        return 2000.0;
    if (fm)
        return 8000.0;
    if (am)
        return 5000.0;
    return 3500.0;
}
