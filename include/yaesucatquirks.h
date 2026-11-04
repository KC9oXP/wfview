#pragma once

#include <QByteArray>
#include <QtGlobal>

// Small, Qt-GUI-free helpers for the places where Yaesu CAT dialects differ
// from what the rig files can describe. Kept header-only so the unit tests can
// use them without linking the radio commander.
namespace yaesuCatQuirks
{

// Transceiver ID answers ("ID0570;", "ID0670;") as stored in rigCaps.modelID.
constexpr quint16 modelFT991 = 570;
constexpr quint16 modelFT991A = 670;

inline bool isFt991(quint16 modelID)
{
    return modelID == modelFT991 || modelID == modelFT991A;
}

// Yaesu mode registers are the ASCII digit that follows "MD0" ('1'..'9', 'A'..'F').
// The Widths\Bands masks in the rig files use that digit's value as the bit number.
inline int modeBit(quint8 reg)
{
    if (reg >= '0' && reg <= '9')
        return reg - '0';
    if (reg >= 'A' && reg <= 'F')
        return reg - 'A' + 10;
    if (reg >= 'a' && reg <= 'f')
        return reg - 'a' + 10;
    if (reg < 16)
        return reg;
    return -1;
}

inline bool widthAppliesToMode(quint16 bands, quint8 reg)
{
    const int bit = modeBit(reg);
    return bit >= 0 && bit < 16 && ((bands >> bit) & 0x01);
}

// FT-991: split is set with "FT2;" (transmit on VFO-A) or "FT3;" (transmit on
// VFO-B); the radio answers FT0 / FT1, which already match wfview's off / on.
inline int splitSetValue(quint16 modelID, int on)
{
    if (isFt991(modelID))
        return on ? 3 : 2;
    return on;
}

// What an external FT-991 client meant by "FTn;": 0/2 = split off, 1/3 = on.
// Returns -1 for other values.
inline int splitFromExternal(quint16 modelID, int digit)
{
    if (isFt991(modelID))
    {
        if (digit == 2 || digit == 0)
            return 0;
        if (digit == 3 || digit == 1)
            return 1;
        return -1;
    }
    return digit != 0 ? 1 : 0;
}

// CT (tone mode): wfview uses 1 = tone (encode), 2 = tone squelch; the FT-991
// uses CT01 = CTCSS encode/decode (tone squelch), CT02 = CTCSS encode only.
// The mapping is its own inverse. 0 (off), 3 and 4 (DCS) pass through.
inline int ft991ToneModeSwap(int v)
{
    if (v == 1)
        return 2;
    if (v == 2)
        return 1;
    return v;
}

// FT-991 "MT" answers carry a whole channel block (channel, frequency,
// clarifier, mode, tones, shift) with the 12-character tag at the end.
// Offset of the tag after "MT" and the channel number.
constexpr int ft991MemoryTagOffset = 23; // matches hamlib newcat.c (ret_data[28] incl. "MT")
constexpr int ft991MemoryTagLength = 12;

// The channel of a memory read ("MR001;") that the radio answered with "?",
// or -1. `wasMemoryRead`: the last command sent was a memory read.
inline int rejectedMemoryReadChannel(bool wasMemoryRead, const QByteArray &lastSent)
{
    if (!wasMemoryRead || !lastSent.startsWith("MR"))
        return -1;
    QByteArray digits;
    for (int i = 2; i < lastSent.size() && lastSent.at(i) >= '0' && lastSent.at(i) <= '9'; ++i)
        digits.append(lastSent.at(i));
    bool ok = false;
    const int ch = digits.toInt(&ok);
    return (ok && ch > 0) ? ch : -1;
}

// AC P1 P2 P3: the value of P3 that starts a tuning cycle.
inline int tunerTuneValue(quint16 modelID)
{
    return isFt991(modelID) ? 2 : 3;
}

// Map an "AC" answer (P1 P2 P3 as a number) to wfview's 0 = off, 1 = on,
// 2 = tuning. Returns -1 for answers that are not understood.
inline int tunerStatusFromAnswer(quint16 modelID, int answer)
{
    if (isFt991(modelID))
    {
        // FT-991: P1 = P2 = 0; P3 0 = off, 1 = on, 2 = tuning.
        if (answer >= 0 && answer <= 2)
            return answer;
        return -1;
    }

    switch (answer)
    {
    case 0:
        return 0;
    case 10:
    case 20:
        return 1;
    case 11:
    case 23:
        return 2;
    default:
        return -1;
    }
}

// FT-991: the SH width codes for the narrow and wide ranges only work while
// the radio's NAR setting matches, so "NA0n;" has to go before "SH0nn;".
// SSB: codes 01-09 (200-1800 Hz) are narrow. CW, RTTY and DATA: codes 01-10
// (50-500 Hz) are narrow. Returns 1 (narrow), 0 (wide) or -1 (no NA needed).
inline int ft991NarrowFor(quint8 modeReg, int widthCode)
{
    if (widthCode <= 0)
        return -1;

    switch (modeBit(modeReg))
    {
    case 0x1: // LSB
    case 0x2: // USB
        return widthCode <= 9 ? 1 : 0;
    case 0x3: // CW-U
    case 0x6: // RTTY-LSB
    case 0x7: // CW-L
    case 0x8: // DATA-LSB
    case 0x9: // RTTY-USB
    case 0xC: // DATA-USB
        return widthCode <= 10 ? 1 : 0;
    default:
        return -1;
    }
}

// "PL" answers: the FT-710 sends two fields of `bytes` digits and wfview wants
// the second; the FT-991 sends a single field. Returns the offset of the level.
inline int compressorLevelOffset(int answerLength, int bytes)
{
    return (bytes > 0 && answerLength >= 2 * bytes) ? bytes : 0;
}

} // namespace yaesuCatQuirks
