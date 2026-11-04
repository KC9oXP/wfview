#pragma once

#include <cstdint>
#include <vector>

// AudioScope: a spectrum/waterfall source made from received audio, for radios
// that do not send scope data over CAT (for example the Yaesu FT-991A).
//
// Qt-free so it can be unit tested. Feed mono float samples (-1..1); every
// hop it produces one frame of amplitude bytes covering 0..spanHz of audio,
// auto-levelled so the noise floor sits near byte 30.
class AudioScope
{
public:
    struct Frame
    {
        std::vector<uint8_t> bins; // bins[0] = 0 Hz audio, last = spanHz
        double hzPerBin = 0.0;
    };

    AudioScope();

    // Sample rate of the samples passed to process(). Resets the analysis.
    void setSampleRate(double sampleRate);
    double sampleRate() const
    {
        return m_sampleRate;
    }

    // Audio range shown (clamped to just below Nyquist). Default 3500 Hz.
    void setSpanHz(double spanHz);
    double spanHz() const
    {
        return m_spanHz;
    }

    // Frames per second (1..60). Default 20.
    void setFps(int fps);

    // Feed samples; returns the number of frames appended to `out`.
    int process(const float *samples, int count, std::vector<Frame> &out);

    // Clear history and the noise-floor tracker.
    void reset();

    static constexpr int fftSize = 4096;
    // Byte value the tracked noise floor is drawn at, and dB per byte step.
    static constexpr int floorByte = 30;
    static constexpr double dbRange = 70.0; // floor+70 dB maps to byte 140

    // Map one dBFS value to a byte given a noise floor in dBFS.
    static uint8_t levelToByte(double db, double floorDb);

private:
    void runFft(Frame &frame);

    double m_sampleRate = 48000.0;
    double m_spanHz = 3500.0;
    int m_fps = 20;
    int m_hop = 2400;
    int m_sinceLast = 0;
    int m_filled = 0;
    int m_writePos = 0;
    bool m_haveFloor = false;
    double m_floorDb = -90.0;

    std::vector<float> m_ring;
    std::vector<double> m_window;
    std::vector<double> m_re;
    std::vector<double> m_im;
    std::vector<double> m_db;
};

// Where an audio-scope frame sits in RF, given the receiver's dial frequency.
struct AudioScopePlacement
{
    double startMHz = 0.0;
    double endMHz = 0.0;
    bool reversed = false; // true: bins must be reversed (lower sideband)
};

// lowerSideband: LSB, CW-L (CW reverse), RTTY-L, DATA-L, PSK-R.
// cw: CW modes, where the dial shows the frequency of a signal heard at pitchHz.
AudioScopePlacement audioScopePlacement(double dialMHz, double spanHz, bool lowerSideband, bool cw, double pitchHz);

// Audio span for a mode family.
double audioScopeSpanForMode(bool cw, bool am, bool fm);
