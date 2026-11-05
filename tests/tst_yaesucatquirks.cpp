#include "yaesucatquirks.h"

#include <QSettings>
#include <QtTest>

using namespace yaesuCatQuirks;

class YaesuCatQuirksTest : public QObject
{
    Q_OBJECT

private slots:
    void modeBitUsesTheHexDigit();
    void widthMasksSelectModes();
    void ft991SplitUsesFt2Ft3();
    void tunerTuneValuePerModel();
    void tunerAnswersPerModel();
    void ft991NarrowFollowsWidthCode();
    void compressorLevelOffset();
    void ft991ToneModeIsSwapped();
    void ft991MemoryTagIsAtTheEnd();
    void rejectedMemoryReadIsAnEmptyChannel();
    void memoryAnswerUsesTheRequestedChannel();
    void ft991aRigFileMatchesCat();
};

void YaesuCatQuirksTest::modeBitUsesTheHexDigit()
{
    QCOMPARE(modeBit('1'), 1);
    QCOMPARE(modeBit('9'), 9);
    QCOMPARE(modeBit('A'), 10);
    QCOMPARE(modeBit('C'), 12);
    QCOMPARE(modeBit('F'), 15);
    QCOMPARE(modeBit('Z'), -1);
}

void YaesuCatQuirksTest::widthMasksSelectModes()
{
    // FT-710 / FT-991A SSB mask
    QVERIFY(widthAppliesToMode(0x0006, '1'));
    QVERIFY(widthAppliesToMode(0x0006, '2'));
    QVERIFY(!widthAppliesToMode(0x0006, '3'));
    // FT-710 CW/DATA/PSK mask
    QVERIFY(widthAppliesToMode(0x5188, '3'));
    QVERIFY(widthAppliesToMode(0x5188, 'C'));
    QVERIFY(!widthAppliesToMode(0x5188, '2'));
    // FT-991A CW/RTTY/DATA mask
    for (char reg : {'3', '6', '7', '8', '9', 'C'})
        QVERIFY(widthAppliesToMode(0x13C8, quint8(reg)));
    for (char reg : {'1', '2', '4', '5', 'A', 'B', 'D'})
        QVERIFY(!widthAppliesToMode(0x13C8, quint8(reg)));
}

void YaesuCatQuirksTest::ft991SplitUsesFt2Ft3()
{
    QCOMPARE(splitSetValue(modelFT991A, 1), 3);
    QCOMPARE(splitSetValue(modelFT991A, 0), 2);
    QCOMPARE(splitSetValue(modelFT991, 1), 3);
    QCOMPARE(splitSetValue(800, 1), 1);
    QCOMPARE(splitSetValue(800, 0), 0);
    QCOMPARE(splitFromExternal(modelFT991A, 3), 1);
    QCOMPARE(splitFromExternal(modelFT991A, 2), 0);
    QCOMPARE(splitFromExternal(modelFT991A, 1), 1);
    QCOMPARE(splitFromExternal(modelFT991A, 0), 0);
    QCOMPARE(splitFromExternal(modelFT991A, 5), -1);
    QCOMPARE(splitFromExternal(800, 1), 1);
}

void YaesuCatQuirksTest::tunerTuneValuePerModel()
{
    QCOMPARE(tunerTuneValue(modelFT991A), 2);
    QCOMPARE(tunerTuneValue(800), 3);
}

void YaesuCatQuirksTest::tunerAnswersPerModel()
{
    QCOMPARE(tunerStatusFromAnswer(modelFT991A, 0), 0);
    QCOMPARE(tunerStatusFromAnswer(modelFT991A, 1), 1);
    QCOMPARE(tunerStatusFromAnswer(modelFT991A, 2), 2);
    QCOMPARE(tunerStatusFromAnswer(modelFT991A, 10), -1);
    QCOMPARE(tunerStatusFromAnswer(800, 0), 0);
    QCOMPARE(tunerStatusFromAnswer(800, 10), 1);
    QCOMPARE(tunerStatusFromAnswer(800, 11), 2);
    QCOMPARE(tunerStatusFromAnswer(800, 20), 1);
    QCOMPARE(tunerStatusFromAnswer(800, 23), 2);
    QCOMPARE(tunerStatusFromAnswer(800, 1), -1);
}

void YaesuCatQuirksTest::ft991NarrowFollowsWidthCode()
{
    QCOMPARE(ft991NarrowFor('2', 9), 1); // USB 1800 Hz
    QCOMPARE(ft991NarrowFor('2', 10), 0); // USB 1950 Hz
    QCOMPARE(ft991NarrowFor('1', 21), 0); // LSB 3200 Hz
    QCOMPARE(ft991NarrowFor('3', 10), 1); // CW 500 Hz
    QCOMPARE(ft991NarrowFor('3', 11), 0); // CW 800 Hz
    QCOMPARE(ft991NarrowFor('C', 6), 1); // DATA-U 300 Hz
    QCOMPARE(ft991NarrowFor('C', 17), 0); // DATA-U 3000 Hz
    QCOMPARE(ft991NarrowFor('4', 5), -1); // FM: no SH table
    QCOMPARE(ft991NarrowFor('2', 0), -1);
}

void YaesuCatQuirksTest::compressorLevelOffset()
{
    QCOMPARE(yaesuCatQuirks::compressorLevelOffset(6, 3), 3); // FT-710 "PLxxxyyy"
    QCOMPARE(yaesuCatQuirks::compressorLevelOffset(3, 3), 0); // FT-991 "PLyyy"
}

void YaesuCatQuirksTest::ft991ToneModeIsSwapped()
{
    QCOMPARE(ft991ToneModeSwap(0), 0);
    QCOMPARE(ft991ToneModeSwap(1), 2); // wfview tone -> CT02 (encode)
    QCOMPARE(ft991ToneModeSwap(2), 1); // wfview TSQL -> CT01 (encode/decode)
    QCOMPARE(ft991ToneModeSwap(3), 3);
    QCOMPARE(ft991ToneModeSwap(ft991ToneModeSwap(1)), 1);
}

void YaesuCatQuirksTest::ft991MemoryTagIsAtTheEnd()
{
    // Answer after "MT": channel, frequency, clarifier, RX/TX clar, mode, VFO/MEM,
    // CTCSS, 00, shift, 0, then the 12-character tag.
    const QByteArray d = "001"
                         "446000000"
                         "+0000"
                         "0"
                         "0"
                         "4"
                         "1"
                         "0"
                         "00"
                         "0"
                         "0"
                         "SIMPLEX 446 ";
    QCOMPARE(d.size(), 3 + ft991MemoryTagOffset + ft991MemoryTagLength);
    QCOMPARE(QString::fromLatin1(d.mid(3 + ft991MemoryTagOffset, ft991MemoryTagLength)).trimmed(),
             QStringLiteral("SIMPLEX 446"));
}

void YaesuCatQuirksTest::rejectedMemoryReadIsAnEmptyChannel()
{
    QCOMPARE(rejectedMemoryReadChannel(true, "MR012;"), 12);
    QCOMPARE(rejectedMemoryReadChannel(true, "MR117;"), 117);
    QCOMPARE(rejectedMemoryReadChannel(false, "MR012;"), -1);
    QCOMPARE(rejectedMemoryReadChannel(true, "MT012;"), -1);
    QCOMPARE(rejectedMemoryReadChannel(true, "MR;"), -1);
}

void YaesuCatQuirksTest::memoryAnswerUsesTheRequestedChannel()
{
    QCOMPARE(memoryReadChannel(true, "MR001;"), 1);
    QCOMPARE(memoryReadChannel(true, "MR099;"), 99);
    QCOMPARE(memoryReadChannel(false, "MR001;"), -1);
    QCOMPARE(memoryReadChannel(true, "IF;"), -1);
}

void YaesuCatQuirksTest::ft991aRigFileMatchesCat()
{
    const QString path = QFINDTESTDATA("../rigs/FT-991A.rig");
    if (path.isEmpty())
        QSKIP("rigs/FT-991A.rig not found next to the tests");

    QSettings rig(path, QSettings::IniFormat);
    rig.beginGroup("Rig");
    QCOMPARE(rig.value("Manufacturer").toInt(), 2);
    QCOMPARE(rig.value("Model").toString(), QStringLiteral("FT-991A"));
    QCOMPARE(rig.value("CIVAddress").toInt(), int(modelFT991A));
    QCOMPARE(rig.value("HasSpectrum").toBool(), false);

    QHash<QString, QString> strings;
    QHash<QString, int> bytes;
    QHash<QString, bool> canSet;
    const int commands = rig.beginReadArray("Commands");
    for (int i = 0; i < commands; ++i)
    {
        rig.setArrayIndex(i);
        strings.insert(rig.value("Type").toString(), rig.value("String").toString());
        bytes.insert(rig.value("Type").toString(), rig.value("Bytes").toInt());
        canSet.insert(rig.value("Type").toString(), rig.value("SetCommand").toBool());
    }
    rig.endArray();

    // The FT-991A has no scope, no second mode register, no ST/CF/VS commands.
    for (const auto &s : strings)
    {
        QVERIFY2(!s.startsWith("SS"), qPrintable(s));
        QVERIFY2(s != "MD1" && s != "ST" && !s.startsWith("CF") && !s.startsWith("VS"), qPrintable(s));
    }
    QCOMPARE(strings.value("Split/Duplex"), QStringLiteral("FT"));
    QCOMPARE(strings.value("RIT Status"), QStringLiteral("RT"));
    QCOMPARE(strings.value("Filter Width"), QStringLiteral("SH0"));
    QCOMPARE(bytes.value("Filter Width"), 2);
    QCOMPARE(strings.value("Data Mod Source"), QStringLiteral("EX070"));
    QCOMPARE(strings.value("SSB Mod Source"), QStringLiteral("EX106"));
    QCOMPARE(bytes.value("Power Meter"), 3);
    // MT/MW set formats differ from what wfview writes; read only for now.
    QCOMPARE(canSet.value("Memory Tag"), false);
    QCOMPARE(canSet.value("Memory Write"), false);

    // Every width code is unique per mode and the widths only go up.
    const int widths = rig.beginReadArray("Widths");
    QMap<int, QList<QPair<int, int>>> perMask;
    for (int i = 0; i < widths; ++i)
    {
        rig.setArrayIndex(i);
        perMask[rig.value("Bands").toString().toInt(nullptr, 16)].append(
            {rig.value("Num").toInt(), rig.value("Hz").toInt()});
    }
    rig.endArray();
    QCOMPARE(perMask.value(0x0006).size(), 21);
    QCOMPARE(perMask.value(0x13C8).size(), 17);
    for (const auto &list : perMask)
    {
        for (int i = 1; i < list.size(); ++i)
        {
            QCOMPARE(list[i].first, list[i - 1].first + 1);
            QVERIFY(list[i].second > list[i - 1].second);
        }
    }
    rig.endGroup();
}

int runYaesuCatQuirksTests(int argc, char **argv)
{
    YaesuCatQuirksTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_yaesucatquirks.moc"
