// ic7610cwhand.h
//
// IC7610_V51_CW_HANDKEY
// "Hand key" CW decoding engine for the wfview IC-7610 EXT CW decoder.
// Qt-free; single-threaded (runs on the decoder worker thread).
//
// ggmorse fits one speed and classifies with textbook 1:3:7 ratios, which
// falls apart on hand-sent CW. This engine instead:
//
//   1. Key detector: I/Q mix at the tracked pitch, low-pass, magnitude,
//      adaptive signal/noise levels, threshold with hysteresis, glitch filter.
//   2. Timing model: learns THIS sender's dot, dash, element-gap and
//      letter-gap lengths separately and keeps adapting (speed drift,
//      odd dash ratio, squeezed letter gaps). Re-seeded by 2-means
//      clustering of recent marks so a wrong start corrects itself.
//   3. Word decoder: every mark gets a dash probability and every gap a
//      letter-break probability from the learned lengths. At each word gap
//      a dynamic program picks the most likely split into valid characters
//      (e.g. ".-|.." = AI vs ".-.." = L is decided by which reads better).
//      Characters are therefore printed a word at a time.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

class Ic7610CwHandDecoder
{
public:
    Ic7610CwHandDecoder() { buildTable(); reset(); }

    void reset()
    {
        m_decim = 0;
        m_decimAcc = 0.0f;
        m_decimCount = 0;
        m_phase = 0.0;
        m_i1 = m_i2 = m_q1 = m_q2 = 0.0f;
        m_hi = 0.0f;
        m_lo = 0.0f;
        m_levelInit = 0;
        m_keyDown = false;
        m_pending = false;
        m_pendCount = 0;
        m_stateSamples = 0;
        m_wordFlushed = true;
        m_marks.clear();
        m_gaps.clear();
        m_markHist.clear();
        m_held.clear();
        m_dot = 60.0f;
        m_dash = 180.0f;
        m_eg = 60.0f;
        m_lg = 180.0f;
        m_wg = 420.0f;
        m_seeded = false;
        m_lastPitch = -1.0f;
        m_signalValid = false;
    }

    // Feed audio. pitchHz <= 0: no pitch known, samples are ignored.
    // gate = false: no steady tone present (from the pitch tracker); the key
    // is treated as up so band noise cannot create marks.
    // Decoded text (letters and ' ' word breaks) is appended to 'out'.
    void process(const float *x, int n, float sampleRate, float pitchHz, bool gate,
                 std::string &out)
    {
        if (n <= 0 || sampleRate <= 0.0f) return;
        if (m_decim == 0 || sampleRate != m_sampleRate) {
            m_sampleRate = sampleRate;
            m_decim = std::max(1, static_cast<int>(sampleRate / 8000.0f));
            m_fs = sampleRate / static_cast<float>(m_decim);
            m_msPerSample = 1000.0f / m_fs;
            setEnvelopeBandwidth(90.0f);
            m_fastA = 1.0f - std::exp(-1.0f / (0.004f * m_fs));
            m_slowA = 1.0f - std::exp(-1.0f / (2.5f * m_fs));
            m_noiseA = 1.0f - std::exp(-1.0f / (0.08f * m_fs));
            m_glitchSamples = std::max(1, static_cast<int>(0.006f * m_fs));
        }
        if (pitchHz <= 0.0f) return;
        // Envelope bandwidth follows the learned speed: just wide enough for
        // the dits (about 2 / dit length), so less noise gets through on
        // slower CW. 90 Hz until the speed is known.
        setEnvelopeBandwidth(m_seeded ? std::min(90.0f, std::max(25.0f, 2000.0f / m_dot)) : 90.0f);
        if (std::fabs(pitchHz - m_lastPitch) > 30.0f) {
            // New station / pitch: restart the detector, keep timing knowledge.
            m_i1 = m_i2 = m_q1 = m_q2 = 0.0f;
            m_levelInit = 0;
            flushWord(out);
        }
        m_lastPitch = pitchHz;
        const double dPhase = 2.0 * 3.14159265358979 * pitchHz / m_fs;

        for (int k = 0; k < n; ++k) {
            m_decimAcc += x[k];
            if (++m_decimCount < m_decim) continue;
            const float s = m_decimAcc / static_cast<float>(m_decim);
            m_decimAcc = 0.0f;
            m_decimCount = 0;

            m_phase += dPhase;
            if (m_phase > 6.283185307179586) m_phase -= 6.283185307179586;
            const float iRaw = s * static_cast<float>(std::cos(m_phase));
            const float qRaw = s * static_cast<float>(std::sin(m_phase));
            m_i1 += m_lpA * (iRaw - m_i1);
            m_i2 += m_lpA * (m_i1 - m_i2);
            m_q1 += m_lpA * (qRaw - m_q1);
            m_q2 += m_lpA * (m_q1 - m_q2);
            const float mag = std::sqrt(m_i2 * m_i2 + m_q2 * m_q2);

            sample(mag, gate, out);
        }
    }

    // Force out whatever word is in progress.
    void flush(std::string &out) { flushWord(out); }

    // Speed from the average of all four learned elements in dot units
    // (PARIS-style), so a sender with long dashes is not over-reported.
    float speedWpm() const
    {
        if (!m_seeded) return 0.0f;
        const float unit = 0.25f * (m_dot + m_dash / 3.0f + m_eg + m_lg / 3.0f);
        return 1200.0f / std::max(10.0f, unit);
    }
    bool  signalValid() const { return m_signalValid; }

    // Learned timing (ms), for diagnostics.
    float dotMs()  const { return m_dot; }
    float dashMs() const { return m_dash; }
    float elementGapMs() const { return m_eg; }
    float letterGapMs()  const { return m_lg; }

private:
    // ---- key detector ------------------------------------------------------

    void setEnvelopeBandwidth(float fc)
    {
        if (std::fabs(fc - m_lpFc) < 2.0f) return;
        m_lpFc = fc;
        m_lpA = 1.0f - std::exp(-2.0f * 3.14159265f * fc / m_fs);
    }

    void sample(float mag, bool gate, std::string &out)
    {
        // Signal level (hi): fast up, slow down, so it holds between marks.
        // Noise level (lo): the AVERAGE envelope while the key is up. (A
        // minimum tracker would sit near zero on noise and make noise look
        // like a strong signal.)
        if (m_levelInit < static_cast<int>(0.05f * m_fs)) {
            if (m_levelInit == 0) { m_hi = mag; m_lo = mag; }
            m_hi = std::max(m_hi, mag);
            m_lo += (mag - m_lo) / static_cast<float>(m_levelInit + 1);
            ++m_levelInit;
        }
        if (mag > m_hi) m_hi += m_fastA * (mag - m_hi);
        else            m_hi += m_slowA * (mag - m_hi);
        if (!m_keyDown && mag < m_lo + 0.33f * (m_hi - m_lo)) m_lo += m_noiseA * (mag - m_lo);

        m_signalValid = gate && (m_hi > 3.0f * m_lo + 1e-9f);
        const float span = m_hi - m_lo;
        const float onTh  = std::max(m_lo + 0.50f * span, 2.5f * m_lo);
        const float offTh = std::max(m_lo + 0.33f * span, 1.8f * m_lo);

        ++m_stateSamples;
        const bool wantDown = m_keyDown ? (mag >= offTh) : (mag > onTh && m_signalValid);

        if (wantDown != m_keyDown) {
            if (!m_pending) { m_pending = true; m_pendCount = 0; }
            if (++m_pendCount >= m_glitchSamples) {
                // Transition happened m_pendCount samples ago.
                const float durMs = (m_stateSamples - m_pendCount) * m_msPerSample;
                if (m_keyDown) onMarkEnd(durMs);
                else           onSpaceEnd(durMs);
                m_keyDown = wantDown;
                m_stateSamples = m_pendCount;
                m_pending = false;
            }
        } else {
            m_pending = false;
        }

        // Word end by timeout while key is up.
        if (!m_keyDown && !m_wordFlushed && !m_marks.empty()) {
            const float upMs = m_stateSamples * m_msPerSample;
            if (upMs > wordGapThreshold()) {
                flushWord(out);
                out.push_back(' ');
            }
        }
    }

    void onMarkEnd(float ms)
    {
        if (ms < 20.0f) return;     // click / noise blip (20 ms = dit at 60 WPM)
        if (m_seeded && ms < 0.3f * m_dot) {                  // noise spike: merge
            return;
        }
        if (ms > 2000.0f) { m_marks.clear(); m_gaps.clear(); return; }   // carrier
        m_marks.push_back(ms);
        m_wordFlushed = false;

        m_markHist.push_back(ms);
        if (m_markHist.size() > 40) m_markHist.erase(m_markHist.begin());
        if (m_markHist.size() >= 6 && (!m_seeded || m_markHist.size() % 8 == 0)) reseed();
    }

    void onSpaceEnd(float ms)
    {
        // A gap between two marks of the same word (word gaps are handled by
        // the timeout in sample()).
        if (!m_marks.empty() && !m_wordFlushed) m_gaps.push_back(ms);
    }

    float wordGapThreshold() const
    {
        return std::max(std::sqrt(m_lg * m_wg), m_lg * 2.0f);
    }

    // 2-means on log mark lengths; seeds dot/dash from the actual sender.
    void reseed()
    {
        std::vector<float> l;
        for (float v : m_markHist) l.push_back(std::log(v));
        float c0 = *std::min_element(l.begin(), l.end());
        float c1 = *std::max_element(l.begin(), l.end());
        if (c1 - c0 < std::log(1.6f)) return;   // all one kind so far
        for (int it = 0; it < 10; ++it) {
            float s0 = 0, s1 = 0; int n0 = 0, n1 = 0;
            const float mid = 0.5f * (c0 + c1);
            for (float v : l) { if (v < mid) { s0 += v; ++n0; } else { s1 += v; ++n1; } }
            if (n0 == 0 || n1 == 0) return;
            c0 = s0 / n0; c1 = s1 / n1;
        }
        const float dot = std::exp(c0), dash = std::exp(c1);
        if (dash / dot < 1.6f) return;
        const float w = m_seeded ? 0.3f : 1.0f;
        m_dot  += w * (dot - m_dot);
        m_dash += w * (dash - m_dash);
        if (!m_seeded) {
            m_eg = m_dot;
            m_lg = 0.5f * (m_dash + 3.0f * m_dot);
            m_wg = 7.0f * m_dot;
        }
        m_seeded = true;
    }

    // ---- word decoder ------------------------------------------------------

    static float sigm(float z) { return 1.0f / (1.0f + std::exp(-z)); }
    static float clampP(float p) { return std::min(0.99f, std::max(0.01f, p)); }

    void flushWord(std::string &out)
    {
        m_wordFlushed = true;
        const int n = static_cast<int>(m_marks.size());
        if (n == 0) { m_gaps.clear(); return; }
        if (!m_seeded) {
            // Too early to know dot vs dash: hold the word (a few at most)
            // and decode it once the timing model is seeded.
            if (m_held.size() < 6) m_held.push_back({m_marks, m_gaps});
            m_marks.clear(); m_gaps.clear();
            return;
        }
        if (!m_held.empty()) {
            std::vector<HeldWord> held;
            held.swap(m_held);
            const std::vector<float> curM = m_marks, curG = m_gaps;
            for (const HeldWord &w : held) {
                m_marks = w.marks; m_gaps = w.gaps;
                decodeWord(out);
                out.push_back(' ');
            }
            m_marks = curM; m_gaps = curG;
        }
        decodeWord(out);
    }

    void decodeWord(std::string &out)
    {
        const int n = static_cast<int>(m_marks.size());
        if (n == 0) return;
        while (static_cast<int>(m_gaps.size()) > n - 1) m_gaps.pop_back();
        while (static_cast<int>(m_gaps.size()) < n - 1) m_gaps.push_back(m_eg);

        // Soft classifications.
        const float rM = std::max(1.5f, m_dash / m_dot);
        const float kM = 5.9f / std::log(rM);
        const float thM = std::sqrt(m_dot * m_dash);
        std::vector<float> pDash(n);
        for (int i = 0; i < n; ++i)
            pDash[i] = clampP(sigm(kM * (std::log(m_marks[i]) - std::log(thM))));

        const float rG = std::max(1.5f, m_lg / m_eg);
        const float kG = 5.9f / std::log(rG);
        const float thG = std::sqrt(m_eg * m_lg);
        std::vector<float> pSplit(std::max(0, n - 1));
        for (int j = 0; j < n - 1; ++j)
            pSplit[j] = clampP(sigm(kG * (std::log(m_gaps[j]) - std::log(thG))));

        // DP over split points: best[i] = best cost to decode marks [0, i).
        const float INF = 1e30f;
        std::vector<float> best(n + 1, INF);
        std::vector<int> from(n + 1, -1);
        std::vector<int> pick(n + 1, -1);
        best[0] = 0.0f;
        for (int i = 1; i <= n; ++i) {
            for (int L = 1; L <= kMaxLen && L <= i; ++L) {
                const int s = i - L;
                if (best[s] >= INF) continue;
                float cost = best[s];
                if (s > 0) cost += -std::log(pSplit[s - 1]);
                for (int j = s; j < i - 1; ++j) cost += -std::log(1.0f - pSplit[j]);
                int bestC = -1; float bestCc = INF;
                for (int c = 0; c < static_cast<int>(m_table.size()); ++c) {
                    const Entry &e = m_table[c];
                    if (e.len != L) continue;
                    float cc = e.prior;
                    for (int m = 0; m < L; ++m)
                        cc += -std::log(e.code[m] == '-' ? pDash[s + m] : 1.0f - pDash[s + m]);
                    if (cc < bestCc) { bestCc = cc; bestC = c; }
                }
                if (bestC < 0) continue;
                cost += bestCc + kLetterCost;
                if (cost < best[i]) { best[i] = cost; from[i] = s; pick[i] = bestC; }
            }
        }

        if (best[n] >= INF) {
            out += "?";
        } else {
            std::vector<int> picks, starts;
            for (int i = n; i > 0; i = from[i]) { picks.push_back(pick[i]); starts.push_back(from[i]); }
            std::reverse(picks.begin(), picks.end());
            std::reverse(starts.begin(), starts.end());
            for (int c : picks) out += m_table[c].text;
            learn(picks, starts);
        }

        m_marks.clear();
        m_gaps.clear();
    }

    // Decision-directed update of the timing model from the decoded word.
    void learn(const std::vector<int> &picks, const std::vector<int> &starts)
    {
        const float a = 0.12f;
        for (size_t p = 0; p < picks.size(); ++p) {
            const Entry &e = m_table[picks[p]];
            const int s = starts[p];
            for (int m = 0; m < e.len; ++m) {
                const float d = m_marks[s + m];
                if (e.code[m] == '-') m_dash += a * (d - m_dash);
                else                  m_dot  += a * (d - m_dot);
                if (m < e.len - 1) m_eg += a * (m_gaps[s + m] - m_eg);
            }
            if (s > 0) m_lg += a * (m_gaps[s - 1] - m_lg);
        }
        // Keep the model sane.
        m_dot  = std::min(400.0f, std::max(15.0f, m_dot));
        m_dash = std::min(m_dot * 5.0f, std::max(m_dot * 1.8f, m_dash));
        m_eg   = std::min(m_dot * 2.0f, std::max(m_dot * 0.4f, m_eg));
        m_lg   = std::min(m_dot * 6.0f, std::max(m_eg * 1.6f, m_lg));
        m_wg   = std::max(m_lg * 2.0f, 6.0f * m_dot);
    }

    // ---- character table ---------------------------------------------------

    struct Entry { char code[8]; int len; std::string text; float prior; };
    static constexpr int   kMaxLen     = 6;
    static constexpr float kLetterCost = 0.8f;   // discourages needless splitting

    void buildTable()
    {
        struct Raw { const char *code; const char *text; float prior; };
        static const Raw raw[] = {
            {".-","A",0}, {"-...","B",0}, {"-.-.","C",0}, {"-..","D",0}, {".","E",0},
            {"..-.","F",0}, {"--.","G",0}, {"....","H",0}, {"..","I",0}, {".---","J",0.5f},
            {"-.-","K",0}, {".-..","L",0}, {"--","M",0}, {"-.","N",0}, {"---","O",0},
            {".--.","P",0}, {"--.-","Q",0}, {".-.","R",0}, {"...","S",0}, {"-","T",0},
            {"..-","U",0}, {"...-","V",0}, {".--","W",0}, {"-..-","X",0}, {"-.--","Y",0},
            {"--..","Z",0.3f},
            {".----","1",0.3f}, {"..---","2",0.3f}, {"...--","3",0.3f}, {"....-","4",0.3f},
            {".....","5",0.3f}, {"-....","6",0.3f}, {"--...","7",0.3f}, {"---..","8",0.3f},
            {"----.","9",0.3f}, {"-----","0",0.3f},
            {"..--..","?",1.0f}, {".-.-.-",".",1.5f}, {"--..--",",",1.5f}, {"-..-.","/",1.0f},
            {"-...-","=",1.0f}, {".-.-.","+",1.0f}, {"-....-","-",2.0f},
            {"...-.-","<SK>",1.5f}, {"-.--.","<KN>",1.0f},
        };
        m_table.clear();
        for (const Raw &r : raw) {
            Entry e;
            std::memset(e.code, 0, sizeof(e.code));
            e.len = static_cast<int>(std::strlen(r.code));
            std::memcpy(e.code, r.code, e.len);
            e.text = r.text;
            e.prior = r.prior;
            m_table.push_back(e);
        }
    }

    std::vector<Entry> m_table;

    // detector state
    float  m_sampleRate = 0.0f;
    int    m_decim = 0;
    float  m_fs = 8000.0f;
    float  m_msPerSample = 0.125f;
    float  m_decimAcc = 0.0f;
    int    m_decimCount = 0;
    double m_phase = 0.0;
    float  m_lpFc = 0.0f;
    float  m_lpA = 0.0f, m_fastA = 0.0f, m_slowA = 0.0f, m_noiseA = 0.0f;
    float  m_i1 = 0, m_i2 = 0, m_q1 = 0, m_q2 = 0;
    float  m_hi = 0, m_lo = 0;
    int    m_levelInit = 0;
    bool   m_signalValid = false;
    bool   m_keyDown = false;
    bool   m_pending = false;
    int    m_pendCount = 0;
    int    m_glitchSamples = 48;
    long   m_stateSamples = 0;
    float  m_lastPitch = -1.0f;

    // word in progress
    bool m_wordFlushed = true;
    std::vector<float> m_marks;
    std::vector<float> m_gaps;
    std::vector<float> m_markHist;
    struct HeldWord { std::vector<float> marks, gaps; };
    std::vector<HeldWord> m_held;

    // timing model (ms)
    float m_dot = 60.0f, m_dash = 180.0f, m_eg = 60.0f, m_lg = 180.0f, m_wg = 420.0f;
    bool  m_seeded = false;
};
