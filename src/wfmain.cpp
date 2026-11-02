#include "wfmain.h"
#include "icomserver.h"
#include "ui_wfmain.h"
#include "ic7610cwdecoder.h"   // IC7610_V49_CW_DECODER
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QScrollArea>
#include <QProgressBar> // IC7610_V27_PROGRESS_INCLUDE
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>

#include "rigidentities.h"
#include "logcategories.h"


// This code is copyright 2017-2024 Elliott H. Liggett and Phil E. Taylor
// All rights reserved

// Log support:
QScopedPointer<QFile> m_logFile;
QMutex logMutex;
QMutex logTextMutex;
QScopedPointer<QTextStream> logStream;

QVector<QPair<QtMsgType,QString>> logStringBuffer;
#ifdef QT_DEBUG
bool debugModeLogging = true;
#else
bool debugModeLogging = false;
#endif

bool insaneDebugLogging = false;
bool rigctlDebugLogging = false;

wfmain::wfmain(const QString settingsFile, const QString logFile, bool debugMode, QWidget *parent ) :
    QMainWindow(parent),
    ui(new Ui::wfmain),
    logFilename(logFile)
{
    QGuiApplication::setApplicationDisplayName("wfview");
    QGuiApplication::setApplicationName(QString("wfview"));

    setWindowIcon(QIcon( QString(":resources/wfview.png")));
    this->debugMode = debugMode;
    debugModeLogging = debugMode;
    ui->setupUi(this);
    // IC7610_V33_OEM_METER_SCALING
    // The .ui gives the three stock meters a Preferred horizontal policy.
    // Make them consume the width of the meter column instead of remaining
    // near their 280 px size hint as the main window grows.
    ui->meterSPoWidget->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->meter2Widget->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->meter3Widget->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->meterSPoWidget->setMinimumWidth(280);
    ui->meter2Widget->setMinimumWidth(280);
    ui->meter3Widget->setMinimumWidth(280);
    // Let the stock meters grow enough to be easy to read, but do not allow
    // the old 255-unit artwork to become a giant horizontally stretched banner.
    ui->meterSPoWidget->setMaximumWidth(620);
    ui->meter2Widget->setMaximumWidth(620);
    ui->meter3Widget->setMaximumWidth(620);

    // Give the meter column the spare horizontal space while leaving the
    // tuning/control columns at their natural sizes.
    ui->horizontalLayout_2->setStretch(0, 0); // IC7610_V36_OEM_WIDTH_CAP
    ui->horizontalLayout_2->setStretch(1, 0);
    ui->horizontalLayout_2->setStretch(2, 0);
    setWindowTitle(QString("wfview"));

    ui->monitorLabel->setText("Mon");
    // IC7610_V37_POWER_BUTTONS
    // Match the compact visual weight of the Vd/Id meter pair.
    ui->rigPowerOnBtn->setMinimumWidth(100);
    ui->rigPowerOnBtn->setMaximumWidth(260);
    ui->rigPowerOnBtn->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->rigPowerOffBtn->setMinimumWidth(100);
    ui->rigPowerOffBtn->setMaximumWidth(260);
    ui->rigPowerOffBtn->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Fixed);

    ui->horizontalLayout_27->setSpacing(4);
    ui->horizontalLayout_27->setStretch(0, 1);
    ui->horizontalLayout_27->setStretch(1, 1);
    ui->horizontalLayout_27->addStretch(1);

    // Accessibility: the operating buttons are NoFocus in the .ui so the tuning
    // controls keep keyboard focus. That leaves keyboard-only and VoiceOver
    // users unable to Tab to them. Make the navigation/dialog buttons reachable
    // by Tab, but deliberately leave the transmit-hazardous buttons (Transmit,
    // Tune, Power On/Off) as NoFocus so a stray Space/Enter can't key the rig.
    const QList<QPushButton*> tabFocusButtons = {
        ui->cwButton, ui->rptSetupBtn, ui->memoriesBtn,
        ui->dualWatchBtn, ui->scopeDualBtn, ui->scopeMainSubBtn,
        ui->splitBtn, ui->swapMainSubBtn, ui->mainEqualsSubBtn,
        ui->aboutBtn, ui->showSettingsBtn, ui->saveSettingsBtn,
        ui->radioStatusBtn, ui->showLogBtn, ui->showBandsBtn,
        ui->showFreqBtn, ui->rigCreatorBtn, ui->TXaudioProcBtn,
        ui->RXaudioProcBtn, ui->connectBtn, ui->exitBtn
    };
    for (QPushButton* btn : tabFocusButtons)
        btn->setFocusPolicy(Qt::StrongFocus);


    logWindow = new loggingWindow(logFile);
    initLogging();
    logWindow->setInitialDebugState(debugMode);
    qInfo(logSystem()).noquote() << QString("wfview version: %1 (Git:%2 on %3 at %4 by %5@%6)")
                          .arg(QString(WFVIEW_VERSION),GITSHORT,__DATE__,__TIME__,UNAME,HOST);

    qInfo(logSystem()).noquote() << QString("Operating System: %0 (%1)").arg(QSysInfo::prettyProductName(),QSysInfo::buildCpuArchitecture());
    qInfo(logSystem()).noquote() << "Looking for External Dependencies:";
    qInfo(logSystem()).noquote() << QString("QT Runtime Version: %0").arg(qVersion());
    if (strncmp(QT_VERSION_STR, qVersion(),sizeof(QT_VERSION_STR)))
    {
        qWarning(logSystem()).noquote() << QString("QT Build Version Mismatch: %0").arg(QT_VERSION_STR);
    }

    qInfo(logSystem()).noquote()  << QString("OPUS Version: %0").arg(opus_get_version_string());

#ifdef HID_API_VERSION_MAJOR
    qInfo(logSystem()).noquote() << QString("HIDAPI Version: %0.%1.%2")
            .arg(HID_API_VERSION_MAJOR)
            .arg(HID_API_VERSION_MINOR)
            .arg(HID_API_VERSION_PATCH);

    if (HID_API_VERSION != HID_API_MAKE_VERSION(hid_version()->major, hid_version()->minor, hid_version()->patch)) {
        qWarning(logSystem()).noquote() << QString("HIDAPI Version mismatch: %0.%1.%2")
                .arg(hid_version()->major)
                .arg(hid_version()->minor)
                .arg(hid_version()->patch);
    }
#endif

#ifdef EIGEN_WORLD_VERSION
    qInfo(logSystem()).noquote() << QString("EIGEN Version: %0.%1.%2").arg(EIGEN_WORLD_VERSION).arg(EIGEN_MAJOR_VERSION).arg(EIGEN_MINOR_VERSION);
#endif

#ifdef QCUSTOMPLOT_VERSION_STR
    qInfo(logSystem()).noquote() << QString("QCUSTOMPLOT Version: %0").arg(QCUSTOMPLOT_VERSION_STR);
#endif

#ifdef RTAUDIO_VERSION
    qInfo(logSystem()).noquote() << QString("RTAUDIO Version: %0").arg(RTAUDIO_VERSION);
    if (RTAUDIO_VERSION != RtAudio::getVersion())
    {
        qWarning(logSystem()).noquote() << QString("RTAUDIO Version Mismatch: %0").arg(RtAudio::getVersion().c_str());
    }
#endif

    qInfo(logSystem()).noquote() << QString("PORTAUDIO Version: %0").arg(Pa_GetVersionText());

    cal = new calibrationWindow();
    rpt = new repeaterSetup();
    sat = new satelliteSetup();
    abtBox = new aboutbox();
    selRad = new selectRadio();
    bandbtns = new bandbuttons();

    finputbtns = new frequencyinputwidget();
    setupui = new settingswidget();

    connect(setupui, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));


    qRegisterMetaType<udpPreferences>(); // Needs to be registered early.
    qRegisterMetaType<manufacturersType_t>();
    qRegisterMetaType<connectionType_t>();
    qRegisterMetaType<rigCapabilities>();
    qRegisterMetaType<duplexMode_t>();
    qRegisterMetaType<rptAccessTxRx_t>();
    qRegisterMetaType<rptrAccessData>();
    qRegisterMetaType<toneInfo>();
    qRegisterMetaType<rigInput>();
    qRegisterMetaType<inputTypes>();
    qRegisterMetaType<meter_t>();
    qRegisterMetaType<meterkind>();
    qRegisterMetaType<freqt>();
    qRegisterMetaType<vfo_t>();
    qRegisterMetaType<modeInfo>();
    qRegisterMetaType<rigMode_t>();
    qRegisterMetaType<pttType_t>();
    qRegisterMetaType<audioPacket>();
    qRegisterMetaType<audioSetup>();
    qRegisterMetaType<SERVERCONFIG>();
    qRegisterMetaType<timekind>();
    qRegisterMetaType<datekind>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QVector<BUTTON>*>();
    qRegisterMetaType<QVector<KNOB>*>();
    qRegisterMetaType<QVector<COMMAND>*>();
    qRegisterMetaType<const COMMAND*>();
    qRegisterMetaType<const USBDEVICE*>();
    qRegisterMetaType<QList<radio_cap_packet>>();
    qRegisterMetaType<QList<spotData>>();
    qRegisterMetaType<networkStatus>();
    qRegisterMetaType<networkAudioLevels>();
    qRegisterMetaType<codecType>();
    qRegisterMetaType<errorType>();
    qRegisterMetaType<usbFeatureType>();
    qRegisterMetaType<funcs>();
    qRegisterMetaType<rigTypedef>();
    qRegisterMetaType<memoryType>();
    qRegisterMetaType<memoryTagType>();
    qRegisterMetaType<memorySplitType>();
    qRegisterMetaType<antennaInfo>();
    qRegisterMetaType<scopeData>();
    qRegisterMetaType<queueItem>();
    qRegisterMetaType<cacheItem>();
    qRegisterMetaType<spectrumBounds>();
    qRegisterMetaType<centerSpanData>();
    qRegisterMetaType<bandStackType>();
    qRegisterMetaType<widthsType>();
    qRegisterMetaType<yaesu_scope_data>();
    qRegisterMetaType<rigInfo>();
    qRegisterMetaType<lpfhpf>();

    this->setObjectName("wfmain");
    queue = cachingQueue::getInstance(this);

    connect(queue,SIGNAL(sendValue(cacheItem)),this,SLOT(receiveValue(cacheItem)));
    connect(queue,SIGNAL(sendMessage(QString)),this,SLOT(showStatusBarText(QString)));
    connect(queue,SIGNAL(intervalUpdated(qint64)),this,SLOT(updatedQueueInterval(qint64)));

    connect(queue, SIGNAL(finished()), queue, SLOT(deleteLater()));

    // Disable all controls until connected.
    enableControls(false);

    // We need to populate the list of rigs as early as possible so do it now

    // Setup the connectiontimer as we may need it soon!
    ConnectionTimer.setSingleShot(true);
    connect(&ConnectionTimer,SIGNAL(timeout()), this,SLOT(connectionTimeout()));

    // Make sure we know about any changes to rigCaps
    connect(queue, SIGNAL(rigCapsUpdated(rigCapabilities*)), this, SLOT(receiveRigCaps(rigCapabilities*)));

    setupKeyShortcuts();

    setupMainUI();
    connectSettingsWidget();

    setSerialDevicesUI();

    setDefPrefs();


    getSettingsFilePath(settingsFile);

    // Derive the per-radio ANR noise profile store path from the settings file.
    // For an explicit --settings file: place the .noise file in the same directory
    // with the same base name (e.g. ic7610.conf → ic7610.noise).
    // For the default QSettings (macOS plist / Windows registry / Linux ~/.config):
    // use AppDataLocation so the file always lands in a real writable directory.
    {
        const QString sf = settings->fileName();
        const QFileInfo fi(sf);
        // Test for an ordinary file path — registry keys don't have a readable suffix.
        if (!sf.isEmpty() && fi.suffix().length() >= 2 && fi.suffix().length() <= 5
                && fi.absoluteDir().exists()) {
            m_noiseStorePath = fi.absoluteDir().absoluteFilePath(fi.baseName() + ".noise");
        } else {
            QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (appData.isEmpty())
                appData = QDir::homePath();
            QDir().mkpath(appData);
            m_noiseStorePath = appData + "/wfview.noise";
        }
    }

    setDefaultColorPresets();

    loadSettings(); // Look for saved preferences
    logWindow->ingestSettings(prefs);

    setManufacturer(prefs.manufacturer);
    setServerToPrefs();

    //setAudioDevicesUI(); // no need to call this as it will be called by the updated() signal

    setTuningSteps(); // TODO: Combine into preferences

    changeFullScreenMode(prefs.useFullScreen);
    useSystemTheme(prefs.useSystemTheme);

    // Don't update prefs until system theme is loaded.
    // This is so we can popup any dialog boxes in the correct theme.
    setupui->updateIfPrefs((int)if_all);
    setupui->updateColPrefs((int)col_all);
    setupui->updateRaPrefs((int)ra_all);
    setupui->updateRsPrefs((int)rs_all); // Most of these come from the rig anyway
    setupui->updateCtPrefs((int)ct_all);
    setupui->updateClusterPrefs((int)cl_all);
    setupui->updateLanPrefs((int)l_all);
    setupui->updateUdpPrefs((int)u_all);
    setupui->updateServerConfigs((int)s_all);

    finputbtns->setAutomaticSidebandSwitching(prefs.automaticSidebandSwitching);


    qDebug(logSystem()) << "Running setInitialTiming()";
    setInitialTiming();

    fts = new FirstTimeSetup();

    if(prefs.hasRunSetup) {
        qDebug(logSystem()) << "Running openRig()";
        openRig();
    } else {
        qInfo(logSystem()) << "Detected first-time run. Showing the First Time Setup widget.";

        connect(fts, &FirstTimeSetup::exitProgram, this, [=]() {
            qInfo(logSystem()) << "User elected exit program.";
            prefs.settingsChanged = false;
            prefs.confirmExit = false;
            QTimer::singleShot(10, this, [&](){
                on_exitBtn_clicked();
            });
        });
        connect(fts, &FirstTimeSetup::showSettings, this, [=](const bool networkEnabled) {
            qInfo(logSystem()) << "User elected to visit the Settings UI.";
            prefs.enableLAN = networkEnabled;
            setupui->updateLanPrefs((int)l_all);
            showAndRaiseWidget(setupui);
            prefs.settingsChanged = true;
            prefs.hasRunSetup = true;
        });
        connect(fts, &FirstTimeSetup::skipSetup, this, [=]() {
            qInfo(logSystem()) << "User elected to skip the setup. Marking setup complete.";
            prefs.settingsChanged = true;
            prefs.hasRunSetup = true;
        });

        fts->exec();
    }

    cluster = new dxClusterClient();

    clusterThread = new QThread(this);
    clusterThread->setObjectName("dxcluster()");

    cluster->moveToThread(clusterThread);

    connect(this, SIGNAL(setClusterEnableUdp(bool)), cluster, SLOT(enableUdp(bool)));
    connect(this, SIGNAL(setClusterEnableTcp(bool)), cluster, SLOT(enableTcp(bool)));
    connect(this, SIGNAL(setClusterUdpPort(int)), cluster, SLOT(setUdpPort(int)));
    connect(this, SIGNAL(setClusterServerName(QString)), cluster, SLOT(setTcpServerName(QString)));
    connect(this, SIGNAL(setClusterTcpPort(int)), cluster, SLOT(setTcpPort(int)));
    connect(this, SIGNAL(setClusterUserName(QString)), cluster, SLOT(setTcpUserName(QString)));
    connect(this, SIGNAL(setClusterPassword(QString)), cluster, SLOT(setTcpPassword(QString)));
    connect(this, SIGNAL(setClusterTimeout(int)), cluster, SLOT(setTcpTimeout(int)));
    connect(this, SIGNAL(setClusterSkimmerSpots(bool)), cluster, SLOT(enableSkimmerSpots(bool)));

    connect(clusterThread, SIGNAL(finished()), cluster, SLOT(deleteLater()));

    clusterThread->start();

    emit setClusterUdpPort(prefs.clusterUdpPort);
    emit setClusterEnableUdp(prefs.clusterUdpEnable);

    for (int f = 0; f < prefs.clusters.size(); f++)
    {
        if (prefs.clusters[f].isdefault)
        {
            emit setClusterServerName(prefs.clusters[f].server);
            emit setClusterTcpPort(prefs.clusters[f].port);
            emit setClusterUserName(prefs.clusters[f].userName);
            emit setClusterPassword(prefs.clusters[f].password);
            emit setClusterTimeout(prefs.clusters[f].timeout);
            emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        }
    }
    emit setClusterEnableTcp(prefs.clusterTcpEnable);

    amTransmitting = false;

    setupLambdaSlots();

#if defined(USB_CONTROLLER)
    #if defined(USB_HOTPLUG) && defined(Q_OS_LINUX)
        uDev = udev_new();
        if (!uDev)
        {
            qInfo(logUsbControl()) << "Cannot register udev, hotplug of USB devices is not available";
            return;
        }
        uDevMonitor = udev_monitor_new_from_netlink(uDev, "udev");
        if (!uDevMonitor)
        {
            qInfo(logUsbControl()) << "Cannot register udev_monitor, hotplug of USB devices is not available";
            return;
        }
        int fd = udev_monitor_get_fd(uDevMonitor);
        uDevNotifier = new QSocketNotifier(fd, QSocketNotifier::Read,this);
        connect(uDevNotifier, SIGNAL(activated(int)), this, SLOT(uDevEvent()));
        udev_monitor_enable_receiving(uDevMonitor);
    #endif
#endif

    // Add right-click menu
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    connect (this, &QMainWindow::customContextMenuRequested,this,&::wfmain::popupScreenMenu);
    screenMenu = new QMenu(this);

    QLabel *header = new QLabel("Enable Display");
    header->setStyleSheet("QLabel {border-bottom: 1px solid; padding:5px;}");
    QWidgetAction* headerAction = new QWidgetAction(screenMenu);
    headerAction->setDefaultWidget(header);
    screenMenu->addAction(headerAction);

    QList<QWidget *> items = this->findChildren<QWidget *>();

    for (const auto &g : items)
    {
        if (g->objectName() !="" && g->isVisible())
        {
            QCheckBox *chk = new QCheckBox(g->objectName(),screenMenu);
            chk->setChecked(true);
            chk->setStyleSheet("QCheckBox {padding:5px;}");

#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
            QObject::connect(chk, &QCheckBox::checkStateChanged, this, [g](Qt::CheckState state){
#else
            QObject::connect(chk, &QCheckBox::stateChanged, this, [g](int state){
#endif
                g->setVisible(state);
            });

            QWidgetAction *chkAction = new QWidgetAction(screenMenu);
            chkAction->setDefaultWidget(chk);
            screenMenu->addAction(chkAction);
        }
    }

    for (int i=1;i<5;i++)
    {
        //QAction* act = new QAction(QString("Action %0").arg(i),this);
        //act->setCheckable(true);
        //screenMenu->addAction(act);
    }

}

void wfmain::popupScreenMenu(QPoint pos)
{
    screenMenu->popup(this->mapToGlobal(pos));
}

wfmain::~wfmain()
{
    if(rigThread != Q_NULLPTR) {
        rigThread->quit();
        rigThread->wait();
    }

    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
    }

    // Delete all shortcuts
    for (const auto &s: shortcuts)
    {
        delete s;
    }
    shortcuts.clear();

    // Each rig needs deleting before we close.
    for (auto &rig: serverConfig.rigs) {
        delete rig;
    }

    if (clusterThread != Q_NULLPTR) {
        clusterThread->quit();
        clusterThread->wait();
    }

    if (tciThread != Q_NULLPTR) {
        tciThread->quit();
        tciThread->wait();
    }

    if (rigCtl != Q_NULLPTR) {
        delete rigCtl;
    }

    if (colorPrefs != Q_NULLPTR) {
        delete colorPrefs;
    }

    delete rpt;
    delete ui;
    delete settings;
#if defined(USB_CONTROLLER)
    if (usbControllerThread != Q_NULLPTR) {
        usbControllerThread->quit();
        usbControllerThread->wait();
    }
    #if defined(Q_OS_LINUX)
        if (uDevMonitor)
        {
            udev_monitor_unref(uDevMonitor);
            udev_unref(uDev);
            delete uDevNotifier;
        }
    #endif

#endif

    logStream->flush();
}

void wfmain::closeEvent(QCloseEvent *event)
{
    if (on_exitBtn_clicked())
        event->ignore();

}

void wfmain::openRig()
{
    // This function is intended to handle opening a connection to the rig.
    // the connection can be either serial or network,
    // and this function is also responsible for initiating the search for a rig model and capabilities.
    // Any errors, such as unable to open connection or unable to open port, are to be reported to the user.

    //TODO: if(hasRunPreviously)

    //TODO: if(useNetwork){...

    // } else {

    // if (prefs.fileWasNotFound) {
    //     showRigSettings(); // rig setting dialog box for network/serial, CIV, hostname, port, baud rate, serial device, etc
    // TODO: How do we know if the setting was loaded?

    emit connectionStatus(true); // Signal any other parts that need to know if we are connecting/connected.
    ui->connectBtn->setText("Cancel connection"); // We are attempting to connect
    connStatus = connConnecting;
    isRadioAdmin = true; // Set user to admin, will be reset if not.
    // M0VSE: This could be in a better place maybe?
    if (prefs.audioSystem == tciAudio)
    {
        prefs.rxSetup.tci = tci;
        prefs.txSetup.tci = tci;
    }

    // Attach TX audio processor to the TX input setup
    if (!txProc) {
        txProc = new TxAudioProcessor(this);
        // Wire meter, sidetone and spectrum signals to the AudioProc widget (if already open)
        if (audioProcWin) {
            connect(txProc, &TxAudioProcessor::txInputLevel,
                    audioProcWin, &AudioProcessingWidget::updateInputLevel);
            connect(txProc, &TxAudioProcessor::txOutputLevel,
                    audioProcWin, &AudioProcessingWidget::updateOutputLevel);

            connect(txProc, &TxAudioProcessor::txInputLevels,
                    audioProcWin, &AudioProcessingWidget::updateInputLevels);
            connect(txProc, &TxAudioProcessor::txOutputLevels,
                    audioProcWin, &AudioProcessingWidget::updateOutputLevels);

            connect(txProc, &TxAudioProcessor::txGainReduction,
                    audioProcWin, &AudioProcessingWidget::updateGainReduction);
            connect(txProc, &TxAudioProcessor::txSpectrumBins,
                    audioProcWin, &AudioProcessingWidget::onSpectrumBins);
        }
    }
    applyAudioProcPrefs(prefs.txAudioProc);
    prefs.txSetup.txProc = txProc;

    if (prefs.audioSystem == rtAudio)
        txProc->setSidetoneDelay(2);
    else
        txProc->setSidetoneDelay(0);

    // Attach RX audio processor to the RX output setup
    if (!rxProc) {
        rxProc = new RxAudioProcessor(this);
        rxProc->setNoiseStorePath(m_noiseStorePath);
    }
    applyRxAudioProcPrefs(prefs.rxAudioProc);
    prefs.rxSetup.rxProc = rxProc;

    makeRig();


    if (prefs.enableLAN)
    {
        // "We need to setup the tx/rx audio:
        udpPrefs.waterfallFormat = prefs.waterfallFormat;
        // 60 second connection timeout.
        ConnectionTimer.start(60000);
        emit sendCommSetup(rigList, prefs.radioCIVAddr, udpPrefs, prefs.rxSetup, prefs.txSetup, prefs.virtualSerialPort, prefs.tcpPort);
    } else {
        if( (prefs.serialPortRadio.toLower() == QString("auto")))
        {
            findSerialPort();
        } else {
            serialPortRig = prefs.serialPortRadio;
        }
        emit sendCommSetup(rigList, prefs.radioCIVAddr, serialPortRig, prefs.serialPortBaud,prefs.virtualSerialPort, prefs.tcpPort,prefs.waterfallFormat);
        ui->statusBar->showMessage(QString("Connecting to rig using serial port ").append(serialPortRig), 1000);

    }


}

// Deprecated (moved to makeRig())
void wfmain::rigConnections()
{
}

void wfmain::makeRig()
{
    if (rigThread == Q_NULLPTR)
    {
        switch (prefs.manufacturer){
        case manufIcom:
            rig = new icomCommander();
            break;
        case manufKenwood:
            rig = new kenwoodCommander();
            break;
        case manufYaesu:
            rig = new yaesuCommander();
            break;
        default:
            qCritical() << "Unknown Manufacturer, aborting...";
            break;
        }

        rigThread = new QThread(this);
        rigThread->setObjectName("rigCommander()");

        // Thread:
        rig->moveToThread(rigThread);
        connect(rigThread, SIGNAL(started()), rig, SLOT(process()));
        connect(rigThread, SIGNAL(finished()), rig, SLOT(deleteLater()));
        rigThread->start();

        // Rig status and Errors:
        connect(rig, SIGNAL(havePortError(errorType)), this, SLOT(receivePortError(errorType)));
        connect(rig, SIGNAL(haveStatusUpdate(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));
        connect(rig, SIGNAL(haveNetworkAudioLevels(networkAudioLevels)), this, SLOT(receiveNetworkAudioLevels(networkAudioLevels)));

        connect(rig, SIGNAL(requestRadioSelection(QList<radio_cap_packet>)), this, SLOT(radioSelection(QList<radio_cap_packet>)));
        connect(rig, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), selRad, SLOT(setInUse(quint8, bool, quint8, QString, QString)));
        connect(rig, SIGNAL(setRadioUsage(quint8, bool, quint8, QString, QString)), this, SLOT(radioInUse(quint8, bool, quint8, QString, QString)));
        connect(selRad, SIGNAL(selectedRadio(quint8)), rig, SLOT(setCurrentRadio(quint8)));
        // Rig comm setup:
        connect(this, SIGNAL(sendCommSetup(rigTypedef,quint16, udpPreferences, audioSetup, audioSetup, QString, quint16)), rig, SLOT(commSetup(rigTypedef,quint16, udpPreferences, audioSetup, audioSetup, QString, quint16)));
        connect(this, SIGNAL(sendCommSetup(rigTypedef,quint16, QString, quint32,QString, quint16,quint8)), rig, SLOT(commSetup(rigTypedef,quint16, QString, quint32,QString, quint16,quint8)));
        connect(this, SIGNAL(setPTTType(pttType_t)), rig, SLOT(setPTTType(pttType_t)));

        connect(rig, SIGNAL(haveBaudRate(quint32)), this, SLOT(receiveBaudRate(quint32)));

        connect(this, SIGNAL(sendCloseComm()), rig, SLOT(closeComm()));
        connect(this, SIGNAL(sendChangeLatency(quint16)), rig, SLOT(changeLatency(quint16)));
        connect(this, SIGNAL(setRigID(quint16)), rig, SLOT(setRigID(quint16)));

        connect(rig, SIGNAL(commReady()), this, SLOT(receiveCommReady()));

        if (serverConfig.enabled) {
            qInfo(logRigServer()) << "**** Connecting rig instance to server";
            connect(rig, SIGNAL(haveAudioData(audioPacket)), server, SLOT(receiveAudioData(audioPacket)));
            connect(rig, SIGNAL(haveDataForServer(QByteArray)), server, SLOT(dataForServer(QByteArray)));
            connect(server, SIGNAL(haveDataFromServer(QByteArray)), rig, SLOT(dataFromServer(QByteArray)));
        }

        connect(this, SIGNAL(setCIVAddr(quint16)), rig, SLOT(setCIVAddr(quint16)));
        connect(this, SIGNAL(sendPowerOn()), rig, SLOT(powerOn()));
        connect(this, SIGNAL(sendPowerOff()), rig, SLOT(powerOff()));
        connect(this, SIGNAL(getDebug()), rig, SLOT(getDebug()));

        connect(rig, SIGNAL(haveRptOffsetFrequency(freqt)), rpt, SLOT(handleRptOffsetFrequency(freqt)));

        connect(this->rpt, &repeaterSetup::getTone, this->rig,
                [=]() {
            qDebug(logSystem()) << "Asking for TONE";
            queue->add(priorityImmediate,funcRepeaterTone,false,false);});

        connect(this->rpt, &repeaterSetup::getTSQL, this->rig,
                [=]() {
            qDebug(logSystem()) << "Asking for TSQL";
            queue->add(priorityImmediate,funcRepeaterTSQL,false,false);});

        connect(this->rpt, &repeaterSetup::setDTCS, this->rig,
                [=](const toneInfo& t) {
            qDebug(logSystem()) << "Setting DCS, code =" << t.tone;
            queue->add(priorityImmediate,queueItem(funcRepeaterDTCS,QVariant::fromValue<toneInfo>(t),false));});

        connect(this->rpt, &repeaterSetup::getRptAccessMode, this->rig,
                [=]() {
                    if (rigCaps->commands.contains(funcToneSquelchType)) {
                        queue->add(priorityImmediate,funcToneSquelchType,false,false);
                    } else {
                        queue->add(priorityImmediate,funcRepeaterTone,false,false);
                        queue->add(priorityImmediate,funcRepeaterTSQL,false,false);
                    }
                });


        connect(this->rig, &rigCommander::haveDuplexMode, this->rpt,
                [=](const duplexMode_t &dm) {
                    if(dm==dmSplitOn)
                        this->splitModeEnabled = true;
                    else
                        this->splitModeEnabled = false;
                });

        connect(this->rpt, &repeaterSetup::selectVFO, this->rig,
                [=](const vfo_t &v) { queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(v),false));});

        connect(this->rpt, &repeaterSetup::equalizeVFOsAB, this->rig,
                [=]() { queue->add(priorityImmediate,funcVFOEqualAB,false,false);});

        connect(this->rpt, &repeaterSetup::equalizeVFOsMS, this->rig,
                [=]() {  queue->add(priorityImmediate,funcVFOEqualMS,false,false);});

        connect(this->rpt, &repeaterSetup::swapVFOs, this->rig,
                [=]() {  queue->add(priorityImmediate,funcVFOSwapMS,false,false);});

        connect(this->rpt, &repeaterSetup::setRptDuplexOffset, this->rig,
                [=](const freqt &fOffset) {
            queue->add(priorityImmediate,queueItem(funcSendFreqOffset,QVariant::fromValue<freqt>(fOffset),false));});

        connect(this->rpt, &repeaterSetup::getRptDuplexOffset, this->rig,
                [=]() {  queue->add(priorityImmediate,funcReadFreqOffset,false,false);});

        // Create link for server so it can have easy access to rig.
        if (serverConfig.rigs.first() != Q_NULLPTR) {
            serverConfig.rigs.first()->rig = rig;
            serverConfig.rigs.first()->rigThread = rigThread;
        }
    }
}

void wfmain::removeRig()
{
    ConnectionTimer.stop();

    if (rigThread != Q_NULLPTR)
    {
        qDebug(logSystem()) << "[SHUTDOWN] rigThread->quit()";
        rigThread->quit();
        qDebug(logSystem()) << "[SHUTDOWN] rigThread->wait() ...";
        rigThread->wait();
        qDebug(logSystem()) << "[SHUTDOWN] rigThread finished";
        rig = Q_NULLPTR;
        rigThread = Q_NULLPTR;
    }

    if (cw != Q_NULLPTR)
    {
        prefs.cwCutNumbers = cw->getCutNumbers();
        prefs.cwSendImmediate = cw->getSendImmediate();
        prefs.cwSidetoneEnabled = cw->getSidetoneEnable();
        prefs.cwSidetoneLevel = cw->getSidetoneLevel();
        prefs.cwMacroList = cw->getMacroText();

        delete cw;
        cw = Q_NULLPTR;
        ui->cwButton->setEnabled(false);
    }

    for (const auto &receiver:receivers) {
        delete receiver;
    }
    receivers.clear();
    currentReceiver=0;
    // Disable all controls until connected.
    enableControls(false);

}


void wfmain::findSerialPort()
{
    // Find the ICOM radio connected, or, if none, fall back to OS default.
    // qInfo(logSystem()) << "Searching for serial port...";
    bool found = false;
    // First try to find first Icom port:
    for(const QSerialPortInfo & serialPortInfo: QSerialPortInfo::availablePorts())
    {
        if (serialPortInfo.serialNumber().left(3) == "IC-") {
            qInfo(logSystem()) << "Serial Port found: " << serialPortInfo.portName() << "Manufacturer:" << serialPortInfo.manufacturer() << "Product ID" << serialPortInfo.description() << "S/N" << serialPortInfo.serialNumber();
#if defined(Q_OS_LINUX) || defined(Q_OS_MAC)
            serialPortRig = (QString("/dev/") + serialPortInfo.portName());
#else
            serialPortRig = serialPortInfo.portName();
#endif
            found = true;
            break;
        }
    }

    if (!found) {
        QDirIterator it73("/dev/serial/by-id", QStringList() << "*IC-7300*", QDir::Files, QDirIterator::Subdirectories);
        QDirIterator it97("/dev/serial", QStringList() << "*IC-9700*A*", QDir::Files, QDirIterator::Subdirectories);
        QDirIterator it785x("/dev/serial", QStringList() << "*IC-785*A*", QDir::Files, QDirIterator::Subdirectories);
        QDirIterator it705("/dev/serial", QStringList() << "*IC-705*A", QDir::Files, QDirIterator::Subdirectories);
        QDirIterator it7610("/dev/serial", QStringList() << "*IC-7610*A", QDir::Files, QDirIterator::Subdirectories);
        QDirIterator itR8600("/dev/serial", QStringList() << "*IC-R8600*A", QDir::Files, QDirIterator::Subdirectories);

        if(!it73.filePath().isEmpty())
        {
            // IC-7300
            serialPortRig = it73.filePath(); // first
        } else if(!it97.filePath().isEmpty())
        {
            // IC-9700
            serialPortRig = it97.filePath();
        } else if(!it785x.filePath().isEmpty())
        {
            // IC-785x
            serialPortRig = it785x.filePath();
        } else if(!it705.filePath().isEmpty())
        {
            // IC-705
            serialPortRig = it705.filePath();
        } else if(!it7610.filePath().isEmpty())
        {
            // IC-7610
            serialPortRig = it7610.filePath();
        } else if(!itR8600.filePath().isEmpty())
        {
            // IC-R8600
            serialPortRig = itR8600.filePath();
        }
        else {
            //fall back:
            qInfo(logSystem()) << "Could not find Icom serial port. Falling back to OS default. Use --port to specify, or modify preferences.";
#ifdef Q_OS_MAC
            serialPortRig = QString("/dev/tty.SLAB_USBtoUART");
#endif
#ifdef Q_OS_LINUX
            serialPortRig = QString("/dev/ttyUSB0");
#endif
#ifdef Q_OS_WIN
            serialPortRig = QString("COM1");
#endif
        }
    }
}

void wfmain::receiveCommReady()
{
    //qInfo(logSystem()) << "Received CommReady!! ";
    if(!prefs.enableLAN)
    {
        // If we're not using the LAN, then we're on serial, and
        // we already know the baud rate and can calculate the timing parameters.
        calculateTimingParameters();
    }
    if(prefs.radioCIVAddr == 0)
    {
        // tell rigCommander to broadcast a request for all rig IDs.
        // qInfo(logSystem()) << "Beginning search from wfview for rigCIV (auto-detection broadcast)";
        ui->statusBar->showMessage(QString("Searching CI-V bus for connected radios."), 1000);

        queue->addUnique(priorityHigh,funcTransceiverId,true);
    } else {
        // don't bother, they told us the CIV they want, stick with it.
        // We still query the rigID to find the model, but at least we know the CIV.
        qInfo(logSystem()) << QString("Skipping automatic CIV, using user-supplied value of 0x%1").arg(prefs.radioCIVAddr,2,16) ;
        showStatusBarText(QString("Using user-supplied radio CI-V address of 0x%1").arg(prefs.radioCIVAddr, 2, 16));
        if(prefs.CIVisRadioModel)
        {
            qInfo(logSystem()) << QString("Skipping Rig ID query, using user-supplied model from CI-V address: 0x%1").arg(prefs.radioCIVAddr,2,16) ;
            emit setCIVAddr(prefs.radioCIVAddr);
            emit setRigID(prefs.radioCIVAddr);
        } else {
            emit setCIVAddr(prefs.radioCIVAddr);
            queue->addUnique(priorityHigh,funcTransceiverId,true);

        }
    }
    if (prefs.autoPowerOn)
    {
        // After 2s send powerOn command
        QTimer::singleShot(2000, rig, SLOT(powerOn()));

    }
}

void wfmain::receivePortError(errorType err)
{
    if (err.alert) {
        connectionHandler(false); // Force disconnect
        QMessageBox::critical(this, err.device, QString("%0: %1").arg(err.device,err.message), QMessageBox::Ok);
    }
    else
    {
        qInfo(logSystem()) << "wfmain: received error for device: " << err.device << " with message: " << err.message;
        ui->statusBar->showMessage(QString("ERROR: using device ").append(err.device).append(": ").append(err.message), 10000);
    }
    // TODO: Dialog box, exit, etc
}

void wfmain::receiveStatusUpdate(networkStatus status)
{

    if (queue->interval()==-1)
    {
        return;
    }

    // If we have received very high network latency, increase queue interval, don't set it to higher than 500ms though.
    if (prefs.polling_ms != 0 && queue->interval() == quint32(delayedCmdIntervalLAN_ms) && (status.networkLatency/2) > quint32(delayedCmdIntervalLAN_ms))
    {
        qInfo(logRig()) << QString("1/2 network latency %0 exceeds configured queue interval %1, increasing").arg(status.networkLatency/2).arg(delayedCmdIntervalLAN_ms);
        queue->interval(qMin(quint32(status.networkLatency/2),quint32(500)));
    }
    else if (prefs.polling_ms != 0 && queue->interval() != quint32(delayedCmdIntervalLAN_ms) && (status.networkLatency/2) < quint32(delayedCmdIntervalLAN_ms))
    {
        // Maybe it was a temporary issue, restore to default
        queue->interval(delayedCmdIntervalLAN_ms);
    }

    //this->rigStatus->setText(QString("%0/%1 %2").arg(mainElapsed).arg(subElapsed).arg(status.message));
    this->rigStatus->setText(status.message);
    selRad->audioOutputLevel(status.rxAudioLevel);
    selRad->audioInputLevel(status.txAudioLevel);
    selRad->addTimeDifference(status.timeDifference);
    //qInfo(logSystem()) << "Got Status Update" << status.rxAudioLevel;
}

void wfmain::receiveNetworkAudioLevels(networkAudioLevels l)
{
    meter_t m = meterNone;
    if(l.haveRxLevels)
    {
        m = meterRxAudio;
        receiveMeter(m, l.rxAudioPeak);
    }
    if(l.haveTxLevels)
    {
        m = meterTxMod;
        receiveMeter(m, l.txAudioPeak);
    }
}

void wfmain::setupMainUI()
{
    // Set scroll wheel response (tick interval)
    // and set arrow key response (single step)
    ui->rfGainSlider->setTickInterval(100);
    ui->rfGainSlider->setSingleStep(10);

    ui->afGainSlider->setTickInterval(100);
    ui->afGainSlider->setSingleStep(10);

    ui->sqlSlider->setTickInterval(100);
    ui->sqlSlider->setSingleStep(10);

    ui->txPowerSlider->setTickInterval(100);
    ui->txPowerSlider->setSingleStep(10);

    ui->micGainSlider->setTickInterval(100);
    ui->micGainSlider->setSingleStep(10);

    ui->monitorSlider->setTickInterval(100);
    ui->monitorSlider->setSingleStep(10);

    // Keep this code when the rest is removed from this function:
    qDebug(logSystem()) << "Running with debugging options enabled.";


    rigStatus = new QLabel(this);
    ui->statusBar->addPermanentWidget(rigStatus);
    ui->statusBar->showMessage("Connecting to rig...", 1000);

    pttLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(pttLed);
    pttLed->setState(QLedLabel::State::StateOk);
    pttLed->setToolTip("Receiving");

    connectedLed = new QLedLabel(this);
    ui->statusBar->addPermanentWidget(connectedLed);

    rigName = new QLabel(this);
    rigName->setAlignment(Qt::AlignRight);
    ui->statusBar->addPermanentWidget(rigName);
    rigName->setText("NONE");
    rigName->setFixedWidth(60);

    freqt f;
    f.MHzDouble = 0.0;
    f.Hz = 0;

    oldFreqDialVal = ui->freqDial->value();

    ui->tuneLockChk->setChecked(false);
    freqLock = false;

    QComboBox *agcSelCombo = new QComboBox(ui->OtherControlsGrp);
    agcSelCombo->setObjectName(QStringLiteral("agcSelCombo"));
    agcSelCombo->setEnabled(false);
    agcSelCombo->setMinimumWidth(68);
    agcSelCombo->setToolTip(tr("Automatic Gain Control: FAST, MID, or SLOW"));
    agcSelCombo->setAccessibleName(tr("Automatic Gain Control"));

    // IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// IC-7610 AGC control: add a third row to "Other Controls".
// v16: AGC lives on the stock upper Other Controls row: NB / NR / IP+.
    QLabel *agcLabel = new QLabel(tr("AGC"), ui->OtherControlsGrp);
    agcLabel->setToolTip(tr("Automatic Gain Control speed"));
    agcSelCombo->setMinimumWidth(68);
    agcSelCombo->setMaximumWidth(90);

    ui->horizontalLayout_6->addSpacing(6);
    ui->horizontalLayout_6->addWidget(agcLabel);
    ui->horizontalLayout_6->addWidget(agcSelCombo);
    // IC7610_V32_VA_ROW
    QWidget *ic7610VaMeterRow = new QWidget(ui->mainGroup);
    ic7610VaMeterRow->setObjectName(QStringLiteral("ic7610VaMeterRow"));
    ic7610VaMeterRow->setMinimumHeight(24);
    ic7610VaMeterRow->setMaximumHeight(30);

    QHBoxLayout *ic7610VaLayout = new QHBoxLayout(ic7610VaMeterRow);
    ic7610VaLayout->setContentsMargins(2, 0, 2, 0);
    ic7610VaLayout->setSpacing(4);

    QLabel *ic7610VdLabel = new QLabel(tr("Vd"), ic7610VaMeterRow);
    ic7610VdLabel->setObjectName(QStringLiteral("ic7610VdLabel"));
    ic7610VdLabel->setMinimumWidth(20);
    ic7610VdLabel->setMaximumWidth(24);
    ic7610VdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ic7610VaLayout->addWidget(ic7610VdLabel);

    QProgressBar *ic7610VdBar = new QProgressBar(ic7610VaMeterRow);
    ic7610VdBar->setObjectName(QStringLiteral("ic7610VdBar"));
    ic7610VdBar->setRange(100, 160);
    ic7610VdBar->setValue(100);
    ic7610VdBar->setTextVisible(false);
    ic7610VdBar->setMinimumWidth(100);
    ic7610VdBar->setMaximumWidth(260);
    ic7610VdBar->setMinimumHeight(12);
    ic7610VdBar->setMaximumHeight(16);
    ic7610VdBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ic7610VdBar->setToolTip(tr("IC-7610 supply voltage, 10.0 to 16.0 V"));
    ic7610VaLayout->addWidget(ic7610VdBar, 1);

    QLabel *ic7610VdText = new QLabel(tr("--.- V"), ic7610VaMeterRow);
    ic7610VdText->setObjectName(QStringLiteral("ic7610VdText"));
    ic7610VdText->setMinimumWidth(56);
    ic7610VdText->setMaximumWidth(64);
    ic7610VdText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ic7610VaLayout->addWidget(ic7610VdText);

    ic7610VaLayout->addSpacing(8);

    QLabel *ic7610IdLabel = new QLabel(tr("Id"), ic7610VaMeterRow);
    ic7610IdLabel->setObjectName(QStringLiteral("ic7610IdLabel"));
    ic7610IdLabel->setMinimumWidth(18);
    ic7610IdLabel->setMaximumWidth(22);
    ic7610IdLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ic7610VaLayout->addWidget(ic7610IdLabel);

    QProgressBar *ic7610IdBar = new QProgressBar(ic7610VaMeterRow);
    ic7610IdBar->setObjectName(QStringLiteral("ic7610IdBar"));
    ic7610IdBar->setRange(0, 300);
    ic7610IdBar->setValue(0);
    ic7610IdBar->setTextVisible(false);
    ic7610IdBar->setMinimumWidth(100);
    ic7610IdBar->setMaximumWidth(260);
    ic7610IdBar->setMinimumHeight(12);
    ic7610IdBar->setMaximumHeight(16);
    ic7610IdBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ic7610IdBar->setToolTip(tr("IC-7610 drain current, 0 to 30 A"));
    ic7610VaLayout->addWidget(ic7610IdBar, 1);

    QLabel *ic7610IdText = new QLabel(tr("--.- A"), ic7610VaMeterRow);
    ic7610IdText->setObjectName(QStringLiteral("ic7610IdText"));
    ic7610IdText->setMinimumWidth(56);
    ic7610IdText->setMaximumWidth(64);
    ic7610IdText->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ic7610VaLayout->addWidget(ic7610IdText);

    // Keep the pair compact on very wide main windows.
    ic7610VaLayout->addStretch(1);

    ui->meterLayout->addWidget(ic7610VaMeterRow);
    // IC7610_V23_HIDE_LEGACY
    agcLabel->setVisible(false);
    agcSelCombo->setVisible(false);
// IC7610_EXT_DIALOG_BEGIN
    QPushButton *ext7610Btn = new QPushButton(tr("7610 EXT"), ui->OtherControlsGrp);
    ext7610Btn->setObjectName(QStringLiteral("ext7610Btn"));
    ext7610Btn->setToolTip(tr("Open IC-7610 extended radio controls"));
    ext7610Btn->setEnabled(false);
    ext7610Btn->setVisible(false);
    ui->horizontalLayout_6->addWidget(ext7610Btn);

    // IC7610_V49_CW_DECODER_BTN
    {
        // IC7610_V50_CW_BTN_BOTTOM_ROW
        // Bottom button row, right after "RX Audio Proc". Not inside
        // OtherControlsGrp, so it stays enabled while disconnected.
        QPushButton *cwDecodeBtn = new QPushButton(tr("CW Decode"),
                                                   ui->RXaudioProcBtn->parentWidget());
        cwDecodeBtn->setObjectName(QStringLiteral("ic7610CwDecodeBtn"));
        cwDecodeBtn->setToolTip(tr("Open the CW (Morse) decoder"));
        cwDecodeBtn->setSizePolicy(ui->RXaudioProcBtn->sizePolicy());
        {
            const int rxProcIdx = ui->horizontalLayout_16->indexOf(ui->RXaudioProcBtn);
            if (rxProcIdx >= 0)
                ui->horizontalLayout_16->insertWidget(rxProcIdx + 1, cwDecodeBtn);
            else
                ui->horizontalLayout_16->addWidget(cwDecodeBtn);
        }
        connect(cwDecodeBtn, &QPushButton::clicked, this, [this]() {
            auto *win = findChild<Ic7610CwDecoderWindow*>(
                    QStringLiteral("ic7610CwDecoderWin"), Qt::FindDirectChildrenOnly);
            if (!win) {
                win = new Ic7610CwDecoderWindow(rxProc, this);
                win->setObjectName(QStringLiteral("ic7610CwDecoderWin"));
                connect(win, &Ic7610CwDecoderWindow::radioPitchRequested, this, [this]() {
                    if (queue != Q_NULLPTR && connStatus == connConnected)
                        queue->add(priorityImmediate, funcCwPitch, false, false);
                });
            } else {
                win->setRxProcessor(rxProc);
            }
            win->show();
            win->raise();
            win->activateWindow();
        });
    }

    QDialog *ext7610Dialog = new QDialog(this);
    ext7610Dialog->setObjectName(QStringLiteral("ext7610Dialog"));
    ext7610Dialog->setWindowTitle(tr("IC-7610 Extended Controls"));
    ext7610Dialog->setModal(false);
    ext7610Dialog->setMinimumSize(560, 430);
    ext7610Dialog->resize(760, 640);

    QVBoxLayout *extRootLayout = new QVBoxLayout(ext7610Dialog);

    QLabel *ext7610ReceiverLabel = new QLabel(tr("Receiver: MAIN"), ext7610Dialog);
    ext7610ReceiverLabel->setObjectName(QStringLiteral("ext7610ReceiverLabel"));
    extRootLayout->addWidget(ext7610ReceiverLabel);

    QLabel *ext7610HintLabel = new QLabel(
        tr("Values are read from the radio on connect/open/refresh. "
           "Documented controls use radio/front-panel scales; fields still marked raw are CI-V values whose engineering scale is not documented."),
        ext7610Dialog);
    ext7610HintLabel->setWordWrap(true);
    extRootLayout->addWidget(ext7610HintLabel);

    QTabWidget *ext7610Tabs = new QTabWidget(ext7610Dialog);
    ext7610Tabs->setObjectName(QStringLiteral("ext7610Tabs"));
    extRootLayout->addWidget(ext7610Tabs);

    auto extMakeSpin = [=](const char *name, int minimum, int maximum, bool raw) -> QSpinBox* {
        QSpinBox *w = new QSpinBox(ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setRange(minimum, maximum);
        w->setEnabled(false);
        if (raw)
            w->setSuffix(tr(" raw"));
        return w;
    };

    auto extMakeCheck = [=](const char *name) -> QCheckBox* {
        QCheckBox *w = new QCheckBox(tr("ON"), ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setEnabled(false);
        return w;
    };

    // ---------------- RX / DSP tab ----------------
    QWidget *extRxPage = new QWidget();
    QFormLayout *extRxForm = new QFormLayout(extRxPage);
    extRxForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QScrollArea *extRxScroll = new QScrollArea(ext7610Tabs);
    extRxScroll->setObjectName(QStringLiteral("extRxScroll"));
    extRxScroll->setWidgetResizable(true);
    extRxScroll->setFrameShape(QFrame::NoFrame);
    extRxScroll->setWidget(extRxPage);
    ext7610Tabs->addTab(extRxScroll, tr("RX / DSP"));

    QComboBox *agcTcCombo = new QComboBox(ext7610Dialog);
    agcTcCombo->setObjectName(QStringLiteral("agcTcCombo"));
    agcTcCombo->setEnabled(false);
    agcTcCombo->addItem(tr("0 - OFF"), 0);
    agcTcCombo->addItem(tr("1 - 0.1 s"), 1);
    agcTcCombo->addItem(tr("2 - 0.2 s"), 2);
    agcTcCombo->addItem(tr("3 - 0.3 s"), 3);
    agcTcCombo->addItem(tr("4 - 0.5 s"), 4);
    agcTcCombo->addItem(tr("5 - 0.8 s"), 5);
    agcTcCombo->addItem(tr("6 - 1.2 s"), 6);
    agcTcCombo->addItem(tr("7 - 1.6 s"), 7);
    agcTcCombo->addItem(tr("8 - 2.0 s"), 8);
    agcTcCombo->addItem(tr("9 - 2.5 s"), 9);
    agcTcCombo->addItem(tr("10 - 3.0 s"), 10);
    agcTcCombo->addItem(tr("11 - 4.0 s"), 11);
    agcTcCombo->addItem(tr("12 - 5.0 s"), 12);
    agcTcCombo->addItem(tr("13 - 6.0 s"), 13);
    agcTcCombo->setCurrentIndex(-1);
    agcTcCombo->setToolTip(tr("SSB/CW/RTTY/PSK time shown; AM uses longer times for the same index."));
    extRxForm->addRow(tr("AGC Time Constant"), agcTcCombo);

    QComboBox *apfModeCombo = new QComboBox(ext7610Dialog);
    apfModeCombo->setObjectName(QStringLiteral("apfModeCombo"));
    apfModeCombo->setEnabled(false);
    apfModeCombo->addItem(tr("OFF"), 0);
    apfModeCombo->addItem(tr("WIDE"), 1);
    apfModeCombo->addItem(tr("MID"), 2);
    apfModeCombo->addItem(tr("NAR"), 3);
    apfModeCombo->setCurrentIndex(-1);
    extRxForm->addRow(tr("APF Mode / Width"), apfModeCombo);

    QSpinBox *apfPositionSpin = extMakeSpin("apfPositionSpin", -550, 550, false);
    apfPositionSpin->setSingleStep(10);
    apfPositionSpin->setSuffix(tr(" Hz"));
    extRxForm->addRow(tr("APF Position"), apfPositionSpin);

    QComboBox *apfTypeCombo = new QComboBox(ext7610Dialog);
    apfTypeCombo->setObjectName(QStringLiteral("apfTypeCombo"));
    apfTypeCombo->setEnabled(false);
    apfTypeCombo->addItem(tr("SHARP"), 0);
    apfTypeCombo->addItem(tr("SOFT"), 1);
    apfTypeCombo->setCurrentIndex(-1);
    extRxForm->addRow(tr("APF Type"), apfTypeCombo);

    QSpinBox *apfAfLevelSpin = extMakeSpin("apfAfLevelSpin", 0, 6, false);
    apfAfLevelSpin->setSuffix(tr(" dB"));
    extRxForm->addRow(tr("APF AF Level"), apfAfLevelSpin);

    QSpinBox *nrLevelSpin = extMakeSpin("nrLevelSpin", 0, 15, false);
    extRxForm->addRow(tr("NR Level"), nrLevelSpin);

    QSpinBox *nbLevelSpin = extMakeSpin("nbLevelSpin", 0, 100, false);
    nbLevelSpin->setSuffix(tr("%"));
    extRxForm->addRow(tr("NB Level"), nbLevelSpin);

    QSpinBox *nbDepthSpin = extMakeSpin("nbDepthSpin", 1, 10, false);
    nbDepthSpin->setToolTip(tr("CI-V 0..9 corresponds to radio NB Depth 1..10."));
    extRxForm->addRow(tr("NB Depth"), nbDepthSpin);

    QSpinBox *nbWidthSpin = extMakeSpin("nbWidthSpin", 1, 100, false);
    nbWidthSpin->setToolTip(tr("Displayed as the IC-7610 front-panel 1..100 scale; CI-V transport is 0..255."));
    extRxForm->addRow(tr("NB Width"), nbWidthSpin);

    QCheckBox *autoNotchChk = extMakeCheck("autoNotchChk");
    extRxForm->addRow(tr("Auto Notch"), autoNotchChk);

    QCheckBox *manualNotchChk = extMakeCheck("manualNotchChk");
    extRxForm->addRow(tr("Manual Notch"), manualNotchChk);

    // IC7610_V23_NOTCH_SCALE
    QSpinBox *notchPositionSpin = extMakeSpin("notchPositionSpin", -100, 100, false);
    notchPositionSpin->setSuffix(tr("%"));
    extRxForm->addRow(tr("Manual Notch Position"), notchPositionSpin);

    QComboBox *manualNotchWidthCombo = new QComboBox(ext7610Dialog);
    manualNotchWidthCombo->setObjectName(QStringLiteral("manualNotchWidthCombo"));
    manualNotchWidthCombo->setEnabled(false);
    manualNotchWidthCombo->addItem(tr("WIDE"), 0);
    manualNotchWidthCombo->addItem(tr("MID"), 1);
    manualNotchWidthCombo->addItem(tr("NAR"), 2);
    manualNotchWidthCombo->setCurrentIndex(-1);
    extRxForm->addRow(tr("Manual Notch Width"), manualNotchWidthCombo);

    QCheckBox *afMuteChk = extMakeCheck("afMuteChk");
    extRxForm->addRow(tr("AF Mute"), afMuteChk);

    // IC7610_V23_DIGISEL_SCALE
    QSpinBox *digiSelShiftSpin = extMakeSpin("digiSelShiftSpin", -100, 100, false);
    digiSelShiftSpin->setSuffix(tr("%"));
    extRxForm->addRow(tr("DIGI-SEL Shift"), digiSelShiftSpin);

    QCheckBox *twinPeakChk = extMakeCheck("twinPeakChk");
    twinPeakChk->setToolTip(tr("IC-7610 only allows Twin Peak in the valid RTTY Mark/Shift condition."));
    extRxForm->addRow(tr("Twin Peak Filter"), twinPeakChk);

    // ---------------- TX / Global tab ----------------
    QWidget *extTxPage = new QWidget();
    QFormLayout *extTxForm = new QFormLayout(extTxPage);
    extTxForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    QScrollArea *extTxScroll = new QScrollArea(ext7610Tabs);
    extTxScroll->setObjectName(QStringLiteral("extTxScroll"));
    extTxScroll->setWidgetResizable(true);
    extTxScroll->setFrameShape(QFrame::NoFrame);
    extTxScroll->setWidget(extTxPage);
    ext7610Tabs->addTab(extTxScroll, tr("TX / Global"));

    QCheckBox *dialLockChk = extMakeCheck("dialLockChk");
    extTxForm->addRow(tr("Dial Lock"), dialLockChk);

    QCheckBox *mainSubTrackingChk = extMakeCheck("mainSubTrackingChk");
    extTxForm->addRow(tr("MAIN/SUB Tracking"), mainSubTrackingChk);

    QSpinBox *compLevelSpin = extMakeSpin("compLevelSpin", 0, 10, false);
    extTxForm->addRow(tr("Compressor Level"), compLevelSpin);

    QSpinBox *driveGainSpin = extMakeSpin("driveGainSpin", 0, 100, false);
    driveGainSpin->setSuffix(tr("%"));
    extTxForm->addRow(tr("Drive Gain"), driveGainSpin);

    QSpinBox *voxGainSpin = extMakeSpin("voxGainSpin", 0, 100, false);
    voxGainSpin->setSuffix(tr("%"));
    extTxForm->addRow(tr("VOX Gain"), voxGainSpin);

    QSpinBox *antiVoxGainSpin = extMakeSpin("antiVoxGainSpin", 0, 100, false);
    antiVoxGainSpin->setSuffix(tr("%"));
    extTxForm->addRow(tr("Anti-VOX Gain"), antiVoxGainSpin);

    QSpinBox *voxDelaySpin = extMakeSpin("voxDelaySpin", 0, 20, false);
    voxDelaySpin->setSuffix(tr(" x0.1 s"));
    extTxForm->addRow(tr("VOX Delay"), voxDelaySpin);

    QComboBox *voxVoiceDelayCombo = new QComboBox(ext7610Dialog);
    voxVoiceDelayCombo->setObjectName(QStringLiteral("voxVoiceDelayCombo"));
    voxVoiceDelayCombo->setEnabled(false);
    voxVoiceDelayCombo->addItem(tr("OFF"), 0);
    voxVoiceDelayCombo->addItem(tr("SHORT"), 1);
    voxVoiceDelayCombo->addItem(tr("MID"), 2);
    voxVoiceDelayCombo->addItem(tr("LONG"), 3);
    voxVoiceDelayCombo->setCurrentIndex(-1);
    extTxForm->addRow(tr("VOX Voice Delay"), voxVoiceDelayCombo);

    QComboBox *breakInModeCombo = new QComboBox(ext7610Dialog);
    breakInModeCombo->setObjectName(QStringLiteral("breakInModeCombo"));
    breakInModeCombo->setEnabled(false);
    breakInModeCombo->addItem(tr("OFF"), 0);
    breakInModeCombo->addItem(tr("SEMI"), 1);
    breakInModeCombo->addItem(tr("FULL"), 2);
    breakInModeCombo->setCurrentIndex(-1);
    extTxForm->addRow(tr("CW Break-In"), breakInModeCombo);

    QSpinBox *breakInDelaySpin = extMakeSpin("breakInDelaySpin", 20, 130, false);
    breakInDelaySpin->setSuffix(tr(" x0.1 d"));
    extTxForm->addRow(tr("Break-In Delay"), breakInDelaySpin);

    QComboBox *ssbTxBwCombo = new QComboBox(ext7610Dialog);
    ssbTxBwCombo->setObjectName(QStringLiteral("ssbTxBwCombo"));
    ssbTxBwCombo->setEnabled(false);
    ssbTxBwCombo->addItem(tr("WIDE"), 0);
    ssbTxBwCombo->addItem(tr("MID"), 1);
    ssbTxBwCombo->addItem(tr("NAR"), 2);
    ssbTxBwCombo->setCurrentIndex(-1);
    extTxForm->addRow(tr("SSB TX Bandwidth"), ssbTxBwCombo);

    QHBoxLayout *extButtonLayout = new QHBoxLayout();
    extButtonLayout->addStretch();

    // IC7610_V23_SET_TAB_BEGIN
    QWidget *extSetPage = new QWidget();
    QFormLayout *extSetForm = new QFormLayout(extSetPage);
    extSetForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    extSetForm->setVerticalSpacing(8);

    QScrollArea *extSetScroll = new QScrollArea(ext7610Tabs);
    extSetScroll->setObjectName(QStringLiteral("extSetScroll"));
    extSetScroll->setWidgetResizable(true);
    extSetScroll->setFrameShape(QFrame::NoFrame);
    extSetScroll->setWidget(extSetPage);
    ext7610Tabs->addTab(extSetScroll, tr("7610 SET"));

    QLabel *setNotice = new QLabel(
        tr("Audited CI-V read/write SET controls. Each control stays disabled "
           "until its first reply is received from the radio."),
        extSetPage);
    setNotice->setWordWrap(true);
    extSetForm->addRow(setNotice);
    // IC7610_V27_CATEGORY_TABS
    QTabWidget *setCategoryTabs = new QTabWidget(extSetPage);
    setCategoryTabs->setObjectName(QStringLiteral("setCategoryTabs"));

    QWidget *setFunctionPage = new QWidget();
    QFormLayout *setFunctionForm = new QFormLayout(setFunctionPage);
    setFunctionForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    setFunctionForm->setVerticalSpacing(8);
    QScrollArea *setFunctionScroll = new QScrollArea(setCategoryTabs);
    setFunctionScroll->setWidgetResizable(true);
    setFunctionScroll->setFrameShape(QFrame::NoFrame);
    setFunctionScroll->setWidget(setFunctionPage);
    setCategoryTabs->addTab(setFunctionScroll, tr("Function"));

    QWidget *setAudioPage = new QWidget();
    QFormLayout *setAudioForm = new QFormLayout(setAudioPage);
    setAudioForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    setAudioForm->setVerticalSpacing(8);
    QScrollArea *setAudioScroll = new QScrollArea(setCategoryTabs);
    setAudioScroll->setWidgetResizable(true);
    setAudioScroll->setFrameShape(QFrame::NoFrame);
    setAudioScroll->setWidget(setAudioPage);
    setCategoryTabs->addTab(setAudioScroll, tr("Audio / I-O"));

    QWidget *setTonePage = new QWidget();
    QFormLayout *setToneForm = new QFormLayout(setTonePage);
    setToneForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    setToneForm->setVerticalSpacing(8);
    QScrollArea *setToneScroll = new QScrollArea(setCategoryTabs);
    setToneScroll->setWidgetResizable(true);
    setToneScroll->setFrameShape(QFrame::NoFrame);
    setToneScroll->setWidget(setTonePage);
    setCategoryTabs->addTab(setToneScroll, tr("Tone Control"));

    QWidget *setCwRttyPage = new QWidget();
    QFormLayout *setCwRttyForm = new QFormLayout(setCwRttyPage);
    setCwRttyForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    setCwRttyForm->setVerticalSpacing(8);
    QScrollArea *setCwRttyScroll = new QScrollArea(setCategoryTabs);
    setCwRttyScroll->setWidgetResizable(true);
    setCwRttyScroll->setFrameShape(QFrame::NoFrame);
    setCwRttyScroll->setWidget(setCwRttyPage);
    setCategoryTabs->addTab(setCwRttyScroll, tr("CW / RTTY"));

    QWidget *setCivPage = new QWidget();
    QFormLayout *setCivForm = new QFormLayout(setCivPage);
    setCivForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    setCivForm->setVerticalSpacing(8);
    QScrollArea *setCivScroll = new QScrollArea(setCategoryTabs);
    setCivScroll->setWidgetResizable(true);
    setCivScroll->setFrameShape(QFrame::NoFrame);
    setCivScroll->setWidget(setCivPage);
    setCategoryTabs->addTab(setCivScroll, tr("CI-V"));

    extSetForm->addRow(setCategoryTabs);

    QLabel *setFunctionHdr = new QLabel(tr("<b>Function / Behavior</b>"), extSetPage);
    setFunctionForm->addRow(setFunctionHdr);

    QCheckBox *setQuickDualChk = extMakeCheck("setQuickDualChk");
    setFunctionForm->addRow(tr("Quick Dual Watch"), setQuickDualChk);

    QCheckBox *setQuickSplitChk = extMakeCheck("setQuickSplitChk");
    setFunctionForm->addRow(tr("Quick Split"), setQuickSplitChk);

    QCheckBox *setCivTransceiveChk = extMakeCheck("setCivTransceiveChk");
    setCivForm->addRow(tr("CI-V Transceive"), setCivTransceiveChk);

    QCheckBox *setCivOutputChk = extMakeCheck("setCivOutputChk");
        // IC7610_V37_CIV_CONTROLS
    QLabel *setCivRadioHdr =
            new QLabel(tr("<b>Radio CI-V Settings</b>"), extSetPage);
    setCivForm->insertRow(0, setCivRadioHdr);

        // IC7610_V38_CIV_COMBO_HELPER
    auto setV38CivCombo = [=](const char *name) -> QComboBox* {
        QComboBox *w = new QComboBox(ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setEnabled(false);
        w->setCurrentIndex(-1);
        w->setMinimumHeight(qMax(30, w->sizeHint().height() + 8));
        w->setMinimumWidth(qMax(170, w->sizeHint().width() + 20));
        return w;
    };
QComboBox *setCivTimeoutCombo =
            setV38CivCombo("setCivTimeoutCombo");
    setCivTimeoutCombo->addItem(tr("OFF"), 0);
    setCivTimeoutCombo->addItem(tr("3 min"), 1);
    setCivTimeoutCombo->addItem(tr("5 min"), 2);
    setCivTimeoutCombo->addItem(tr("10 min"), 3);
    setCivTimeoutCombo->addItem(tr("20 min"), 4);
    setCivTimeoutCombo->addItem(tr("30 min"), 5);
    setCivForm->addRow(
            tr("CI-V Time-Out Timer"), setCivTimeoutCombo);

    QComboBox *setCivRemoteAddrCombo =
            setV38CivCombo("setCivRemoteAddrCombo");
    for (int a = 0; a <= 0xDF; ++a) {
        setCivRemoteAddrCombo->addItem(
            QStringLiteral("0x%1")
                .arg(a, 2, 16, QChar('0')).toUpper(),
            a);
    }
    setCivForm->addRow(
            tr("USB/LAN -> REMOTE Address"), setCivRemoteAddrCombo);
setCivForm->addRow(tr("CI-V Output (ANT)"), setCivOutputChk);
    QComboBox *setCivUsbPortCombo =
            setV38CivCombo("setCivUsbPortCombo");
    setCivUsbPortCombo->addItem(tr("Link to [REMOTE]"), 0);
    setCivUsbPortCombo->addItem(tr("Unlink from [REMOTE]"), 1);
    setCivForm->addRow(
            tr("CI-V USB Port"), setCivUsbPortCombo);

    QCheckBox *setCivUsbEchoChk =
            extMakeCheck("setCivUsbEchoChk");
    setCivForm->addRow(
            tr("CI-V USB Echo Back"), setCivUsbEchoChk);

    QLabel *setCivConnectionHdr =
            new QLabel(tr("<b>wfview Connection</b>"), extSetPage);
    setCivForm->addRow(setCivConnectionHdr);

    QLabel *setCivProtocolValue =
            new QLabel(tr("Icom CI-V"), ext7610Dialog);
    setCivProtocolValue->setObjectName(
            QStringLiteral("setCivProtocolValue"));
    setCivForm->addRow(
            tr("Protocol"), setCivProtocolValue);

    QLabel *setCivTransportValue =
            new QLabel(
                prefs.enableLAN
                    ? tr("LAN / Network")
                    : tr("Serial / USB"),
                ext7610Dialog);
    setCivTransportValue->setObjectName(
            QStringLiteral("setCivTransportValue"));
    setCivForm->addRow(
            tr("wfview Transport"), setCivTransportValue);

    QLabel *setCivPortValue =
            new QLabel(
                prefs.enableLAN
                    ? tr("Native LAN")
                    : prefs.serialPortRadio,
                ext7610Dialog);
    setCivPortValue->setObjectName(
            QStringLiteral("setCivPortValue"));
    setCivForm->addRow(
            tr("Port"), setCivPortValue);

    QLabel *setCivBaudValue =
            new QLabel(
                prefs.serialPortBaud
                    ? QStringLiteral("%1 baud")
                        .arg(prefs.serialPortBaud)
                    : tr("Auto / unknown"),
                ext7610Dialog);
    setCivBaudValue->setObjectName(
            QStringLiteral("setCivBaudValue"));
    setCivForm->addRow(
            tr("Current Baud"), setCivBaudValue);

    QLabel *setCivLocalAddressValue =
            new QLabel(ext7610Dialog);
    setCivLocalAddressValue->setObjectName(
            QStringLiteral("setCivLocalAddressValue"));
    if (prefs.radioCIVAddr == 0) {
        setCivLocalAddressValue->setText(tr("Auto"));
    }
    else {
        setCivLocalAddressValue->setText(
            QStringLiteral("0x%1")
                .arg(prefs.radioCIVAddr, 2, 16, QChar('0'))
                .toUpper());
    }
    setCivForm->addRow(
            tr("wfview Radio Address"), setCivLocalAddressValue);

    QLabel *setCivMenuNote =
            new QLabel(
                tr("Radio CI-V Baud Rate and primary CI-V Address are "
                   "radio SET-menu items; the IC-7610 does not expose them "
                   "as normal R/W CI-V settings."),
                ext7610Dialog);
    setCivMenuNote->setWordWrap(true);
    setCivMenuNote->setStyleSheet(
            QStringLiteral("QLabel { color: palette(mid); }"));
    setCivForm->addRow(setCivMenuNote);

    QPushButton *setCivOpenSettingsBtn =
            new QPushButton(tr("Open Connection Settings..."), ext7610Dialog);
    setCivOpenSettingsBtn->setMinimumHeight(
            qMax(30, setCivOpenSettingsBtn->sizeHint().height() + 6));
    setCivOpenSettingsBtn->setMaximumWidth(260);
    setCivForm->addRow(setCivOpenSettingsBtn);

    connect(
        setCivOpenSettingsBtn,
        &QPushButton::clicked,
        this,
        [=]() {
            showAndRaiseWidget(setupui);
        });


    QSpinBox *setDashRatioSpin = extMakeSpin("setDashRatioSpin", 28, 45, false);
    setDashRatioSpin->setSuffix(tr(" x0.1"));
    setDashRatioSpin->setToolTip(tr("28 = 2.8, 45 = 4.5"));
            // IC7610_V41_CIV_RUNTIME_GROUPING
    // Re-home existing CI-V rows by widget pointer instead of source-text
    // replacement. This is intentionally insensitive to formatting changes.
    QGroupBox *setCivRadioConfigGroup =
            new QGroupBox(tr("Radio CI-V Configuration"), extSetPage);
    QFormLayout *setCivRadioConfigForm =
            new QFormLayout(setCivRadioConfigGroup);
    setCivRadioConfigForm->setFieldGrowthPolicy(
            QFormLayout::ExpandingFieldsGrow);

    QGroupBox *setCivConnectionGroup =
            new QGroupBox(tr("wfview Connection"), extSetPage);
    QFormLayout *setCivConnectionForm =
            new QFormLayout(setCivConnectionGroup);
    setCivConnectionForm->setFieldGrowthPolicy(
            QFormLayout::ExpandingFieldsGrow);

    auto moveCivRow =
            [=](QWidget *field, QFormLayout *destination) {
        if (!field || !destination)
            return;

        int row = -1;
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        setCivForm->getWidgetPosition(field, &row, &role);

        if (row < 0)
            return;

        QFormLayout::TakeRowResult taken =
                setCivForm->takeRow(row);

        QWidget *labelWidget =
                taken.labelItem
                    ? taken.labelItem->widget()
                    : nullptr;

        QWidget *fieldWidget =
                taken.fieldItem
                    ? taken.fieldItem->widget()
                    : nullptr;

        if (labelWidget && fieldWidget)
            destination->addRow(labelWidget, fieldWidget);
        else if (fieldWidget)
            destination->addRow(fieldWidget);
        else if (labelWidget)
            destination->addRow(labelWidget);

        delete taken.labelItem;
        delete taken.fieldItem;
    };

    // Old loose section headers are replaced by the group-box titles.
    if (setCivRadioHdr)
        setCivForm->removeRow(setCivRadioHdr);
    if (setCivConnectionHdr)
        setCivForm->removeRow(setCivConnectionHdr);

    QLabel *setCivRadioBaudInfo =
            new QLabel(
                tr("Front-panel SET only: Auto / 4800 / 9600 / 19200 / "
                   "38400 / 57600 / 115200"),
                ext7610Dialog);
    // IC7610_V46_CIV_RUNTIME_OBJECTS
    setCivRadioBaudInfo->setObjectName(
            QStringLiteral("setCivRadioBaudInfo"));    setCivRadioBaudInfo->setWordWrap(true);
    setCivRadioConfigForm->addRow(
            tr("CI-V Baud Rate"), setCivRadioBaudInfo);

    QString setCivAddressText =
            tr("Front-panel SET only");

    if (prefs.radioCIVAddr != 0) {
        setCivAddressText +=
                tr(" — wfview target is 0x%1")
                    .arg(prefs.radioCIVAddr, 2, 16, QChar('0'))
                    .toUpper();
    }
    else {
        setCivAddressText +=
                tr(" — IC-7610 default is 0x98");
    }

    QLabel *setCivRadioAddressInfo =
            new QLabel(setCivAddressText, ext7610Dialog);
    setCivRadioAddressInfo->setObjectName(
            QStringLiteral("setCivRadioAddressInfo"));    setCivRadioAddressInfo->setWordWrap(true);
    setCivRadioConfigForm->addRow(
            tr("CI-V Address"), setCivRadioAddressInfo);

    QLabel *setCivUsbBaudInfo =
            new QLabel(
                tr("Front-panel SET only: Auto / 4800 / 9600 / 19200 / "
                   "38400 / 57600 / 115200; applies when CI-V USB Port "
                   "is Unlink from [REMOTE]"),
                ext7610Dialog);
    setCivUsbBaudInfo->setObjectName(
            QStringLiteral("setCivUsbBaudInfo"));    setCivUsbBaudInfo->setWordWrap(true);
    setCivRadioConfigForm->addRow(
            tr("CI-V USB Baud Rate"), setCivUsbBaudInfo);

    // Live radio R/W CI-V settings.
    moveCivRow(setCivTransceiveChk, setCivRadioConfigForm);
    moveCivRow(setCivTimeoutCombo, setCivRadioConfigForm);
    moveCivRow(setCivRemoteAddrCombo, setCivRadioConfigForm);
    moveCivRow(setCivOutputChk, setCivRadioConfigForm);
    moveCivRow(setCivUsbPortCombo, setCivRadioConfigForm);
    moveCivRow(setCivUsbEchoChk, setCivRadioConfigForm);

    // wfview-side connection information / entry point.
    moveCivRow(setCivProtocolValue, setCivConnectionForm);
    moveCivRow(setCivTransportValue, setCivConnectionForm);
    moveCivRow(setCivPortValue, setCivConnectionForm);
    moveCivRow(setCivBaudValue, setCivConnectionForm);
    moveCivRow(setCivLocalAddressValue, setCivConnectionForm);
    moveCivRow(setCivMenuNote, setCivConnectionForm);
    // IC7610_V48_CIV_DEDUP
    // setCivConnectionExplanation (below) says the same thing in full.
    setCivConnectionForm->setRowVisible(setCivMenuNote, false);
    moveCivRow(setCivOpenSettingsBtn, setCivConnectionForm);

    auto renameCivLabel =
            [=](QFormLayout *form,
                QWidget *field,
                const QString &text) {
        int row = -1;
        QFormLayout::ItemRole role = QFormLayout::FieldRole;
        form->getWidgetPosition(field, &row, &role);

        if (row < 0)
            return;

        QLayoutItem *item =
                form->itemAt(row, QFormLayout::LabelRole);

        QLabel *label =
                item
                    ? qobject_cast<QLabel *>(item->widget())
                    : nullptr;

        if (label)
            label->setText(text);
    };

    renameCivLabel(
            setCivConnectionForm,
            setCivTransportValue,
            tr("Transport"));

    renameCivLabel(
            setCivConnectionForm,
            setCivPortValue,
            tr("COM / Port"));

    renameCivLabel(
            setCivConnectionForm,
            setCivBaudValue,
            tr("wfview COM Baud"));

    if (prefs.enableLAN)
        setCivBaudValue->setText(tr("N/A (LAN transport)"));

    QLabel *setCivConnectionExplanation =
            new QLabel(
                tr("Radio CI-V Baud Rate, CI-V Address, and CI-V USB Baud "
                   "Rate are radio front-panel SET items, not normal remote "
                   "R/W CI-V commands. COM / Port and wfview COM Baud are "
                   "the PC-side connection settings."),
                ext7610Dialog);
    setCivConnectionExplanation->setWordWrap(true);
    setCivConnectionExplanation->setStyleSheet(
            QStringLiteral("QLabel { color: palette(mid); }"));
    setCivConnectionForm->addRow(setCivConnectionExplanation);

    // IC7610_V48_CIV_TOOLTIPS
    {
        const QString v48FrontPanelTip =
                tr("This is an IC-7610 front-panel SET menu item. "
                   "The radio does not report it over CI-V, so wfview "
                   "cannot read it. Over LAN there is no serial baud at all; "
                   "over USB/serial wfview shows the effective link rate.");
        setCivRadioBaudInfo->setToolTip(v48FrontPanelTip);
        setCivUsbBaudInfo->setToolTip(v48FrontPanelTip);
        setCivRadioAddressInfo->setToolTip(
                tr("The radio's CI-V Address menu value cannot be read "
                   "remotely. Shown is wfview's own target address "
                   "(Settings > Radio Connection)."));
    }

    // At this point the old loose rows have been taken out of setCivForm.
    setCivForm->addRow(setCivRadioConfigGroup);
    setCivForm->addRow(setCivConnectionGroup);
    // IC7610_V43_FINAL2_CIV_INFO
    // Show the values wfview can actually observe or infer without
    // pretending the literal front-panel-only menu item is remotely R/W.
    if (prefs.enableLAN) {
        setCivRadioBaudInfo->setText(
                tr("Not readable over LAN"));

        setCivUsbBaudInfo->setText(
                tr("Not readable over LAN"));

        setCivBaudValue->setText(
                tr("N/A (LAN transport)"));
    }
    else {
        if (prefs.serialPortBaud > 0) {
            setCivRadioBaudInfo->setText(
                tr("Effective link: %1 baud; radio SET may literally "
                   "be AUTO or this fixed rate")
                    .arg(prefs.serialPortBaud));

            setCivUsbBaudInfo->setText(
                tr("Effective serial link: %1 baud; literal USB Baud "
                   "vs [REMOTE]-linked rate is not separately observable")
                    .arg(prefs.serialPortBaud));

            setCivBaudValue->setText(
                tr("%1 baud")
                    .arg(prefs.serialPortBaud));
        }
        else {
            setCivRadioBaudInfo->setText(
                tr("Effective serial baud is not known"));

            setCivUsbBaudInfo->setText(
                tr("Effective serial baud is not known"));
        }
    }

    if (prefs.radioCIVAddr != 0) {
        setCivRadioAddressInfo->setText(
                tr("wfview target 0x%1")
                    .arg(
                        prefs.radioCIVAddr & 0xff,
                        2,
                        16,
                        QChar('0'))
                    .toUpper());
    }
    else {
        setCivRadioAddressInfo->setText(
                tr("Auto (wfview detects radio)"));
    }
    // IC7610_V45_CIV_LIVE_BAUD
    // Prefer the baud actually reported by the connected rig object.
    // This is the effective live CI-V serial link rate.  It does not claim
    // that the IC-7610 front-panel CI-V Baud menu is literally fixed rather
    // than AUTO.
    auto v45ValidCivBaud =
            [](quint32 baud) -> bool {
        switch (baud) {
        case 4800:
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
            return true;
        default:
            return false;
        }
    };

    quint32 v45CivBaud = 0;

    if (rigCaps != Q_NULLPTR &&
        v45ValidCivBaud(rigCaps->baudRate)) {
        v45CivBaud = rigCaps->baudRate;
    }

    if (v45CivBaud == 0 &&
        v45ValidCivBaud(prefs.serialPortBaud)) {
        v45CivBaud = prefs.serialPortBaud;
    }

    if (v45CivBaud == 0 &&
        v45ValidCivBaud(serverConfig.baudRate)) {
        v45CivBaud = serverConfig.baudRate;
    }

    if (prefs.enableLAN) {
        setCivRadioBaudInfo->setText(
            tr("Not readable over LAN"));

        setCivUsbBaudInfo->setText(
            tr("Not readable over LAN"));

        setCivBaudValue->setText(
            tr("N/A (LAN transport)"));
    }
    else if (v45CivBaud != 0) {
        setCivRadioBaudInfo->setText(
            tr("%1 baud - live/effective serial CI-V link; "
               "front-panel CI-V Baud may be AUTO or fixed")
                .arg(v45CivBaud));

        setCivUsbBaudInfo->setText(
            tr("%1 baud - live/effective serial CI-V link; "
               "literal USB Baud vs [REMOTE]-linked selection "
               "is not separately observable")
                .arg(v45CivBaud));

        setCivBaudValue->setText(
            tr("%1 baud")
                .arg(v45CivBaud));
    }
    else {
        setCivRadioBaudInfo->setText(
            tr("Effective serial CI-V baud is not available"));

        setCivUsbBaudInfo->setText(
            tr("Effective serial CI-V baud is not available"));

        setCivBaudValue->setText(
            tr("Unknown"));
    }
// IC7610_V37_CW_RTTY_CONTROLS
    auto setV37Combo = [=](const char *name) -> QComboBox* {
        QComboBox *w = new QComboBox(ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setEnabled(false);
        w->setCurrentIndex(-1);
        w->setMinimumHeight(qMax(30, w->sizeHint().height() + 8));
        w->setMinimumWidth(qMax(170, w->sizeHint().width() + 20));
        return w;
    };

        // IC7610_V39_CW_RTTY_CONFIG_GROUPS
    QGroupBox *setRttyConfigGroup =
            new QGroupBox(tr("RTTY Configuration"), extSetPage);
    QFormLayout *setRttyConfigForm =
            new QFormLayout(setRttyConfigGroup);
    setRttyConfigForm->setFieldGrowthPolicy(
            QFormLayout::ExpandingFieldsGrow);
    setCwRttyForm->addRow(setRttyConfigGroup);

    QGroupBox *setRttyDecodeGroup =
            new QGroupBox(tr("RTTY Decoder / Logging"), extSetPage);
    QFormLayout *setRttyDecodeForm =
            new QFormLayout(setRttyDecodeGroup);
    setRttyDecodeForm->setFieldGrowthPolicy(
            QFormLayout::ExpandingFieldsGrow);
    setCwRttyForm->addRow(setRttyDecodeGroup);

    QGroupBox *setCwConfigGroup =
            new QGroupBox(tr("CW Configuration"), extSetPage);
    QFormLayout *setCwConfigForm =
            new QFormLayout(setCwConfigGroup);
    setCwConfigForm->setFieldGrowthPolicy(
            QFormLayout::ExpandingFieldsGrow);
    setCwRttyForm->addRow(setCwConfigGroup);
QLabel *setRttyHdr =
            new QLabel(tr("<b>RTTY</b>"), extSetPage);
    setRttyHdr->hide(); // IC7610_V39_GROUPED_RTTY

    QComboBox *setRttyMarkCombo =
            setV37Combo("setRttyMarkCombo");
    setRttyMarkCombo->addItem(tr("1275 Hz"), 0);
    setRttyMarkCombo->addItem(tr("1615 Hz"), 1);
    setRttyMarkCombo->addItem(tr("2125 Hz"), 2);
    setCwRttyForm->addRow(
            tr("RTTY Mark Frequency"), setRttyMarkCombo);

    QComboBox *setRttyShiftCombo =
            setV37Combo("setRttyShiftCombo");
    setRttyShiftCombo->addItem(tr("170 Hz"), 0);
    setRttyShiftCombo->addItem(tr("200 Hz"), 1);
    setRttyShiftCombo->addItem(tr("425 Hz"), 2);
    setCwRttyForm->addRow(
            tr("RTTY Shift Width"), setRttyShiftCombo);

    QComboBox *setRttyPolarityCombo =
            setV37Combo("setRttyPolarityCombo");
    setRttyPolarityCombo->addItem(tr("Normal"), 0);
    setRttyPolarityCombo->addItem(tr("Reverse"), 1);
    setCwRttyForm->addRow(
            tr("RTTY Keying Polarity"), setRttyPolarityCombo);

        // IC7610_V39_RTTY_CONTROLS
    QCheckBox *setExternalKeypadRttyChk =
            extMakeCheck("setExternalKeypadRttyChk");
    setRttyConfigForm->addRow(
            tr("External Keypad RTTY"), setExternalKeypadRttyChk);

    QComboBox *setDecodeBaudCombo =
            setV37Combo("setDecodeBaudCombo");
    setDecodeBaudCombo->addItem(tr("4800 bps"), 0);
    setDecodeBaudCombo->addItem(tr("9600 bps"), 1);
    setDecodeBaudCombo->addItem(tr("19200 bps"), 2);
    setDecodeBaudCombo->addItem(tr("38400 bps"), 3);
    setRttyConfigForm->addRow(
            tr("Decoded Data Baud Rate"), setDecodeBaudCombo);

    QComboBox *setRttyFftAvgCombo =
            setV37Combo("setRttyFftAvgCombo");
    setRttyFftAvgCombo->addItem(tr("OFF"), 0);
    setRttyFftAvgCombo->addItem(tr("2"), 1);
    setRttyFftAvgCombo->addItem(tr("3"), 2);
    setRttyFftAvgCombo->addItem(tr("4"), 3);
    setRttyDecodeForm->addRow(
            tr("FFT Scope Averaging"), setRttyFftAvgCombo);

    QCheckBox *setRttyDecodeUsosChk =
            extMakeCheck("setRttyDecodeUsosChk");
    setRttyDecodeForm->addRow(
            tr("Decode USOS"), setRttyDecodeUsosChk);

    QComboBox *setRttyNewLineCombo =
            setV37Combo("setRttyNewLineCombo");
    setRttyNewLineCombo->addItem(tr("CR, LF, CR+LF"), 0);
    setRttyNewLineCombo->addItem(tr("CR+LF"), 1);
    setRttyDecodeForm->addRow(
            tr("Decode New Line Code"), setRttyNewLineCombo);

    QComboBox *setRttyDiddleCombo =
            setV37Combo("setRttyDiddleCombo");
    setRttyDiddleCombo->addItem(tr("OFF"), 0);
    setRttyDiddleCombo->addItem(tr("BLANK"), 1);
    setRttyDiddleCombo->addItem(tr("LTRS"), 2);
    setRttyDecodeForm->addRow(
            tr("Diddle"), setRttyDiddleCombo);

    QCheckBox *setRttyTxUsosChk =
            extMakeCheck("setRttyTxUsosChk");
    setRttyDecodeForm->addRow(
            tr("TX USOS"), setRttyTxUsosChk);

    QCheckBox *setRttyAutoCrlfChk =
            extMakeCheck("setRttyAutoCrlfChk");
    setRttyDecodeForm->addRow(
            tr("Auto CR+LF by TX"), setRttyAutoCrlfChk);

    QCheckBox *setRttyTimestampChk =
            extMakeCheck("setRttyTimestampChk");
    setRttyDecodeForm->addRow(
            tr("Time Stamp"), setRttyTimestampChk);

    QComboBox *setRttyTimestampTimeCombo =
            setV37Combo("setRttyTimestampTimeCombo");
    setRttyTimestampTimeCombo->addItem(tr("Local"), 0);
    setRttyTimestampTimeCombo->addItem(
            tr("UTC (CLOCK2 UTC Offset)"), 1);
    setRttyDecodeForm->addRow(
            tr("Time Stamp Time"), setRttyTimestampTimeCombo);

    QCheckBox *setRttyTimestampFreqChk =
            extMakeCheck("setRttyTimestampFreqChk");
    setRttyDecodeForm->addRow(
            tr("Time Stamp Frequency"), setRttyTimestampFreqChk);

    QCheckBox *setRttyDecodeLogChk =
            extMakeCheck("setRttyDecodeLogChk");
    setRttyDecodeForm->addRow(
            tr("Decode Log"), setRttyDecodeLogChk);

    QComboBox *setRttyLogFileTypeCombo =
            setV37Combo("setRttyLogFileTypeCombo");
    setRttyLogFileTypeCombo->addItem(tr("Text"), 0);
    setRttyLogFileTypeCombo->addItem(tr("HTML"), 1);
    setRttyDecodeForm->addRow(
            tr("Log File Type"), setRttyLogFileTypeCombo);

    QLabel *setRttyColorNote =
            new QLabel(
                tr("RTTY waveform/font colors use multivalue RGB CI-V data. "
                   "They will get proper color-picker controls rather than "
                   "raw numeric fields."),
                ext7610Dialog);
    setRttyColorNote->setWordWrap(true);
    setRttyColorNote->setStyleSheet(
            QStringLiteral("QLabel { color: palette(mid); }"));
    setRttyDecodeForm->addRow(setRttyColorNote);
QLabel *setCwHdr =
            new QLabel(tr("<b>CW</b>"), extSetPage);
    setCwHdr->hide(); // IC7610_V39_GROUPED_CW

    QCheckBox *setCwSyncChk =
            extMakeCheck("setCwSyncChk");
    setCwRttyForm->addRow(
            tr("SSB/CW Synchronous Tuning"), setCwSyncChk);

    QComboBox *setCwNormalSideCombo =
            setV37Combo("setCwNormalSideCombo");
    setCwNormalSideCombo->addItem(tr("LSB"), 0);
    setCwNormalSideCombo->addItem(tr("USB"), 1);
    setCwRttyForm->addRow(
            tr("CW Normal Side"), setCwNormalSideCombo);

    QSpinBox *setSideToneLevelSpin =
            extMakeSpin("setSideToneLevelSpin", 0, 100, false);
    setSideToneLevelSpin->setSuffix(tr("%"));
    setCwRttyForm->addRow(
            tr("Side Tone Level"), setSideToneLevelSpin);

    QCheckBox *setSideToneLimitChk =
            extMakeCheck("setSideToneLimitChk");
    setCwRttyForm->addRow(
            tr("Side Tone Level Limit"), setSideToneLimitChk);

    QSpinBox *setKeyerRepeatSpin =
            extMakeSpin("setKeyerRepeatSpin", 1, 60, false);
    setKeyerRepeatSpin->setSuffix(tr(" sec"));
    setCwRttyForm->addRow(
            tr("Keyer Repeat Time"), setKeyerRepeatSpin);
setCwConfigForm->addRow(tr("CW Dash Ratio"), setDashRatioSpin);
    QComboBox *setCwRiseCombo =
            setV37Combo("setCwRiseCombo");
    setCwRiseCombo->addItem(tr("2 ms"), 0);
    setCwRiseCombo->addItem(tr("4 ms"), 1);
    setCwRiseCombo->addItem(tr("6 ms"), 2);
    setCwRiseCombo->addItem(tr("8 ms"), 3);
    setCwRttyForm->addRow(
            tr("CW Rise Time"), setCwRiseCombo);

    QComboBox *setPaddlePolarityCombo =
            setV37Combo("setPaddlePolarityCombo");
    setPaddlePolarityCombo->addItem(tr("Normal"), 0);
    setPaddlePolarityCombo->addItem(tr("Reverse"), 1);
    setCwRttyForm->addRow(
            tr("Paddle Polarity"), setPaddlePolarityCombo);

    QComboBox *setKeyTypeCombo =
            setV37Combo("setKeyTypeCombo");
    setKeyTypeCombo->addItem(tr("Straight"), 0);
    setKeyTypeCombo->addItem(tr("Bug"), 1);
    setKeyTypeCombo->addItem(tr("Paddle"), 2);
    setCwRttyForm->addRow(
            tr("Key Type"), setKeyTypeCombo);

    QCheckBox *setMicUpDownKeyerChk =
            extMakeCheck("setMicUpDownKeyerChk");
    setCwRttyForm->addRow(
            tr("MIC Up/Down Keyer"), setMicUpDownKeyerChk);

    QLabel *setUsbKeyingHdr =
            new QLabel(tr("<b>USB Keying</b>"), extSetPage);
    setUsbKeyingHdr->hide(); // IC7610_V39_GROUPED_USB_KEYING

    auto fillUsbKeying = [=](QComboBox *w) {
        w->addItem(tr("OFF"), 0);
        w->addItem(tr("USB1(A) DTR"), 1);
        w->addItem(tr("USB1(A) RTS"), 2);
        w->addItem(tr("USB1(B) DTR"), 3);
        w->addItem(tr("USB1(B) RTS"), 4);
    };

    QComboBox *setUsbKeyCwCombo =
            setV37Combo("setUsbKeyCwCombo");
    fillUsbKeying(setUsbKeyCwCombo);
    setCwRttyForm->addRow(
            tr("USB Keying (CW)"), setUsbKeyCwCombo);

    QComboBox *setUsbKeyRttyCombo =
            setV37Combo("setUsbKeyRttyCombo");
    fillUsbKeying(setUsbKeyRttyCombo);
    setCwRttyForm->addRow(
            tr("USB Keying (RTTY)"), setUsbKeyRttyCombo);


    QLabel *setModHdr = new QLabel(tr("<b>Modulation Input Levels</b>"), extSetPage);
    setAudioForm->addRow(setModHdr);

    QSpinBox *setAccModSpin = extMakeSpin("setAccModSpin", 0, 100, false);
    setAccModSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("ACC1 Mod Level"), setAccModSpin);

    QSpinBox *setUsbModSpin = extMakeSpin("setUsbModSpin", 0, 100, false);
    setUsbModSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("USB Mod Level"), setUsbModSpin);

    QSpinBox *setLanModSpin = extMakeSpin("setLanModSpin", 0, 100, false);
    setLanModSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("LAN Mod Level"), setLanModSpin);

    // IC7610_V27_OUTPUT_CONTROLS
    QLabel *setOutputHdr = new QLabel(tr("<b>AF / IF Outputs</b>"), extSetPage);
    setAudioForm->addRow(setOutputHdr);

    auto setMakeAfIf = [=](const char *name) -> QComboBox* {
        QComboBox *w = new QComboBox(ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setEnabled(false);
        w->addItem(tr("AF"), 0);
        w->addItem(tr("IF"), 1);
        w->setCurrentIndex(-1);
        return w;
    };

    QComboBox *setAccOutputSelect = setMakeAfIf("setAccOutputSelect");
    setAudioForm->addRow(tr("ACC Output Select"), setAccOutputSelect);

    QSpinBox *setAccAfOutputSpin = extMakeSpin("setAccAfOutputSpin", 0, 100, false);
    setAccAfOutputSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("ACC AF Output Level"), setAccAfOutputSpin);

    QSpinBox *setAccIfOutputSpin = extMakeSpin("setAccIfOutputSpin", 0, 100, false);
    setAccIfOutputSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("ACC IF Output Level"), setAccIfOutputSpin);

    QComboBox *setUsbOutputSelect = setMakeAfIf("setUsbOutputSelect");
    setAudioForm->addRow(tr("USB Output Select"), setUsbOutputSelect);

    QSpinBox *setUsbAfOutputSpin = extMakeSpin("setUsbAfOutputSpin", 0, 100, false);
    setUsbAfOutputSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("USB AF Output Level"), setUsbAfOutputSpin);

    QSpinBox *setUsbIfOutputSpin = extMakeSpin("setUsbIfOutputSpin", 0, 100, false);
    setUsbIfOutputSpin->setSuffix(tr("%"));
    setAudioForm->addRow(tr("USB IF Output Level"), setUsbIfOutputSpin);

    QComboBox *setLanOutputSelect = setMakeAfIf("setLanOutputSelect");
    setAudioForm->addRow(tr("LAN Output Select"), setLanOutputSelect);

    QLabel *setRefHdr = new QLabel(tr("<b>Reference</b>"), extSetPage);
    setFunctionForm->addRow(setRefHdr);

    QComboBox *setRefInputCombo = new QComboBox(ext7610Dialog);
    setRefInputCombo->setObjectName(QStringLiteral("setRefInputCombo"));
    setRefInputCombo->setEnabled(false);
    setRefInputCombo->addItem(tr("REF IN"), 0); // IC7610_V31_REF_LABEL
    setRefInputCombo->addItem(tr("Internal Reference"), 1);
    setRefInputCombo->setCurrentIndex(-1);
    setRefInputCombo->setToolTip(
        tr("IC-7610 REF IN: IN uses the external reference input; OFF uses the internal reference."));
    setFunctionForm->addRow(tr("Reference Source"), setRefInputCombo);

    QCheckBox *setShowVaMeterChk = extMakeCheck("setShowVaMeterChk");
    setShowVaMeterChk->setEnabled(true);
    setShowVaMeterChk->setChecked(true);
    setFunctionForm->addRow(tr("Show V/A 4th Meter Row"), setShowVaMeterChk);
    QLabel *setToneHdr = new QLabel(tr("<b>RX / TX Tone Control</b>"), extSetPage);
    setToneForm->addRow(setToneHdr);

    auto setMakeTone = [=](const char *name) -> QSpinBox* {
        QSpinBox *w = extMakeSpin(name, -5, 5, false);
        return w;
    };

    QSpinBox *setSsbRxBass = setMakeTone("setSsbRxBass");
    QSpinBox *setSsbRxTreble = setMakeTone("setSsbRxTreble");
    QSpinBox *setAmRxBass = setMakeTone("setAmRxBass");
    QSpinBox *setAmRxTreble = setMakeTone("setAmRxTreble");
    QSpinBox *setFmRxBass = setMakeTone("setFmRxBass");
    QSpinBox *setFmRxTreble = setMakeTone("setFmRxTreble");
    QSpinBox *setSsbTxBass = setMakeTone("setSsbTxBass");
    QSpinBox *setSsbTxTreble = setMakeTone("setSsbTxTreble");
    QSpinBox *setAmTxBass = setMakeTone("setAmTxBass");
    QSpinBox *setAmTxTreble = setMakeTone("setAmTxTreble");
    QSpinBox *setFmTxBass = setMakeTone("setFmTxBass");
    QSpinBox *setFmTxTreble = setMakeTone("setFmTxTreble");

    setToneForm->addRow(tr("SSB RX Bass"), setSsbRxBass);
    setToneForm->addRow(tr("SSB RX Treble"), setSsbRxTreble);
    setToneForm->addRow(tr("AM RX Bass"), setAmRxBass);
    setToneForm->addRow(tr("AM RX Treble"), setAmRxTreble);
    setToneForm->addRow(tr("FM RX Bass"), setFmRxBass);
    setToneForm->addRow(tr("FM RX Treble"), setFmRxTreble);
    setToneForm->addRow(tr("SSB TX Bass"), setSsbTxBass);
    setToneForm->addRow(tr("SSB TX Treble"), setSsbTxTreble);
    setToneForm->addRow(tr("AM TX Bass"), setAmTxBass);
    setToneForm->addRow(tr("AM TX Treble"), setAmTxTreble);
    setToneForm->addRow(tr("FM TX Bass"), setFmTxBass);
    setToneForm->addRow(tr("FM TX Treble"), setFmTxTreble);

    QLabel *setBeepHdr = new QLabel(tr("<b>Beep / Control</b>"), extSetPage);
    setFunctionForm->addRow(setBeepHdr);

    QSpinBox *setBeepLevelSpin = extMakeSpin("setBeepLevelSpin", 0, 100, false);
    setBeepLevelSpin->setSuffix(tr("%"));
    setFunctionForm->addRow(tr("Beep Level"), setBeepLevelSpin);

    QCheckBox *setBeepLimitChk = extMakeCheck("setBeepLimitChk");
    setFunctionForm->addRow(tr("Beep Level Limit"), setBeepLimitChk);

    QCheckBox *setBeepConfirmChk = extMakeCheck("setBeepConfirmChk");
    setFunctionForm->addRow(tr("Beep Confirmation"), setBeepConfirmChk);

    QComboBox *setBandEdgeBeepCombo = new QComboBox(ext7610Dialog);
    setBandEdgeBeepCombo->setObjectName(QStringLiteral("setBandEdgeBeepCombo"));
    setBandEdgeBeepCombo->setEnabled(false);
    setBandEdgeBeepCombo->addItem(tr("OFF"), 0);
    setBandEdgeBeepCombo->addItem(tr("ON (Default)"), 1);
    setBandEdgeBeepCombo->addItem(tr("ON (User)"), 2);
    setBandEdgeBeepCombo->addItem(tr("ON (User) + TX Limit"), 3);
    setBandEdgeBeepCombo->setCurrentIndex(-1);
    setFunctionForm->addRow(tr("Band Edge Beep"), setBandEdgeBeepCombo);

    QComboBox *setRfSqlCombo = new QComboBox(ext7610Dialog);
    setRfSqlCombo->setObjectName(QStringLiteral("setRfSqlCombo"));
    setRfSqlCombo->setEnabled(false);
    setRfSqlCombo->addItem(tr("AUTO"), 0);
    setRfSqlCombo->addItem(tr("SQL"), 1);
    setRfSqlCombo->addItem(tr("RF + SQL"), 2);
    setRfSqlCombo->setCurrentIndex(-1);
    setFunctionForm->addRow(tr("RF/SQL Control"), setRfSqlCombo);

    auto setMakeTxDelay = [=](const char *name) -> QComboBox* {
        QComboBox *w = new QComboBox(ext7610Dialog);
        w->setObjectName(QString::fromLatin1(name));
        w->setEnabled(false);
        w->addItem(tr("OFF"), 0);
        w->addItem(tr("10 ms"), 1);
        w->addItem(tr("15 ms"), 2);
        w->addItem(tr("20 ms"), 3);
        w->addItem(tr("25 ms"), 4);
        w->addItem(tr("30 ms"), 5);
        w->setCurrentIndex(-1);
        return w;
    };

    QComboBox *setTxDelayHfCombo = setMakeTxDelay("setTxDelayHfCombo");
    QComboBox *setTxDelay50Combo = setMakeTxDelay("setTxDelay50Combo");
    setFunctionForm->addRow(tr("TX Delay HF"), setTxDelayHfCombo);
    setFunctionForm->addRow(tr("TX Delay 50 MHz"), setTxDelay50Combo);

    // Fix the DPI/font clipping seen on the test PC. Qt scales these logical
    // pixels for device DPI; sizeHint keeps native style metrics respected.
    extRxForm->setVerticalSpacing(8);
    extTxForm->setVerticalSpacing(8);

    const QList<QSpinBox*> extSpins =
            ext7610Dialog->findChildren<QSpinBox*>();
    for (QSpinBox *w : extSpins) {
        w->setMinimumHeight(qMax(30, w->sizeHint().height() + 8));
        w->setMinimumWidth(qMax(170, w->sizeHint().width() + 20));
    }

    const QList<QComboBox*> extCombos =
            ext7610Dialog->findChildren<QComboBox*>();
    for (QComboBox *w : extCombos) {
        w->setMinimumHeight(qMax(30, w->sizeHint().height() + 8));
        w->setMinimumWidth(qMax(170, w->sizeHint().width() + 20));
    }
    // IC7610_V23_SET_TAB_END
    QPushButton *ext7610RefreshBtn = new QPushButton(tr("Refresh from Radio"), ext7610Dialog);
    ext7610RefreshBtn->setObjectName(QStringLiteral("ext7610RefreshBtn"));
    extButtonLayout->addWidget(ext7610RefreshBtn);

    QPushButton *ext7610CloseBtn = new QPushButton(tr("Close"), ext7610Dialog);
    ext7610CloseBtn->setObjectName(QStringLiteral("ext7610CloseBtn"));
    extButtonLayout->addWidget(ext7610CloseBtn);

    extRootLayout->addLayout(extButtonLayout);
    // IC7610_EXT_DIALOG_END

    connect(
                ui->txPowerSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->txPowerSlider->maximum())
                        statusFromSliderPercent("Tx Power", 255*newValue/ui->txPowerSlider->maximum());
                }
    );

    connect(
                ui->rfGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->rfGainSlider->maximum())
                        statusFromSliderPercent("RF Gain", 255*newValue/ui->rfGainSlider->maximum());
                }
    );

    connect(
                ui->afGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->afGainSlider->maximum())
                        statusFromSliderPercent("AF Gain", 255*newValue/ui->afGainSlider->maximum());
                }
    );

    connect(
                ui->micGainSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->micGainSlider->maximum())
                        statusFromSliderPercent("TX Audio Gain", 255*newValue/ui->micGainSlider->maximum());
                }
    );

    connect(
                ui->sqlSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->sqlSlider->maximum())
                        statusFromSliderPercent("Squelch", 255*newValue/ui->sqlSlider->maximum());
                }
    );

    connect(
                ui->monitorSlider, &QSlider::valueChanged, this,
                [=](const int &newValue) {
                    if (ui->monitorSlider->maximum())
                        statusFromSliderPercent("Monitor", 255*newValue/ui->monitorSlider->maximum());
                }
    );

}

void wfmain::connectSettingsWidget()
{
    connect(setupui, SIGNAL(changedClusterPref(prefClusterItem)), this, SLOT(extChangedClusterPref(prefClusterItem)));
    connect(setupui, SIGNAL(changedClusterPrefs(quint64)), this, SLOT(extChangedClusterPrefs(quint64)));

    connect(setupui, SIGNAL(changedCtPref(prefCtItem)), this, SLOT(extChangedCtPref(prefCtItem)));
    connect(setupui, SIGNAL(changedCtPrefs(quint64)), this, SLOT(extChangedCtPrefs(quint64)));

    connect(setupui, SIGNAL(changedIfPref(prefIfItem)), this, SLOT(extChangedIfPref(prefIfItem)));
    connect(setupui, SIGNAL(changedIfPrefs(quint64)), this, SLOT(extChangedIfPrefs(quint64)));

    connect(setupui, SIGNAL(changedColPref(prefColItem)), this, SLOT(extChangedColPref(prefColItem)));
    connect(setupui, SIGNAL(changedColPrefs(quint64)), this, SLOT(extChangedColPrefs(quint64)));

    connect(setupui, SIGNAL(changedLanPref(prefLanItem)), this, SLOT(extChangedLanPref(prefLanItem)));
    connect(setupui, SIGNAL(changedLanPrefs(quint64)), this, SLOT(extChangedLanPrefs(quint64)));

    connect(setupui, SIGNAL(changedRaPref(prefRaItem)), this, SLOT(extChangedRaPref(prefRaItem)));
    connect(setupui, SIGNAL(changedRaPrefs(quint64)), this, SLOT(extChangedRaPrefs(quint64)));

    connect(setupui, SIGNAL(changedRsPref(prefRsItem)), this, SLOT(extChangedRsPref(prefRsItem)));
    connect(setupui, SIGNAL(changedRsPrefs(quint64)), this, SLOT(extChangedRsPrefs(quint64)));

    connect(setupui, SIGNAL(changedUdpPref(prefUDPItem)), this, SLOT(extChangedUdpPref(prefUDPItem)));
    connect(setupui, SIGNAL(changedUdpPrefs(quint64)), this, SLOT(extChangedUdpPrefs(quint64)));

    connect(setupui, SIGNAL(changedServerPref(prefServerItem)), this, SLOT(extChangedServerPref(prefServerItem)));
    connect(setupui, SIGNAL(changedServerPrefs(quint64)), this, SLOT(extChangedServerPrefs(quint64)));

    connect(this, SIGNAL(connectionStatus(bool)), setupui, SLOT(connectionStatus(bool)));
    connect(setupui, SIGNAL(connectButtonPressed()), this, SLOT(handleExtConnectBtn()));
    connect(setupui, SIGNAL(saveSettingsButtonPressed()), this, SLOT(on_saveSettingsBtn_clicked()));
    connect(setupui, SIGNAL(revertSettingsButtonPressed()), this, SLOT(handleRevertSettingsBtn()));
}

// NOT Migrated, EHL TODO, carefully remove this function

void wfmain::getSettingsFilePath(QString settingsFile)
{
    if (settingsFile.isNull()) {
        settings = new QSettings();
    }
    else
    {
        QString file = settingsFile;
        QFile info(settingsFile);
        QString path="";
        if (!QFileInfo(info).isAbsolute())
        {
            path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            if (path.isEmpty())
            {
                path = QDir::homePath();
            }
            path = path + "/";
            file = info.fileName();
        }
        settings = new QSettings(path + file, QSettings::Format::IniFormat);
    }
}


void wfmain::setInitialTiming()
{
    loopTickCounter = 0;
    delayedCmdIntervalLAN_ms = 70; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdIntervalSerial_ms = 100; // interval for regular delayed commands, including initial rig/UI state queries
    delayedCmdStartupInterval_ms = 250; // interval for rigID polling
    queue->interval(delayedCmdStartupInterval_ms);

    pttTimer = new QTimer(this);
    pttTimer->setInterval(180*1000); // 3 minute max transmit time in ms
    pttTimer->setSingleShot(true);
    connect(pttTimer, SIGNAL(timeout()), this, SLOT(handlePttLimit()));

    timeSync = new QTimer(this);
    connect(timeSync, SIGNAL(timeout()), this, SLOT(setRadioTimeDateSend()));
    waitingToSetTimeDate = false;
    lastFreqCmdTime_ms = QDateTime::currentMSecsSinceEpoch() - 5000; // 5 seconds ago
}

void wfmain::setServerToPrefs()
{

    // Start server if enabled in config
    //ui->serverSetupGroup->setEnabled(serverConfig.enabled);
    if (serverThread != Q_NULLPTR) {
        serverThread->quit();
        serverThread->wait();
        serverThread = Q_NULLPTR;
        server = Q_NULLPTR;
        ui->statusBar->showMessage(QString("Server disabled"), 1000);
    }

    if (serverConfig.enabled) {
        serverConfig.lan = prefs.enableLAN;

        switch (prefs.manufacturer)
        {
            case manufIcom:
                server = new icomServer(&serverConfig);
                break;
            case manufKenwood:
                server = new kenwoodServer(&serverConfig);
                break;
            case manufYaesu:
                server = new yaesuServer(&serverConfig);
                break;
        default:
            return;

        }

        serverThread = new QThread(this);

        server->moveToThread(serverThread);

        if (serverConfig.lan) {
            connect(server, SIGNAL(haveNetworkStatus(networkStatus)), this, SLOT(receiveStatusUpdate(networkStatus)));
        } else {
            qInfo(logAudio()) << "Audio Input device " << serverConfig.rigs.first()->rxAudioSetup.name;
            qInfo(logAudio()) << "Audio Output device " << serverConfig.rigs.first()->txAudioSetup.name;
        }

        connect(this, SIGNAL(initServer()), server, SLOT(init()));
        connect(serverThread, SIGNAL(finished()), server, SLOT(deleteLater()));

        serverThread->start();

        emit initServer();

        ui->statusBar->showMessage(QString("Server enabled"), 1000);

    }
}

void wfmain::configureVFOs()
{
    qInfo(logSystem()) << "Running configureVFOs()";

    if (QThread::currentThread() != QCoreApplication::instance()->thread())
    {
        qCritical(logSystem()) << "Thread is NOT the main UI thread, cannot create VFO";
        return;
    }

    if (receivers.size()) {
        foreach (receiverWidget* receiver, receivers)
        {
            ui->vfoLayout->removeWidget(receiver);
            delete receiver;
        }
        receivers.clear();
    }

    for(uchar i=0;i<rigCaps->numReceiver;i++)
    {
        receiverWidget* receiver = new receiverWidget(rigCaps->hasSpectrum,i,rigCaps->numVFO,this);
        receiver->prepareScope(rigCaps->spectAmpMax, rigCaps->spectLenMax);
        receiver->setSeparators(prefs.groupSeparator,prefs.decimalSeparator);
        receiver->setUnderlayMode(prefs.underlayMode);
        receiver->wfAntiAliased(prefs.wfAntiAlias);
        receiver->wfInterpolate(prefs.wfInterpolate);
        receiver->setScrollSpeedXY(prefs.scopeScrollX, prefs.scopeScrollY);
        receiver->prepareWf(i==0?prefs.mainWflength:prefs.subWflength);
        receiver->setRange(i==0?prefs.mainPlotFloor:prefs.subPlotFloor,i==0?prefs.mainPlotCeiling:prefs.subPlotCeiling);
        receiver->wfTheme(i==0?prefs.mainWfTheme:prefs.subWfTheme);
        receiver->setClickDragTuning(prefs.clickDragTuningEnable);
        receiver->setTuningFloorZeros(prefs.niceTS);
        receiver->resizePlasmaBuffer(prefs.underlayBufferSize);
        receiver->setUnit((FctlUnit)prefs.frequencyUnits);
        colorPrefsType p = colorPreset[prefs.currentColorPresetNumber];
        receiver->colorPreset(&p);
        receiver->setIdentity(i==0?"Main Band":"Sub Band");
        // IC7610_V23_CONNECT_RECEIVER_EXT
        connect(receiver, &receiverWidget::requestIC7610Ext,
                this, [=](uchar rx) {
            // v23 exposes the strip button only on MAIN, but preserve the
            // receiver argument so this is safe if SUB support is added later.
            currentReceiver = rx;
            QPushButton *legacyExt =
                    findChild<QPushButton*>(
                        QStringLiteral("ext7610Btn"));
            if (legacyExt != Q_NULLPTR)
                legacyExt->click();
        });
        ui->vfoLayout->addWidget(receiver);

        // Hide any secondary receivers until we need them!
        receiver->selected(i==0?true:false);
        receiver->setVisible(i==0?true:false);

        connect(receiver, SIGNAL(frequencyRange(uchar, double, double)), cluster, SLOT(freqRange(uchar, double, double)));

        connect(cluster, SIGNAL(sendSpots(uchar, QList<spotData>)), receiver, SLOT(receiveSpots(uchar, QList<spotData>)));
        connect(cluster, SIGNAL(sendOutput(QString)), this, SLOT(receiveClusterOutput(QString)));
        connect(receiver, SIGNAL(updateSettings(uchar,int,quint16,int,int)), this, SLOT(receiveScopeSettings(uchar,int,quint16,int,int)));

        connect(receiver, SIGNAL(dataChanged(modeInfo)), this, SLOT(dataModeChanged(modeInfo)));

        connect(receiver, &receiverWidget::bandChanged, receiver, [=](const uchar& rx, const bandType& b) {
            if (rx == currentReceiver && rigCaps->commands.contains(funcAntenna)) {
                ui->antennaSelCombo->setEnabled(b.ants);
                if (b.ants) {
                    queue->addUnique(priorityMediumHigh,queueItem(funcAntenna,true,i));
                } else {
                    queue->del(funcAntenna,i);
                }
            }
        });

        connect(receiver,SIGNAL(showStatusBarText(QString)),this,SLOT(showStatusBarText(QString)));
        connect(receiver,SIGNAL(sendScopeImage(uchar)),this,SLOT(receiveScopeImage(uchar)));
        receivers.append(receiver);

        // Currently tracking is disabled
        //if (receivers.size() > 1)
        //{
        //    connect(receivers[0], SIGNAL(sendTrack(int)),receivers[1], SLOT(receiveTrack(int)));
        //}

    }
}

void wfmain::setSerialDevicesUI()
{

}

QShortcut* wfmain::setupKeyShortcut(const QKeySequence k, bool appWide)
{
    QShortcut* sc=new QShortcut(this);
    sc->setKey(k);
    if(appWide)
        sc->setContext(Qt::ApplicationShortcut);

    connect(sc, &QShortcut::activated, this,
            [=]() {
        this->runShortcut(k);
    });
    return sc;
}

void wfmain::setupKeyShortcuts()
{
    shortcuts.append(setupKeyShortcut(Qt::Key_F1, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F2, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F3, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F4, true));
    shortcuts.append(setupKeyShortcut(Qt::Key_F5));
    shortcuts.append(setupKeyShortcut(Qt::Key_F6));
    shortcuts.append(setupKeyShortcut(Qt::Key_F7));
    shortcuts.append(setupKeyShortcut(Qt::Key_F8));
    shortcuts.append(setupKeyShortcut(Qt::Key_F9));
    shortcuts.append(setupKeyShortcut(Qt::Key_F10));
    shortcuts.append(setupKeyShortcut(Qt::Key_F11));
    shortcuts.append(setupKeyShortcut(Qt::Key_F12));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_T));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_R));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_P));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_I));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_U));
    shortcuts.append(setupKeyShortcut(Qt::Key_Asterisk));
    shortcuts.append(setupKeyShortcut(Qt::Key_Slash));
    shortcuts.append(setupKeyShortcut(Qt::Key_Backslash));
    shortcuts.append(setupKeyShortcut(Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Minus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Plus));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_Q));
    shortcuts.append(setupKeyShortcut(Qt::Key_PageUp));
    shortcuts.append(setupKeyShortcut(Qt::Key_PageDown));
    shortcuts.append(setupKeyShortcut(Qt::Key_F));
    shortcuts.append(setupKeyShortcut(Qt::Key_M));
    shortcuts.append(setupKeyShortcut(Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::SHIFT | Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_K));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_J));
    shortcuts.append(setupKeyShortcut(Qt::Key_H));
    shortcuts.append(setupKeyShortcut(Qt::Key_L));
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0))
    shortcuts.append(setupKeyShortcut(Qt::CTRL | Qt::Key_D));
#endif


}

void wfmain::runShortcut(const QKeySequence k)
{
    qDebug() << "Running shortcut for key:" << k;

    if (k == Qt::Key_F1)
    {
        this->raise();
    }
    else if (k==Qt::Key_F11)
    {
        if(onFullscreen)
        {
            this->showNormal();
            onFullscreen = false;
            prefs.useFullScreen = false;
        } else {
            this->showFullScreen();
            onFullscreen = true;
            prefs.useFullScreen = true;
        }
        setupui->updateIfPref(if_useFullScreen);
    }
    else if (k==(Qt::CTRL | Qt::Key_Q))
    {
        on_exitBtn_clicked();
    }
    else if (k==(Qt::CTRL | Qt::Key_D) || k==(Qt::CTRL | Qt::SHIFT | Qt::Key_D))
    {
        debugBtn_clicked();
    }
    else if (rigCaps == Q_NULLPTR)
    {
        // No rig yet
        qWarning() << "Cannot run shortcut as not connected to rig";
        return;
    }

    // All of the below commands require a connected rig
    bool freqUpdate=false;
    freqt f;

    if (k == Qt::Key_F2)
    {
        showAndRaiseWidget(bandbtns);
    }
    else if (k==Qt::Key_F3 || k==Qt::Key_Asterisk)
    {
        showAndRaiseWidget(finputbtns);
    }
    else if (k==Qt::Key_F4)
    {
        showAndRaiseWidget(setupui);
    }
    else if (k==Qt::Key_F5)
    {
        changeMode(modeLSB, false, currentReceiver);
    }
    else if (k==Qt::Key_F6)
    {
        changeMode(modeUSB, false, currentReceiver);
    }
    else if (k==Qt::Key_F7)
    {
        changeMode(modeAM, false, currentReceiver);
    }
    else if (k==Qt::Key_F8)
    {
        changeMode(modeCW, false, currentReceiver);
    }
    else if (k==Qt::Key_F9)
    {
        changeMode(modeUSB, true, currentReceiver);
    }
    else if (k==Qt::Key_F10)
    {
        changeMode(modeFM, false, currentReceiver);
    }
    else if (k==Qt::Key_F12)
    {
        showStatusBarText("Sending speech command to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech,QVariant::fromValue(uchar(0U)),false,currentReceiver));
    }
    else if (k==(Qt::CTRL | Qt::Key_T))
    {
        // Transmit
        qDebug(logSystem()) << "Activated Control-T shortcut";
        showStatusBarText(QString("Transmitting. Press Control-R to receive."));
        extChangedRsPrefs(rs_pttOn);
    }
    else if (k==(Qt::CTRL | Qt::Key_R))
    {
        // Receive
        extChangedRsPrefs(rs_pttOff);
    }
    else if (k==(Qt::CTRL | Qt::Key_P))
    {
        // Toggle PTT
        if(amTransmitting) {
            extChangedRsPrefs(rs_pttOff);
        } else {
            extChangedRsPrefs(rs_pttOn);
            showStatusBarText(QString("Transmitting. Press Control-P again to receive."));
        }
    }
    else if (k==(Qt::CTRL | Qt::Key_I))
    {
        // Enable ATU
        ui->tuneEnableChk->click();
    }
    else if (k==(Qt::CTRL | Qt::Key_U))
    {
        // Run ATU tuning cycle
        ui->tuneNowBtn->click();
    }
    else if (k==Qt::Key_Slash)
    {
        for (size_t i = 0; i < rigCaps->modes.size(); i++) {
            if (rigCaps->modes[i].mk == receivers[currentReceiver]->currentMode().mk)
            {
                if (i + 1 < rigCaps->modes.size()) {
                    changeMode(rigCaps->modes[i + 1].mk,false,currentReceiver);
                }
                else {
                    changeMode(rigCaps->modes[0].mk,false,currentReceiver);
                }
                break;
            }
        }
    }
    else if (k==Qt::Key_Backslash)
    {
        bool found = false;
        availableBands band = bandUnknown;
        auto b = rigCaps->bands.cend();
        while (b != rigCaps->bands.cbegin())
        {
            b--;
            if ((b->region == "" || prefs.region == b->region))
            {
                if (!found && b->band == bandbtns->currentBand())
                {
                    found = true;
                }
                else if (found && b->band != bandbtns->currentBand())
                {
                    qInfo() << "Got new band:" << b->band << "Name:" << b->name << "region" << b->region;
                    band = b->band;
                    break;
                }
            }
        }
        if (band == bandUnknown)
        {
            band = rigCaps->bands[rigCaps->bands.size() - 1].band;
        }
        bandbtns->setBand(band);
    }
    else if (k==Qt::Key_Minus|| k==Qt::Key_J)
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusHz);
        freqUpdate=true;
    }
    else if (k==Qt::Key_Plus || k==Qt::Key_K)
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusHz);
        freqUpdate=true;
    }
    else if (k==(Qt::SHIFT | Qt::Key_Minus) || k==(Qt::SHIFT | Qt::Key_J))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusShiftHz);
        freqUpdate=true;
    }
    else if (k==(Qt::SHIFT | Qt::Key_Plus) || k==(Qt::SHIFT | Qt::Key_K))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusShiftHz);
        freqUpdate=true;
    }
    else if (k==(Qt::CTRL | Qt::Key_Minus) || k==(Qt::CTRL | Qt::Key_J))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, -1, tsPlusControlHz);
        freqUpdate=true;
    }
    else if (k==(Qt::CTRL | Qt::Key_Plus) || k==(Qt::CTRL | Qt::Key_K))
    {
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, 1, tsPlusControlHz);
        freqUpdate=true;
    }
    else if (k==Qt::Key_PageUp)
    {
        f.Hz = receivers[0]->getFrequency().Hz + tsPageHz;
        freqUpdate=true;
    }
    else if (k==Qt::Key_PageDown)
    {
        f.Hz = receivers[0]->getFrequency().Hz - tsPageHz;
        freqUpdate=true;
    }
    else if (k==Qt::Key_F)
    {
        showStatusBarText("Sending speech command (frequency) to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech, QVariant::fromValue<uchar>(1),false,currentReceiver));
    }
    else if (k==Qt::Key_M)
    {
        showStatusBarText("Sending speech command (mode) to radio.");
        queue->add(priorityImmediate,queueItem(funcSpeech, QVariant::fromValue<uchar>(2),false,currentReceiver));
    }
    else if (k==Qt::Key_H)
    {
        f.Hz = roundFrequencyWithStep(receivers.first()->getFrequency().Hz, -1, tsKnobHz);
        freqUpdate = true;
    }
    else if (k==Qt::Key_L)
    {
        f.Hz = roundFrequencyWithStep(receivers.first()->getFrequency().Hz, 1, tsKnobHz);
        freqUpdate = true;
    }


    if (!freqLock && freqUpdate)
    {
        vfoCommandType t = queue->getVfoCommand(vfoA,currentReceiver,true);
        f.MHzDouble = f.Hz / (double)1E6;
        queue->add(priorityImmediate,queueItem(t.freqFunc,
                                                        QVariant::fromValue<freqt>(f),false,uchar(t.receiver)));
    }
}

void wfmain::setupUsbControllerDevice()
{
#if defined (USB_CONTROLLER)

    if (usbWindow == Q_NULLPTR) {
        usbWindow = new controllerSetup();
    }

    usbControllerDev = new usbController();
    usbControllerThread = new QThread(this);
    usbControllerThread->setObjectName("usb()");
    usbControllerDev->moveToThread(usbControllerThread);
    connect(usbControllerThread, SIGNAL(started()), usbControllerDev, SLOT(run()));
    connect(usbControllerThread, SIGNAL(finished()), usbControllerDev, SLOT(deleteLater()));
    connect(usbControllerThread, SIGNAL(finished()), usbWindow, SLOT(deleteLater())); // Delete window when controller is deleted
    connect(usbControllerDev, SIGNAL(sendJog(int)), this, SLOT(changeFrequency(int)));
    connect(usbControllerDev, SIGNAL(doShuttle(bool,quint8)), this, SLOT(doShuttle(bool,quint8)));
    connect(usbControllerDev, SIGNAL(button(const COMMAND*)), this, SLOT(buttonControl(const COMMAND*)));
    connect(usbControllerDev, SIGNAL(removeDevice(USBDEVICE*)), usbWindow, SLOT(removeDevice(USBDEVICE*)));
    connect(usbControllerDev, SIGNAL(initUI(usbDevMap*, QVector<BUTTON>*, QVector<KNOB>*, QVector<COMMAND>*, QMutex*)), usbWindow, SLOT(init(usbDevMap*, QVector<BUTTON>*, QVector<KNOB>*, QVector<COMMAND>*, QMutex*)));
    connect(usbControllerDev, SIGNAL(changePage(USBDEVICE*,int)), usbWindow, SLOT(pageChanged(USBDEVICE*,int)));
    connect(usbControllerDev, SIGNAL(setConnected(USBDEVICE*)), usbWindow, SLOT(setConnected(USBDEVICE*)));
    connect(usbControllerDev, SIGNAL(newDevice(USBDEVICE*)), usbWindow, SLOT(newDevice(USBDEVICE*)));
    usbControllerThread->start(QThread::LowestPriority);

    connect(usbWindow, SIGNAL(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)), usbControllerDev, SLOT(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)));
    connect(this, SIGNAL(sendControllerRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)), usbControllerDev, SLOT(sendRequest(USBDEVICE*, usbFeatureType, int, QString, QImage*, QColor *)));
    connect(usbWindow, SIGNAL(programPages(USBDEVICE*,int)), usbControllerDev, SLOT(programPages(USBDEVICE*,int)));
    connect(usbWindow, SIGNAL(programDisable(USBDEVICE*,bool)), usbControllerDev, SLOT(programDisable(USBDEVICE*,bool)));
    connect(this, SIGNAL(initUsbController(QMutex*,usbDevMap*,QVector<BUTTON>*,QVector<KNOB>*)), usbControllerDev, SLOT(init(QMutex*,usbDevMap*,QVector<BUTTON>*,QVector<KNOB>*)));
    connect(this, SIGNAL(usbHotplug()), usbControllerDev, SLOT(run()));
    connect(usbWindow, SIGNAL(backup(USBDEVICE*,QString)), usbControllerDev, SLOT(backupController(USBDEVICE*,QString)));
    connect(usbWindow, SIGNAL(restore(USBDEVICE*,QString)), usbControllerDev, SLOT(restoreController(USBDEVICE*,QString)));

#endif
}

void wfmain::pttToggle(bool status)
{
    // is it enabled?

    if (!prefs.enablePTT)
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }
    queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(status),false,uchar(0)));

    // Start 3 minute timer
    if (status)
        pttTimer->start();
}

void wfmain::doShuttle(bool up, quint8 level)
{
    if (level == 1 && up)
        runShortcut(Qt::SHIFT | Qt::Key_Plus);
    else if (level == 1 && !up)
        runShortcut(Qt::SHIFT | Qt::Key_Minus);
    else if (level > 1 && level < 5 && up)
        for (int i = 1; i < level; i++)
            runShortcut(Qt::Key_Plus);
    else if (level > 1 && level < 5 && !up)
        for (int i = 1; i < level; i++)
            runShortcut(Qt::Key_Minus);
    else if (level > 4 && up)
        for (int i = 4; i < level; i++)
            runShortcut(Qt::CTRL | Qt::Key_Plus);
    else if (level > 4 && !up)
        for (int i = 4; i < level; i++)
            runShortcut(Qt::CTRL | Qt::Key_Minus);
}

void wfmain::buttonControl(const COMMAND* cmd)
{
    qDebug(logUsbControl()) << QString("executing command: %0 (%1) suffix:%2 value:%3" ).arg(cmd->text).arg(funcString[cmd->command]).arg(cmd->suffix).arg(cmd->value);
    quint8 vfo=0;
    if (rigCaps == Q_NULLPTR) {
        return;
    }

    switch (cmd->command) {
    case funcBandStackReg:
    {
        bool found = false;
        availableBands band = bandUnknown;
        if (cmd->value == 1) {
            auto b = rigCaps->bands.cend();
            while (b != rigCaps->bands.cbegin())
            {
                b--;
                if ((b->region == "" || prefs.region == b->region))
                {
                    if (!found && b->band == bandbtns->currentBand()) {
                        found = true;
                    } else if (found && b->band != bandbtns->currentBand()) {
                        qInfo() << "Got new band:" << b->band << "Name:" << b->name << "region" << b->region;
                        band = b->band;
                        break;
                    }
                }
            }
            if (band == bandUnknown)
            {
                band = rigCaps->bands[rigCaps->bands.size() - 1].band;
            }
        } else if (cmd->value == -1) {
            auto b = rigCaps->bands.cbegin();
            while (b != rigCaps->bands.cend())
            {
                if ((b->region == "" || prefs.region == b->region))
                {
                    if (!found && b->band == bandbtns->currentBand()) {
                        found = true;
                    } else if (found && b->band != bandbtns->currentBand()) {
                        qInfo() << "Got band:" << b->band << "Name:" << b->name << "region" << b->region;
                        band = b->band;
                        break;
                    }
                }
                b++;
            }
            if (band == bandUnknown) {
                band = rigCaps->bands[0].band;
            }
        } else {
            band=cmd->band;
        }
        bandbtns->setBand(band);
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedMode:
        vfo=1;
    case funcMode:
    case funcModeSet:
        qInfo(logRig()) << "Setting mode" << cmd->value << "or" << cmd->mode;
        if (cmd->suffix < receivers.size())
        {
            if (cmd->value == 1)
            {
                for (size_t i = 0; i < rigCaps->modes.size(); i++)
                {
                    if (rigCaps->modes[i].mk == receivers[cmd->suffix]->currentMode().mk)
                    {
                        if (i + 1 < rigCaps->modes.size())
                        {
                            changeMode(rigCaps->modes[i + 1].mk,false,cmd->suffix);
                        }
                        else
                        {
                            changeMode(rigCaps->modes[0].mk,false,cmd->suffix);
                        }
                    }
                }
            }
            else if (cmd->value == -1)
            {
                for (size_t i = 0; i < rigCaps->modes.size(); i++)
                {
                    if (rigCaps->modes[i].mk == receivers[cmd->suffix]->currentMode().mk)
                    {
                        if (i>0)
                        {
                            changeMode(rigCaps->modes[i - 1].mk,false,cmd->suffix);
                        }
                        else
                        {
                            changeMode(rigCaps->modes[rigCaps->modes.size()-1].mk,false,cmd->suffix);
                        }
                    }
                }
            }
            else
            {
                changeMode(cmd->mode,false,cmd->suffix);
            }
        }
        break;
    case funcTuningStep:
        if ((cmd->value > 0 && ui->tuningStepCombo->currentIndex()  < ui->tuningStepCombo->count()-cmd->value) ||
            (cmd->value < 0 && ui->tuningStepCombo->currentIndex() > 0))
        {
            ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() + cmd->value);
        }
        else
        {
            if (cmd->value < 0)
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->count()-1);
            else
                ui->tuningStepCombo->setCurrentIndex(0);
        }
        break;
    case funcScopeSpan:
        if (cmd->suffix < receivers.size()) {
            receivers[cmd->suffix]->changeSpan(cmd->value);
        }
        break;
    case funcUnselectedFreq:
        vfo=1;
    case funcFreq:
    case funcSelectedFreq:
    {
        if (!freqLock)
        {
            freqt f;
            if (cmd->suffix < receivers.size()) {
                f.Hz = roundFrequencyWithStep(receivers[cmd->suffix]->getFrequency().Hz, cmd->value, tsWfScrollHz);
            } else {
                f.Hz = 0;
            }
            f.MHzDouble = f.Hz / double(1E6);
            f.VFO=(selVFO_t)cmd->suffix;
            vfoCommandType t = queue->getVfoCommand(vfo?vfoB:vfoA,cmd->suffix,true);
            //qInfo() << "sending command" << funcString[t.freqFunc] << "freq:" << f.Hz << "rx:" << t.receiver << "vfo:" << t.vfo;
            queue->add(priorityImmediate,queueItem(t.freqFunc,QVariant::fromValue<freqt>(f),false,t.receiver));
        }
        break;
    }

    // These are 2 byte values, so use ushort (even though they would fit in a uchar)
    case funcCwPitch:
    case funcKeySpeed:
    case funcAfGain:
    case funcRfGain:
    case funcSquelch:
    case funcAPFLevel:
    case funcNRLevel:
    case funcPBTInner:
    case funcPBTOuter:
    case funcIFShift:
    case funcRFPower:
    case funcMicGain:
    case funcLANModLevel:
    case funcUSBModLevel:
    case funcACCAModLevel:
    case funcNotchFilter:
    case funcCompressorLevel:
    case funcBreakInDelay:
    case funcNBLevel:
    case funcDigiSelShift:
    case funcDriveGain:
    case funcMonitorGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        qInfo(logUsbControl()) << "Command" << cmd->command << "Value" << cmd->value;
        queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<ushort>(cmd->value),false,cmd->suffix));
        break;

    case funcTransceiverStatus:
    {
        if (cmd->value == -1)
        {
            // toggle PTT
            on_transmitBtn_clicked();
            break;
        }

    }
    default:
        qInfo(logUsbControl()) << "Command" << cmd->command << "Value" << cmd->value << "Suffix" << cmd->suffix;
        if (cmd->suffix == 0xff) {
            queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<uchar>(cmd->value),false,cmd->suffix));
        } else {
            queue->add(priorityImmediate,queueItem((funcs)cmd->command,QVariant::fromValue<uchar>(cmd->suffix),false,0));
        }
        break;
    }
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
}

void wfmain::stepUp()
{
    if (ui->tuningStepCombo->currentIndex() < ui->tuningStepCombo->count() - 1)
        ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() + 1);
}

void wfmain::stepDown()
{
    if (ui->tuningStepCombo->currentIndex() > 0)
        ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->currentIndex() - 1);
}

void wfmain::changeFrequency(int value) {
    if (freqLock) return;

    if (receivers.size())
    {
        freqt f;
        f.Hz = roundFrequencyWithStep(receivers[0]->getFrequency().Hz, value, tsWfScrollHz);
        f.MHzDouble = f.Hz / (double)1E6;
        vfoCommandType t = queue->getVfoCommand(vfoA,currentReceiver,true);
        queue->add(priorityImmediate,queueItem(t.freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(t.receiver)));
    }
}

void wfmain::setDefPrefs()
{
    defPrefs.hasRunSetup = false;
    defPrefs.useFullScreen = false;
    defPrefs.useSystemTheme = false;
    defPrefs.drawPeaks = true;
    defPrefs.currentColorPresetNumber = 0;
    defPrefs.underlayMode = underlayNone;
    defPrefs.underlayBufferSize = 64;
    defPrefs.wfEnable = 2;
    defPrefs.wfAntiAlias = false;
    defPrefs.wfInterpolate = true;
    defPrefs.stylesheetPath = QString("qdarkstyle/style.qss");
    defPrefs.radioCIVAddr = 0x00; // previously was 0x94 for 7300.
    defPrefs.CIVisRadioModel = false;
    defPrefs.pttType = pttCIV;
    defPrefs.serialPortRadio = QString("auto");
    defPrefs.serialPortBaud = 115200;
    defPrefs.enableLAN = false;
    defPrefs.polling_ms = 0; // 0 = Automatic
    defPrefs.enablePTT = true;
    defPrefs.niceTS = true;
    defPrefs.enableRigCtlD = false;
    defPrefs.rigCtlPort = 4533;
    defPrefs.virtualSerialPort = QString("none");
    defPrefs.localAFgain = 255;
    defPrefs.mainWflength = 160;
    defPrefs.mainWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.mainPlotFloor = 0;
    defPrefs.mainPlotCeiling = 160;
    defPrefs.subWflength = 160;
    defPrefs.subWfTheme = static_cast<int>(QCPColorGradient::gpJet);
    defPrefs.subPlotFloor = 0;
    defPrefs.subPlotCeiling = 160;
    defPrefs.scopeScrollX = 120;
    defPrefs.scopeScrollY = 120;
    defPrefs.confirmExit = true;
    defPrefs.confirmPowerOff = true;
    defPrefs.confirmSettingsChanged = true;
    defPrefs.confirmMemories = false;
    defPrefs.meter1Type = meterS;
    defPrefs.meter2Type = meterNone;
    defPrefs.meter3Type = meterNone;
    defPrefs.compMeterReverse = false;
    defPrefs.region = "1";
    defPrefs.showBands = true;
    defPrefs.manufacturer = manufIcom;
    defPrefs.useUTC = false;
    defPrefs.setRadioTime = false;
    defPrefs.forceVfoMode = true;
    defPrefs.autoPowerOn=true;

    defPrefs.tcpPort = 0;
    defPrefs.tciPort = 50001;
    defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.waterfallFormat = 0;
    defPrefs.audioSystem = qtAudio;
    defPrefs.enableUSBControllers = false;
#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
    defPrefs.groupSeparator = QLocale().groupSeparator();
    defPrefs.decimalSeparator = QLocale().decimalPoint();
#else
    defPrefs.groupSeparator = QLocale().groupSeparator().at(0);       // digit group separator
    defPrefs.decimalSeparator = QLocale().decimalPoint().at(0);       // digit group separator
#endif

    // Audio
    defPrefs.rxSetup.latency = 150;
    defPrefs.txSetup.latency = 150;
    defPrefs.rxSetup.isinput = false;
    defPrefs.txSetup.isinput = true;
    defPrefs.rxSetup.sampleRate = 48000;
    defPrefs.rxSetup.codec = 4;
    defPrefs.txSetup.codec = 4;
    defPrefs.rxSetup.resampleQuality = 4;
	
	// Cluster
	defPrefs.clusterUdpEnable = false;
    defPrefs.clusterTcpEnable = false;
    defPrefs.clusterUdpPort = 12060;
	
	// CW
	defPrefs.cwCutNumbers=false;
    defPrefs.cwSendImmediate=false;
    defPrefs.cwSidetoneEnabled=true;
    defPrefs.cwSidetoneLevel=100;

    udpDefPrefs.ipAddress = QString("");
    udpDefPrefs.controlLANPort = 50001;
    udpDefPrefs.serialLANPort = 50002;
    udpDefPrefs.audioLANPort = 50003;
    udpDefPrefs.scopeLANPort = 50004;
    udpDefPrefs.username = QString("");
    udpDefPrefs.password = QString("");
    udpDefPrefs.clientName = QHostInfo::localHostName();
    udpDefPrefs.connectionType = connectionLAN;
    udpDefPrefs.adminLogin = false;
}

void wfmain::loadSettings()
{
    qInfo(logSystem()) << "Loading settings from " << settings->fileName();

    QString currentVersionString = QString(WFVIEW_VERSION);
    float currentVersionFloat = currentVersionString.toFloat();

    settings->beginGroup("Program");
    QString priorVersionString = settings->value("version", "unset").toString();
    float priorVersionFloat = priorVersionString.toFloat();
    if (currentVersionString != priorVersionString)
    {
        qWarning(logSystem()) << "Settings previously saved under version " << priorVersionString << ", you should review your settings and press SaveSettings.";
    }
    if(priorVersionFloat > currentVersionFloat)
    {
        qWarning(logSystem()).noquote().nospace() << "It looks like the previous version of wfview (" << priorVersionString << ") was newer than this version (" << currentVersionString << ")";
    }
    prefs.version = priorVersionString;
    prefs.majorVersion = settings->value("majorVersion", defPrefs.majorVersion).toInt();
    prefs.minorVersion = settings->value("minorVersion", defPrefs.minorVersion).toInt();
    prefs.hasRunSetup = settings->value("hasRunSetup", defPrefs.hasRunSetup).toBool();
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    prefs.useFullScreen = settings->value("UseFullScreen", defPrefs.useFullScreen).toBool();
    prefs.useSystemTheme = settings->value("UseSystemTheme", defPrefs.useSystemTheme).toBool();
    prefs.wfEnable = settings->value("WFEnable", defPrefs.wfEnable).toInt();
    //ui->scopeEnableWFBtn->setCheckState(Qt::CheckState(prefs.wfEnable));
    prefs.mainWfTheme = settings->value("MainWFTheme", defPrefs.mainWfTheme).toInt();
    prefs.subWfTheme = settings->value("SubWFTheme", defPrefs.subWfTheme).toInt();
    prefs.mainPlotFloor = settings->value("MainPlotFloor", defPrefs.mainPlotFloor).toInt();
    prefs.subPlotFloor = settings->value("SubPlotFloor", defPrefs.subPlotFloor).toInt();
    prefs.mainPlotCeiling = settings->value("MainPlotCeiling", defPrefs.mainPlotCeiling).toInt();
    prefs.subPlotCeiling = settings->value("SubPlotCeiling", defPrefs.subPlotCeiling).toInt();
    prefs.scopeScrollX = settings->value("scopeScrollX", defPrefs.scopeScrollX).toInt();
    prefs.scopeScrollY = settings->value("scopeScrollY", defPrefs.scopeScrollY).toInt();
    prefs.decimalSeparator = settings->value("DecimalSeparator", defPrefs.decimalSeparator).toChar();
    prefs.groupSeparator = settings->value("GroupSeparator", defPrefs.groupSeparator).toChar();
    prefs.forceVfoMode =  settings->value("ForceVfoMode", defPrefs.groupSeparator).toBool();
    prefs.autoPowerOn =  settings->value("AutoPowerOn", defPrefs.autoPowerOn).toBool();

    prefs.drawPeaks = settings->value("DrawPeaks", defPrefs.drawPeaks).toBool();
    prefs.underlayBufferSize = settings->value("underlayBufferSize", defPrefs.underlayBufferSize).toInt();
    prefs.underlayMode = static_cast<underlay_t>(settings->value("underlayMode", defPrefs.underlayMode).toInt());
    prefs.wfAntiAlias = settings->value("WFAntiAlias", defPrefs.wfAntiAlias).toBool();
    prefs.wfInterpolate = settings->value("WFInterpolate", defPrefs.wfInterpolate).toBool();
    prefs.mainWflength = (unsigned int)settings->value("MainWFLength", defPrefs.mainWflength).toInt();
    prefs.subWflength = (unsigned int)settings->value("SubWFLength", defPrefs.subWflength).toInt();
    prefs.stylesheetPath = settings->value("StylesheetPath", defPrefs.stylesheetPath).toString();
    //ui->splitter->restoreState(settings->value("splitter").toByteArray());

    restoreGeometry(settings->value("windowGeometry").toByteArray());
    restoreState(settings->value("windowState").toByteArray());
    setWindowState(Qt::WindowActive); // Works around QT bug to returns window+keyboard focus.

    if (bandbtns != Q_NULLPTR)
        bandbtns->setGeometry(settings->value("BandWindowGeometry").toByteArray());

    if (finputbtns != Q_NULLPTR)
        finputbtns->setGeometry(settings->value("FreqWindowGeometry").toByteArray());

    prefs.confirmExit = settings->value("ConfirmExit", defPrefs.confirmExit).toBool();
    prefs.confirmPowerOff = settings->value("ConfirmPowerOff", defPrefs.confirmPowerOff).toBool();
    prefs.confirmSettingsChanged = settings->value("ConfirmSettingsChanged", defPrefs.confirmSettingsChanged).toBool();
    prefs.confirmMemories = settings->value("ConfirmMemories", defPrefs.confirmMemories).toBool();
    prefs.meter1Type = static_cast<meter_t>(settings->value("Meter1Type", defPrefs.meter1Type).toInt());
    prefs.meter2Type = static_cast<meter_t>(settings->value("Meter2Type", defPrefs.meter2Type).toInt());
    prefs.meter3Type = static_cast<meter_t>(settings->value("Meter3Type", defPrefs.meter3Type).toInt());
    prefs.compMeterReverse = settings->value("compMeterReverse", defPrefs.compMeterReverse).toBool();
    prefs.clickDragTuningEnable = settings->value("ClickDragTuning", false).toBool();

    prefs.useUTC = settings->value("UseUTC", defPrefs.useUTC).toBool();
    prefs.setRadioTime = settings->value("SetRadioTime", defPrefs.setRadioTime).toBool();

    prefs.rigCreatorEnable = settings->value("RigCreator",false).toBool();
    prefs.region = settings->value("Region",defPrefs.region).toString();
    bandbtns->setRegion(prefs.region);
    prefs.showBands = settings->value("ShowBands",defPrefs.showBands).toBool();

    ui->rigCreatorBtn->setEnabled(prefs.rigCreatorEnable);

    prefs.frequencyUnits = settings->value("FrequencyUnits",3).toInt();

    settings->endGroup();

    // Load in the color presets. The default values are already loaded.

    settings->beginGroup("ColorPresets");
    prefs.currentColorPresetNumber = settings->value("currentColorPresetNumber", defPrefs.currentColorPresetNumber).toInt();
    if(prefs.currentColorPresetNumber > numColorPresetsTotal-1)
        prefs.currentColorPresetNumber = 0;

    int numPresetsInFile = settings->beginReadArray("ColorPreset");
    // We will use the number of presets that the working copy of wfview
    // supports, as we must never exceed the available number.
    if (numPresetsInFile > 0)
    {
        colorPrefsType* p;
        QString tempName;
        for (int pn = 0; pn < numColorPresetsTotal; pn++)
        {
            settings->setArrayIndex(pn);
            p = &(colorPreset[pn]);
            p->presetNum = settings->value("presetNum", p->presetNum).toInt();
            tempName = settings->value("presetName", *p->presetName).toString();
            if ((!tempName.isEmpty()) && tempName.length() < 11)
            {
                    p->presetName->clear();
                    p->presetName->append(tempName);
            }
            p->gridColor = colorFromString(settings->value("gridColor", p->gridColor.name(QColor::HexArgb)).toString());
            p->axisColor = colorFromString(settings->value("axisColor", p->axisColor.name(QColor::HexArgb)).toString());
            p->textColor = colorFromString(settings->value("textColor", p->textColor.name(QColor::HexArgb)).toString());
            p->spectrumLine = colorFromString(settings->value("spectrumLine", p->spectrumLine.name(QColor::HexArgb)).toString());
            p->spectrumFill = colorFromString(settings->value("spectrumFill", p->spectrumFill.name(QColor::HexArgb)).toString());
            p->useSpectrumFillGradient = settings->value("useSpectrumFillGradient", p->useSpectrumFillGradient).toBool();
            p->spectrumFillTop = colorFromString(settings->value("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb)).toString());
            p->spectrumFillBot = colorFromString(settings->value("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb)).toString());
            p->underlayLine = colorFromString(settings->value("underlayLine", p->underlayLine.name(QColor::HexArgb)).toString());
            p->underlayFill = colorFromString(settings->value("underlayFill", p->underlayFill.name(QColor::HexArgb)).toString());
            p->useUnderlayFillGradient = settings->value("useUnderlayFillGradient", p->useUnderlayFillGradient).toBool();
            p->underlayFillTop = colorFromString(settings->value("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb)).toString());
            p->underlayFillBot = colorFromString(settings->value("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb)).toString());
            p->plotBackground = colorFromString(settings->value("plotBackground", p->plotBackground.name(QColor::HexArgb)).toString());
            p->tuningLine = colorFromString(settings->value("tuningLine", p->tuningLine.name(QColor::HexArgb)).toString());
            p->passband = colorFromString(settings->value("passband", p->passband.name(QColor::HexArgb)).toString());
            p->pbt = colorFromString(settings->value("pbt", p->pbt.name(QColor::HexArgb)).toString());
            p->wfBackground = colorFromString(settings->value("wfBackground", p->wfBackground.name(QColor::HexArgb)).toString());
            p->wfGrid = colorFromString(settings->value("wfGrid", p->wfGrid.name(QColor::HexArgb)).toString());
            p->wfAxis = colorFromString(settings->value("wfAxis", p->wfAxis.name(QColor::HexArgb)).toString());
            p->wfText = colorFromString(settings->value("wfText", p->wfText.name(QColor::HexArgb)).toString());
            p->meterLevel = colorFromString(settings->value("meterLevel", p->meterLevel.name(QColor::HexArgb)).toString());
            p->meterAverage = colorFromString(settings->value("meterAverage", p->meterAverage.name(QColor::HexArgb)).toString());
            p->meterPeakLevel = colorFromString(settings->value("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb)).toString());
            p->meterPeakScale = colorFromString(settings->value("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb)).toString());
            p->meterLowerLine = colorFromString(settings->value("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb)).toString());
            p->meterLowText = colorFromString(settings->value("meterLowText", p->meterLowText.name(QColor::HexArgb)).toString());
            p->clusterSpots = colorFromString(settings->value("clusterSpots", p->clusterSpots.name(QColor::HexArgb)).toString());
            p->buttonOff = colorFromString(settings->value("buttonOff", p->buttonOff.name(QColor::HexArgb)).toString());
            p->buttonOn = colorFromString(settings->value("buttonOn", p->buttonOn.name(QColor::HexArgb)).toString());
        }
    }
    settings->endArray();
    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    prefs.manufacturer = (manufacturersType_t)settings->value("Manufacturer", defPrefs.manufacturer).value<manufacturersType_t>();
    prefs.radioCIVAddr = (quint16)settings->value("RigCIVuInt", defPrefs.radioCIVAddr).toInt();
    prefs.CIVisRadioModel = (bool)settings->value("CIVisRadioModel", defPrefs.CIVisRadioModel).toBool();
    prefs.pttType = (pttType_t)settings->value("PTTType", defPrefs.pttType).toInt();
    prefs.serialPortRadio = settings->value("SerialPortRadio", defPrefs.serialPortRadio).toString();
    prefs.serialPortBaud = (quint32)settings->value("SerialPortBaud", defPrefs.serialPortBaud).toInt();

    if (prefs.serialPortBaud > 0)
    {
        serverConfig.baudRate = prefs.serialPortBaud;
    }

    prefs.polling_ms = settings->value("polling_ms", defPrefs.polling_ms).toInt();
    // Migrated
    if(prefs.polling_ms == 0)
    {
        // Automatic

    } else {
        // Manual

    }

    prefs.virtualSerialPort = settings->value("VirtualSerialPort", defPrefs.virtualSerialPort).toString();


    prefs.localAFgain = (quint8)settings->value("localAFgain", defPrefs.localAFgain).toUInt();
    prefs.rxSetup.localAFgain = prefs.localAFgain;
    prefs.txSetup.localAFgain = 255;

    prefs.audioSystem = static_cast<audioType>(settings->value("AudioSystem", defPrefs.audioSystem).toInt());

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    prefs.enablePTT = settings->value("EnablePTT", defPrefs.enablePTT).toBool();

    prefs.niceTS = settings->value("NiceTS", defPrefs.niceTS).toBool();

    prefs.automaticSidebandSwitching = settings->value("automaticSidebandSwitching", defPrefs.automaticSidebandSwitching).toBool();
    settings->endGroup();

    settings->beginGroup("LAN");

    prefs.enableLAN = settings->value("EnableLAN", defPrefs.enableLAN).toBool();

    // If LAN is enabled, server gets its audio straight from the LAN
    // migrated, remove these
    //ui->serverRXAudioInputCombo->setEnabled(!prefs.enableLAN);
    //ui->serverTXAudioOutputCombo->setEnabled(!prefs.enableLAN);

    // If LAN is not enabled, disable local audio input/output
    //ui->audioOutputCombo->setEnabled(prefs.enableLAN);
    //ui->audioInputCombo->setEnabled(prefs.enableLAN);

    ui->connectBtn->setEnabled(true);

    prefs.enableRigCtlD = settings->value("EnableRigCtlD", defPrefs.enableRigCtlD).toBool();
    prefs.rigCtlPort = settings->value("RigCtlPort", defPrefs.rigCtlPort).toInt();

    prefs.tcpPort = settings->value("TCPServerPort", defPrefs.tcpPort).toInt();
    prefs.tciPort = settings->value("TCIServerPort", defPrefs.tciPort).toInt();

    prefs.waterfallFormat = settings->value("WaterfallFormat", defPrefs.waterfallFormat).toInt();

    udpPrefs.ipAddress = settings->value("IPAddress", udpDefPrefs.ipAddress).toString();
    udpPrefs.controlLANPort = settings->value("ControlLANPort", udpDefPrefs.controlLANPort).toInt();
    udpPrefs.serialLANPort = settings->value("SerialLANPort", udpDefPrefs.serialLANPort).toInt();
    udpPrefs.audioLANPort = settings->value("AudioLANPort", udpDefPrefs.audioLANPort).toInt();
    udpPrefs.scopeLANPort = settings->value("ScopeLANPort", udpDefPrefs.scopeLANPort).toInt();
    udpPrefs.adminLogin = settings->value("AdminLogin",udpDefPrefs.adminLogin).toBool();
    udpPrefs.username = settings->value("Username", udpDefPrefs.username).toString();
    udpPrefs.password = settings->value("Password", udpDefPrefs.password).toString();
    prefs.rxSetup.isinput = defPrefs.rxSetup.isinput;
    prefs.txSetup.isinput = defPrefs.txSetup.isinput;

    prefs.rxSetup.latency = settings->value("AudioRXLatency", defPrefs.rxSetup.latency).toInt();
    prefs.txSetup.latency = settings->value("AudioTXLatency", defPrefs.txSetup.latency).toInt();

    prefs.rxSetup.sampleRate=settings->value("AudioRXSampleRate", defPrefs.rxSetup.sampleRate).toInt();
    prefs.txSetup.sampleRate=prefs.rxSetup.sampleRate;

    prefs.rxSetup.codec = settings->value("AudioRXCodec", defPrefs.rxSetup.codec).toInt();
    prefs.txSetup.codec = settings->value("AudioTXCodec", defPrefs.txSetup.codec).toInt();
    prefs.rxSetup.name = settings->value("AudioOutput", "Default Output Device").toString();
    qInfo(logGui()) << "Got Audio Output from Settings: " << prefs.rxSetup.name;

    prefs.txSetup.name = settings->value("AudioInput", "Default Input Device").toString();
    qInfo(logGui()) << "Got Audio Input from Settings: " << prefs.txSetup.name;

    prefs.rxSetup.resampleQuality = settings->value("ResampleQuality", defPrefs.rxSetup.resampleQuality).toInt();
    prefs.txSetup.resampleQuality = prefs.rxSetup.resampleQuality;

    // TX audio processing prefs
    prefs.txAudioProc.bypass        = settings->value("TxProcBypass",   true).toBool();
    prefs.txAudioProc.compEnabled   = settings->value("TxProcCompEnabled",   false).toBool();
    prefs.txAudioProc.eqEnabled     = settings->value("TxProcEqEnabled",     false).toBool();
    prefs.txAudioProc.eqFirst       = settings->value("TxProcEqFirst",       true).toBool();
    prefs.txAudioProc.inputGainDB   = settings->value("TxProcInputGain",     10.0f).toFloat();
    prefs.txAudioProc.outputGainDB  = settings->value("TxProcOutputGain",    0.0f).toFloat();
    prefs.txAudioProc.compPeakLimit = settings->value("TxProcCompPeak",     -10.0f).toFloat();
    prefs.txAudioProc.compRelease   = settings->value("TxProcCompRelease",   0.1f).toFloat();
    prefs.txAudioProc.compFastRatio = settings->value("TxProcCompFast",      0.2f).toFloat();
    prefs.txAudioProc.compSlowRatio = settings->value("TxProcCompSlow",      0.2f).toFloat();
    prefs.txAudioProc.sidetoneEnabled = settings->value("TxProcSidetone",        false).toBool();
    prefs.txAudioProc.sidetoneLevel   = settings->value("TxProcSidetoneLevel",   0.5f).toFloat();
    prefs.txAudioProc.spectrumEnabled     = settings->value("TxProcSpectrumEnabled",     false).toBool();
    prefs.txAudioProc.spectrumFPS         = settings->value("TxProcSpectrumFps",         10).toInt();
    prefs.txAudioProc.specInhibitDuringRx = settings->value("TxProcSpecInhibitDuringRx", true).toBool();
    for (int i = 0; i < TxAudioProcessor::EQ_BANDS; ++i)
        prefs.txAudioProc.eqBands[i] = settings->value(
            QString("TxProcEqBand%1").arg(i), 0.0f).toFloat();
    prefs.txAudioProc.gateEnabled   = settings->value("TxProcGateEnabled",   false).toBool();
    prefs.txAudioProc.gateThreshold = settings->value("TxProcGateThreshold", -60.0f).toFloat();
    prefs.txAudioProc.gateAttack    = settings->value("TxProcGateAttack",     5.0f).toFloat();
    prefs.txAudioProc.gateHold      = settings->value("TxProcGateHold",      40.0f).toFloat();
    prefs.txAudioProc.gateDecay     = settings->value("TxProcGateDecay",     20.0f).toFloat();
    prefs.txAudioProc.gateRange     = settings->value("TxProcGateRange",     -90.0f).toFloat();
    prefs.txAudioProc.gateLfCutoff  = settings->value("TxProcGateLfCutoff",   380.0f).toFloat();
    prefs.txAudioProc.gateHfCutoff  = settings->value("TxProcGateHfCutoff", 2700.0f).toFloat();

    // RX audio processing prefs
    prefs.rxAudioProc.bypass           = settings->value("RxProcBypass",           true).toBool();
    prefs.rxAudioProc.nrEnabled        = settings->value("RxProcNrEnabled",         false).toBool();
    prefs.rxAudioProc.nrMode           = static_cast<RxNrMode>(settings->value("RxProcNrMode", 0).toInt());
    prefs.rxAudioProc.channelSelect    = settings->value("RxProcChannelSelect",     0).toInt();
    prefs.rxAudioProc.speexSuppression = settings->value("RxProcSpeexSuppression", -30).toInt();
    prefs.rxAudioProc.speexBandsPreset = settings->value("RxProcSpeexBandsPreset",   3).toInt();
    prefs.rxAudioProc.speexFrameMs     = settings->value("RxProcSpeexFrameMs",      20).toInt();
    prefs.rxAudioProc.speexAgc         = settings->value("RxProcSpeexAgc",         false).toBool();
    prefs.rxAudioProc.speexAgcLevel    = settings->value("RxProcSpeexAgcLevel",  8000.0f).toFloat();
    prefs.rxAudioProc.speexAgcMaxGain  = settings->value("RxProcSpeexAgcMaxGain",    30).toInt();
    prefs.rxAudioProc.speexVad         = settings->value("RxProcSpeexVad",         false).toBool();
    prefs.rxAudioProc.speexVadProbStart= settings->value("RxProcSpeexVadProbStart",   85).toInt();
    prefs.rxAudioProc.speexVadProbCont = settings->value("RxProcSpeexVadProbCont",    65).toInt();
    prefs.rxAudioProc.speexSnrDecay        = settings->value("RxProcSpeexSnrDecay",        0.7f).toFloat();
    prefs.rxAudioProc.speexNoiseUpdateRate = settings->value("RxProcSpeexNoiseUpdateRate", 0.03f).toFloat();
    prefs.rxAudioProc.speexPriorBase       = settings->value("RxProcSpeexPriorBase",       0.1f).toFloat();
    prefs.rxAudioProc.anrNoiseReductionDb = settings->value("RxProcAnrNoiseReductionDb", 20.0).toDouble();
    prefs.rxAudioProc.anrSensitivity      = settings->value("RxProcAnrSensitivity",       1.1).toDouble();
    prefs.rxAudioProc.anrFreqSmoothing    = settings->value("RxProcAnrFreqSmoothing",       4).toInt();
    prefs.rxAudioProc.eqEnabled         = settings->value("RxProcEqEnabled",          false).toBool();
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        prefs.rxAudioProc.eqGain[i] = settings->value(QString("RxProcEqGain%1").arg(i), 0.0f).toFloat();
        prefs.rxAudioProc.eqFreq[i] = settings->value(QString("RxProcEqFreq%1").arg(i),
            (i == 0 ? 100.0f : i == 1 ? 800.0f : i == 2 ? 2000.0f : 3500.0f)).toFloat();
        prefs.rxAudioProc.eqQ[i]    = settings->value(QString("RxProcEqQ%1").arg(i), 1.0f).toFloat();
    }
    prefs.rxAudioProc.outputGainDB      = settings->value("RxProcOutputGainDB",      0.0f).toFloat();
    prefs.rxAudioProc.spectrumEnabled       = settings->value("RxProcSpectrumEnabled",       false).toBool();
    prefs.rxAudioProc.spectrumFPS           = settings->value("RxProcSpectrumFps",           10).toInt();
    prefs.rxAudioProc.specInhibitDuringTx   = settings->value("RxProcSpecInhibitDuringTx",   true).toBool();

    if (prefs.tciPort > 0 && tci == Q_NULLPTR) {

        tci = new tciServer();

        tciThread = new QThread(this);
        tciThread->setObjectName("TCIServer()");
        tci->moveToThread(tciThread);
        connect(queue,SIGNAL(rigCapsUpdated(rigCapabilities*)),tci, SLOT(receiveRigCaps(rigCapabilities*)));
        connect(this,SIGNAL(tciInit(quint16)),tci, SLOT(init(quint16)));
        connect(tciThread, SIGNAL(finished()), tci, SLOT(deleteLater()));
        tciThread->start(QThread::TimeCriticalPriority);
        emit tciInit(prefs.tciPort);
    }

    if (prefs.audioSystem == tciAudio)
    {
        prefs.rxSetup.tci = tci;
        prefs.txSetup.tci = tci;
    }

    udpPrefs.connectionType = settings->value("ConnectionType", udpDefPrefs.connectionType).value<connectionType_t>();
    udpPrefs.clientName = settings->value("ClientName", udpDefPrefs.clientName).toString();

    udpPrefs.halfDuplex = settings->value("HalfDuplex", udpDefPrefs.halfDuplex).toBool();

    settings->endGroup();

    settings->beginGroup("Server");
    setupui->acceptServerConfig(&serverConfig);

    serverConfig.enabled = settings->value("ServerEnabled", false).toBool();
    serverConfig.disableUI = settings->value("DisableUI", false).toBool();
    // These defPrefs are actually for the client, but they are the same.
    serverConfig.controlPort = settings->value("ServerControlPort", udpDefPrefs.controlLANPort).toInt();
    serverConfig.civPort = settings->value("ServerCivPort", udpDefPrefs.serialLANPort).toInt();
    serverConfig.audioPort = settings->value("ServerAudioPort", udpDefPrefs.audioLANPort).toInt();

    serverConfig.users.clear();

    int numUsers = settings->beginReadArray("Users");
    if (numUsers > 0) {
        {
            for (int f = 0; f < numUsers; f++)
            {
                settings->setArrayIndex(f);
                SERVERUSER user;
                user.username = settings->value("Username", "").toString();
                user.password = settings->value("Password", "").toString();
                user.userType = settings->value("UserType", 0).toInt();
                serverConfig.users.append(user);
            }
        }
        settings->endArray();
    }
    else {
        /* Support old way of storing users*/
        settings->endArray();
        numUsers = settings->value("ServerNumUsers", 2).toInt();
        for (int f = 0; f < numUsers; f++)
        {
            SERVERUSER user;
            user.username = settings->value("ServerUsername_" + QString::number(f), "").toString();
            user.password = settings->value("ServerPassword_" + QString::number(f), "").toString();
            user.userType = settings->value("ServerUserType_" + QString::number(f), 0).toInt();
            serverConfig.users.append(user);
        }
    }

    RIGCONFIG* rigTemp = new RIGCONFIG();
    rigTemp->rxAudioSetup.isinput = true;
    rigTemp->txAudioSetup.isinput = false;
    rigTemp->rxAudioSetup.localAFgain = 255;
    rigTemp->txAudioSetup.localAFgain = 255;
    rigTemp->rxAudioSetup.resampleQuality = 4;
    rigTemp->txAudioSetup.resampleQuality = 4;
    rigTemp->rxAudioSetup.type = prefs.audioSystem;
    rigTemp->txAudioSetup.type = prefs.audioSystem;
    rigTemp->pttType = prefs.pttType; // Use the global PTT type.

    rigTemp->baudRate = prefs.serialPortBaud;
    rigTemp->civAddr = 0;
    rigTemp->serialPort = prefs.serialPortRadio;
    QString guid = settings->value("GUID", "").toString();
    if (guid.isEmpty()) {
        guid = QUuid::createUuid().toString();
        settings->setValue("GUID", guid);
    }
#if (QT_VERSION >= QT_VERSION_CHECK(5,10,0))
    memcpy(rigTemp->guid, QUuid::fromString(guid).toRfc4122().constData(), GUIDLEN);
#endif

    rigTemp->rxAudioSetup.name = settings->value("ServerAudioInput", "").toString();
    rigTemp->txAudioSetup.name = settings->value("ServerAudioOutput", "").toString();

    serverConfig.rigs.append(rigTemp);

    // At this point, the users list has exactly one empty user.
    //setupui->updateServerConfig(s_users);

    settings->endGroup();

    // Memory channels

    settings->beginGroup("Memory");
    int size = settings->beginReadArray("Channel");
    int chan = 0;
    double freq;
    quint8 mode;
    bool isSet;

    // Annoying: QSettings will write the array to the
    // preference file starting the array at 1 and ending at 100.
    // Thus, we are writing the channel number each time.
    // It is also annoying that they get written with their array
    // numbers in alphabetical order without zero padding.
    // Also annoying that the preference groups are not written in
    // the order they are specified here.

    for (int i = 0; i < size; i++)
    {
        settings->setArrayIndex(i);
        chan = settings->value("chan", 0).toInt();
        freq = settings->value("freq", 12.345).toDouble();
        mode = settings->value("mode", 0).toInt();
        isSet = settings->value("isSet", false).toBool();

        if (isSet)
        {
            mem.setPreset(chan, freq, (rigMode_t)mode);
        }
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");

    prefs.clusterUdpEnable = settings->value("UdpEnabled", defPrefs.clusterUdpEnable).toBool();
    prefs.clusterTcpEnable = settings->value("TcpEnabled", defPrefs.clusterTcpEnable).toBool();
    prefs.clusterUdpPort = settings->value("UdpPort", defPrefs.clusterUdpPort).toInt();

    int numClusters = settings->beginReadArray("Servers");
    prefs.clusters.clear();
    if (numClusters > 0) {
        {
            for (int f = 0; f < numClusters; f++)
            {
                settings->setArrayIndex(f);
                clusterSettings c;
                c.server = settings->value("ServerName", "").toString();
                c.port = settings->value("Port", 7300).toInt();
                c.userName = settings->value("UserName", "").toString();
                c.password = settings->value("Password", "").toString();
                c.timeout = settings->value("Timeout", 0).toInt();
                c.isdefault = settings->value("Default", false).toBool();
                if (!c.server.isEmpty()) {
                    prefs.clusters.append(c);
                }
            }
        }
    }
    settings->endArray();
    settings->endGroup();

    // CW Memory Load:
    settings->beginGroup("Keyer");
    prefs.cwCutNumbers=settings->value("CutNumbers", defPrefs.cwCutNumbers).toBool();
    prefs.cwSendImmediate=settings->value("SendImmediate", defPrefs.cwSendImmediate).toBool();
    prefs.cwSidetoneEnabled=settings->value("SidetoneEnabled", defPrefs.cwSidetoneEnabled).toBool();
    prefs.cwSidetoneLevel=settings->value("SidetoneLevel", defPrefs.cwSidetoneLevel).toInt();

    int numMemories = settings->beginReadArray("macros");
    if(numMemories==10)
    {
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            prefs.cwMacroList << settings->value("macroText", "").toString();
        }
    }
    settings->endArray();
    settings->endGroup();

#if defined (USB_CONTROLLER)
    settings->beginGroup("USB");
    /* Load USB buttons*/
    prefs.enableUSBControllers = settings->value("EnableUSBControllers", defPrefs.enableUSBControllers).toBool();

    /*Ensure that no operations on the usb commands/buttons/knobs take place*/
    QMutexLocker locker(&usbMutex);

    int numControllers = settings->beginReadArray("Controllers");
    if (numControllers == 0) {
        settings->endArray();
    }
    else {
        usbDevices.clear();
        for (int nc = 0; nc < numControllers; nc++)
        {
            settings->setArrayIndex(nc);
            USBDEVICE tempPrefs;
            tempPrefs.path = settings->value("Path", "").toString();
            tempPrefs.disabled = settings->value("Disabled", false).toBool();
            tempPrefs.sensitivity = settings->value("Sensitivity", 1).toInt();
            tempPrefs.pages = settings->value("Pages", 1).toInt();
            tempPrefs.brightness = (quint8)settings->value("Brightness", 2).toInt();
            tempPrefs.orientation = (quint8)settings->value("Orientation", 2).toInt();
            tempPrefs.speed = (quint8)settings->value("Speed", 2).toInt();
            tempPrefs.timeout = (quint8)settings->value("Timeout", 30).toInt();
            tempPrefs.color = colorFromString(settings->value("Color", QColor(Qt::white).name(QColor::HexArgb)).toString());
            tempPrefs.lcd = (funcs)settings->value("LCD",0).toInt();

            if (!tempPrefs.path.isEmpty()) {
                usbDevices.insert(tempPrefs.path,tempPrefs);
            }
        }
        settings->endArray();
    }

    int numButtons = settings->beginReadArray("Buttons");
    if (numButtons == 0) {
        settings->endArray();
    }
    else {
        usbButtons.clear();
        for (int nb = 0; nb < numButtons; nb++)
        {
            settings->setArrayIndex(nb);
            BUTTON butt;
            butt.path = settings->value("Path", "").toString();
            butt.page = settings->value("Page", 1).toInt();
            auto it = usbDevices.find(butt.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating button, aborting!";
                continue;
            }
            butt.parent = &it.value();
            butt.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            butt.num = settings->value("Num", 0).toInt();
            butt.name = settings->value("Name", "").toString();
            butt.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            butt.textColour = colorFromString(settings->value("Colour", QColor(Qt::white).name(QColor::HexArgb)).toString());
            butt.backgroundOn = colorFromString(settings->value("BackgroundOn", QColor(Qt::lightGray).name(QColor::HexArgb)).toString());
            butt.backgroundOff = colorFromString(settings->value("BackgroundOff", QColor(Qt::blue).name(QColor::HexArgb)).toString());
            butt.toggle = settings->value("Toggle", false).toBool();
            // PET add Linux as it stops Qt6 building FIXME
#if (QT_VERSION > QT_VERSION_CHECK(6,0,0) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS))
            if (settings->value("Icon",NULL) != NULL) {
                butt.icon = new QImage(settings->value("Icon",NULL).value<QImage>());
                butt.iconName = settings->value("IconName", "").toString();
            }
#endif
            butt.on = settings->value("OnCommand", "None").toString();
            butt.off = settings->value("OffCommand", "None").toString();
            butt.graphics = settings->value("Graphics", false).toBool();
            butt.led = settings->value("Led", 0).toInt();
            if (!butt.path.isEmpty())
                usbButtons.append(butt);
        }
        settings->endArray();
    }

    int numKnobs = settings->beginReadArray("Knobs");
    if (numKnobs == 0) {
        settings->endArray();
    }
    else {
        usbKnobs.clear();
        for (int nk = 0; nk < numKnobs; nk++)
        {
            settings->setArrayIndex(nk);
            KNOB kb;
            kb.path = settings->value("Path", "").toString();
            auto it = usbDevices.find(kb.path);
            if (it==usbDevices.end())
            {
                qWarning(logUsbControl) << "Cannot find existing device while creating knob, aborting!";
                continue;
            }
            kb.parent = &it.value();
            kb.page = settings->value("Page", 1).toInt();
            kb.dev = (usbDeviceType)settings->value("Dev", 0).toInt();
            kb.num = settings->value("Num", 0).toInt();
            kb.name = settings->value("Name", "").toString();
            kb.pos = QRect(settings->value("Left", 0).toInt(),
                settings->value("Top", 0).toInt(),
                settings->value("Width", 0).toInt(),
                settings->value("Height", 0).toInt());
            kb.textColour = QColor((settings->value("Colour", "Green").toString()));

            kb.cmd = settings->value("Command", "None").toString();
            if (!kb.path.isEmpty())
                usbKnobs.append(kb);
        }
        settings->endArray();
    }

    settings->endGroup();

    if (prefs.enableUSBControllers) {
        // Setup USB Controller
        setupUsbControllerDevice();
        emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
    }


#endif

    setupui->acceptPreferencesPtr(&prefs);
    setupui->acceptColorPresetPtr(colorPreset);
    setupui->acceptUdpPreferencesPtr(&udpPrefs);

    prefs.settingsChanged = false;
}

void wfmain::extChangedIfPrefs(quint64 items)
{
    prefIfItem pif;
    if(items & (int)if_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)if_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating If pref in wfmain" << (int)i;
            pif = (prefIfItem)i;
            extChangedIfPref(pif);
        }
    }
}

void wfmain::extChangedColPrefs(quint64 items)
{
    prefColItem col;
    if(items & (int)col_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)col_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Color pref in wfmain" << (int)i;
            col = (prefColItem)i;
            extChangedColPref(col);
        }
    }
}

void wfmain::extChangedRaPrefs(quint64 items)
{
    prefRaItem pra;
    if(items & (int)ra_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ra_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Ra pref in wfmain" << (int)i;
            pra = (prefRaItem)i;
            extChangedRaPref(pra);
        }
    }
}

void wfmain::extChangedRsPrefs(quint64 items)
{
    prefRsItem prs;
    if(items & (int)rs_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)rs_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Rs pref in wfmain" << (int)i;
            prs = (prefRsItem)i;
            extChangedRsPref(prs);
        }
    }
}

void wfmain::extChangedCtPrefs(quint64 items)
{
    prefCtItem pct;
    if(items & (int)ct_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)ct_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Ct pref in wfmain" << (int)i;
            pct = (prefCtItem)i;
            extChangedCtPref(pct);
        }
    }
}

void wfmain::extChangedLanPrefs(quint64 items)
{
    prefLanItem plan;
    if(items & (int)l_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)l_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Lan pref in wfmain" << (int)i;
            plan = (prefLanItem)i;
            extChangedLanPref(plan);
        }
    }
}

void wfmain::extChangedClusterPrefs(quint64 items)
{
    prefClusterItem pcl;
    if(items & (int)cl_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)cl_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Cluster pref in wfmain" << (int)i;
            pcl = (prefClusterItem)i;
            extChangedClusterPref(pcl);
        }
    }
}

void wfmain::extChangedUdpPrefs(quint64 items)
{
    prefUDPItem upi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)u_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating UDP preference in wfmain:" << i;
            upi = (prefUDPItem)i;
            extChangedUdpPref(upi);
        }
    }
}


void wfmain::extChangedServerPrefs(quint64 items)
{
    prefServerItem svi;
    if(items & (int)u_all)
    {
        items = 0xffffffff;
    }
    for(int i=1; i < (int)u_all; i = i << 1)
    {
        if(items & i)
        {
            qDebug(logSystem()) << "Updating Server preference in wfmain:" << i;
            svi = (prefServerItem)i;
            extChangedServerPref(svi);
        }
    }
}

void wfmain::extChangedIfPref(prefIfItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case if_useFullScreen:
        changeFullScreenMode(prefs.useFullScreen);
        break;
    case if_useSystemTheme:
        useSystemTheme(prefs.useSystemTheme);
        break;
    case if_drawPeaks:
        // depreciated;
        break;
    case if_underlayMode:
        for (const auto& receiver: receivers) {
            receiver->setUnderlayMode(prefs.underlayMode);
        }
        break;
    case if_underlayBufferSize:
        for (const auto& receiver: receivers) {
            receiver->resizePlasmaBuffer(prefs.underlayBufferSize);
        }
        break;
    case if_wfAntiAlias:
        for (const auto& receiver: receivers) {
            receiver->wfAntiAliased(prefs.wfAntiAlias);
        }
        if (audioProcWin)   audioProcWin->setAntiAlias(prefs.wfAntiAlias);
        if (rxAudioProcWin) rxAudioProcWin->setAntiAlias(prefs.wfAntiAlias);
        break;
    case if_wfInterpolate:
        for (const auto& receiver: receivers) {
            receiver->wfInterpolate(prefs.wfInterpolate);
        }
        break;
    case if_wftheme:
        // Not in settings widget
        break;
    case if_plotFloor:
        // Not in settings widget
        break;
    case if_plotCeiling:
        // Not in settings widget
        break;
    case if_stylesheetPath:
        // Not in settings widget
        break;
    case if_wflength:
        // Not in settings widget
        break;
    case if_confirmExit:
        // Not in settings widget
        break;
    case if_confirmPowerOff:
        // Not in settings widget
        break;
    case if_meter1Type:
        changeMeterType(prefs.meter1Type, 1);
        break;
    case if_meter2Type:
        changeMeterType(prefs.meter2Type, 2);
        break;
    case if_meter3Type:
        changeMeterType(prefs.meter3Type, 3);
        break;
    case if_clickDragTuningEnable:
        // There's nothing to do here since the code
        // already uses the preference variable as state.
        break;
    case if_compMeterReverse:
        if (ui->meter2Widget->getMeterType() == meterComp)
            ui->meter2Widget->setCompReverse(prefs.compMeterReverse);
        if (ui->meter3Widget->getMeterType() == meterComp)
            ui->meter3Widget->setCompReverse(prefs.compMeterReverse);
        break;
    case if_rigCreatorEnable:
        ui->rigCreatorBtn->setEnabled(prefs.rigCreatorEnable);
        break;
    case if_currentColorPresetNumber:
    {
        colorPrefsType p = colorPreset[prefs.currentColorPresetNumber];
        useColorPreset(&p);
        // TODO.....
        break;
    }
    case if_frequencyUnits:
        for (const auto& receiver: receivers)
        {
            receiver->setUnit((FctlUnit)prefs.frequencyUnits);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case if_region:        
        bandbtns->setRegion(prefs.region);
        // Allow to fallthrough so that band indicators are updated correctly on region change.
    case if_showBands:
        for (const auto& receiver: receivers)
        {
            receiver->setBandIndicators(prefs.showBands, prefs.region, &rigCaps->bands);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case if_separators:
        for (const auto& receiver: receivers)
        {
            receiver->setSeparators(prefs.groupSeparator,prefs.decimalSeparator);
        }
        break;
    default:
        qWarning(logSystem()) << "Did not understand if pref update in wfmain for item " << (int)i;
        break;
    }
}

void wfmain::extChangedColPref(prefColItem i)
{
    prefs.settingsChanged = true;
    colorPrefsType* cp = &colorPreset[prefs.currentColorPresetNumber];

    // These are all updated by the widget itself
    switch(i)
    {

#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

    // Any updated scope colors will cause the scope colorPreset to be changed.
    case col_buttonOff:
    case col_buttonOn:
        ui->scopeDualBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        ui->dualWatchBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        ui->splitBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                        .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
        //    ui->mainSubTrackingBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
        //                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    case col_grid:
    case col_axis:
    case col_text:
    case col_plotBackground:
    case col_spectrumLine:
    case col_spectrumFill:
    case col_useSpectrumFillGradient:
    case col_spectrumFillTop:
    case col_spectrumFillBot:
    case col_underlayLine:
    case col_underlayFill:
    case col_underlayFillTop:
    case col_underlayFillBot:
    case col_useUnderlayFillGradient:
    case col_tuningLine:
    case col_passband:
    case col_pbtIndicator:
    case col_waterfallBack:
    case col_waterfallGrid:
    case col_waterfallAxis:
    case col_waterfallText:
    case col_clusterSpots:
        for (const auto& receiver: receivers)
        {
            receiver->colorPreset(cp);
        }
        break;
    // Any updated meter colors, will cause the meter to be updated.
    case col_meterLevel:
    case col_meterAverage:
    case col_meterPeakLevel:
    case col_meterHighScale:
    case col_meterScale:
    case col_meterText:
        ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        ui->meter3Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
        break;

#if defined __GNUC__
#pragma GCC diagnostic pop
#endif

    default:
        qWarning(logSystem()) << "Cannot update wfmain col pref" << (int)i;
    }
}

void wfmain::extChangedRaPref(prefRaItem i)
{
    prefs.settingsChanged = true;
    // Radio Access Prefs
    switch(i)
    {
    case ra_radioCIVAddr:
        if(prefs.radioCIVAddr == 0) {
            showStatusBarText("Setting radio CI-V address to: 'auto'. Make sure CI-V Transceive is enabled on the radio.");
        } else {
            showStatusBarText(QString("Setting radio CI-V address to: 0x%1. Press Save Settings to retain.").arg(prefs.radioCIVAddr, 2, 16));
        }
        break;
    case ra_CIVisRadioModel:
        break;
    case ra_pttType:
        // Not used as cannot be connected to radio when this is updated.
        break;
    case ra_polling_ms:
        if(prefs.polling_ms == 0)
        {
            // Automatic
            qInfo(logSystem()) << "User set radio polling interval to automatic.";
            calculateTimingParameters();
        } else {
            // Manual
            changePollTiming(prefs.polling_ms);
        }
        break;
    case ra_serialPortRadio:
    {
        prefs.serialPortRadio = prefs.serialPortRadio;
        showStatusBarText(QString("Changed serial port to %1. Press Save Settings to retain.").arg(prefs.serialPortRadio));
        break;
    }
    case ra_serialPortBaud:
        prefs.serialPortBaud = prefs.serialPortBaud;
        serverConfig.baudRate = prefs.serialPortBaud;
        showStatusBarText(QString("Changed baud rate to %1 bps. Press Save Settings to retain.").arg(prefs.serialPortBaud));
        break;
    case ra_virtualSerialPort:
        break;
    case ra_localAFgain:
        // Not handled here.
        break;
    case ra_audioSystem:
        // Not handled here
        break;
    case ra_manufacturer:
        setManufacturer(prefs.manufacturer);
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain ra pref" << (int)i;
    }
}

void wfmain::setManufacturer(manufacturersType_t man)
{

    this->rigList.clear();
    qInfo() << "Searching for radios with Manufacturer =" << man;

#ifndef Q_OS_LINUX
#ifdef Q_OS_MACOS
    QString systemRigLocation = QCoreApplication::applicationDirPath() + "/../Resources/rigs";
#else
    QString systemRigLocation = QCoreApplication::applicationDirPath() + "/rigs";
#endif
#else
    // When running as an AppImage the runtime sets APPDIR.  In that case the
    // compile-time PREFIX (/usr) is meaningless; use the XDG user-data
    // location (~/.local/share/wfview/rigs) where the AppImage installer
    // places the rig files.
    QString systemRigLocation = qEnvironmentVariable("APPDIR").isEmpty()
        ? QString(PREFIX "/share/wfview/rigs")
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/wfview/rigs";
#endif

    QDir systemRigDir(systemRigLocation);

    if (!systemRigDir.exists()) {
        qWarning() << "********* Rig directory " << systemRigLocation << "does not exist ********";
    } else {
        qDebug(logRig()) << "Loaded rigs from directory " << systemRigLocation;
        QStringList rigs = systemRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString &rig: rigs) {
            QSettings* rigSettings = new QSettings(systemRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");
            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
            if (manuf == man) {
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = systemRigDir.absoluteFilePath(rig);

                qDebug() << QString("Found Rig %0 with CI-V address of 0x%1 and version %2").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2);
                // Any user modified rig files will override system provided ones.
                this->rigList.insert(civ,rigInfo(civ,model,path,ver));
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

    QString userRigLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+"/rigs";
    QDir userRigDir(userRigLocation);
    if (userRigDir.exists()){
        QStringList rigs = userRigDir.entryList(QStringList() << "*.rig" << "*.RIG", QDir::Files);
        for (QString& rig: rigs) {
            QSettings* rigSettings = new QSettings(userRigDir.absoluteFilePath(rig), QSettings::Format::IniFormat);

#if (QT_VERSION < QT_VERSION_CHECK(6,0,0))
            rigSettings->setIniCodec("UTF-8");
#endif

            if (!rigSettings->childGroups().contains("Rig"))
            {
                qWarning() << rig << "Does not seem to be a rig description file";
                delete rigSettings;
                continue;
            }

            float ver = rigSettings->value("Version","0.0").toString().toFloat();

            rigSettings->beginGroup("Rig");

            manufacturersType_t manuf = rigSettings->value("Manufacturer",manufIcom).value<manufacturersType_t>();
            if (manuf == man) {
                quint16 civ = rigSettings->value("CIVAddress",0).toInt();
                QString model = rigSettings->value("Model","").toString();
                QString path = userRigDir.absoluteFilePath(rig);

                auto it = this->rigList.find(civ);

                if (it != this->rigList.end())
                {
                    if (ver >= it.value().version) {
                        qInfo() << QString("Found User Rig %0 with CI-V address of 0x%1 and newer or same version than system one (%2>=%3)").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2).arg(it.value().version,0,'f',2);
                        this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                    }
                } else {
                    qInfo() << QString("Found New User Rig %0 with CI-V address of 0x%1 version %2").arg(model).arg(civ,4,16,QChar('0')).arg(ver,0,'f',2);
                    this->rigList.insert(civ,rigInfo(civ,model,path,ver));
                }
                // Any user modified rig files will override system provided ones.
            }
            rigSettings->endGroup();
            delete rigSettings;
        }
    }

}

void wfmain::extChangedRsPref(prefRsItem i)
{
    prefs.settingsChanged = true;
    // Radio Settings prefs
    switch(i)
    {
    case rs_dataOffMod:
        queue->add(priorityImmediate,queueItem(funcDATAOffMod,QVariant::fromValue<rigInput>(prefs.inputSource[0]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[0].type).cmd,true,currentReceiver));
        break;
    case rs_data1Mod:
        queue->add(priorityImmediate,queueItem(funcDATA1Mod,QVariant::fromValue<rigInput>(prefs.inputSource[1]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[1].type).cmd,true,currentReceiver));
        break;
    case rs_data2Mod:
        queue->add(priorityImmediate,queueItem(funcDATA2Mod,QVariant::fromValue<rigInput>(prefs.inputSource[2]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[2].type).cmd,true,currentReceiver));
        break;
    case rs_data3Mod:
        queue->add(priorityImmediate,queueItem(funcDATA3Mod,QVariant::fromValue<rigInput>(prefs.inputSource[3]),false,currentReceiver));
        queue->addUnique(priorityHigh,queueItem(getInputTypeCommand(prefs.inputSource[3].type).cmd,true,currentReceiver));
        break;
    case rs_setClock:
        setRadioTimeDatePrep();
        break;
    case rs_pttOn:
        if(!prefs.enablePTT)
        {
            showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
            return;
        }
        showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false,uchar(0)));
        pttTimer->start();
        break;
    case rs_pttOff:
        showStatusBarText("Sending PTT OFF command");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));
        pttTimer->stop();
        break;
    case rs_satOps:
        sat->show();
        break;
    case rs_adjRef:
        cal->show();
        break;
    case rs_clockUseUtc:
    case rs_setRadioTime:
        break;
    default:
        qWarning(logSystem()) << "Cannot update wfmain rs pref" << (int)i;
    }
}


void wfmain::extChangedCtPref(prefCtItem i)
{

    prefs.settingsChanged = true;
    switch(i)
    {
    case ct_enablePTT:
        break;
    case ct_niceTS:
        for (const auto& receiver: receivers)
        {
            receiver->setTuningFloorZeros(prefs.niceTS);
        }

        break;
    case ct_automaticSidebandSwitching:
        finputbtns->setAutomaticSidebandSwitching(prefs.automaticSidebandSwitching);
        break;
    case ct_enableUSBControllers:
#if defined (USB_CONTROLLER)
        if (usbControllerThread != Q_NULLPTR) {
            usbControllerThread->quit();
            usbControllerThread->wait();
            usbControllerThread = Q_NULLPTR;
            usbWindow = Q_NULLPTR;
        }

        if (prefs.enableUSBControllers) {
            // Setup USB Controller
            setupUsbControllerDevice();
            emit initUsbController(&usbMutex,&usbDevices,&usbButtons,&usbKnobs);
        }
#endif
        break;
    case ct_USBControllersSetup:
        if (usbWindow != Q_NULLPTR) {
            if (usbWindow->isVisible()) {
                usbWindow->hide();
            }
            else {
                qInfo(logUsbControl()) << "Showing USB Controller window";
                usbWindow->show();
                usbWindow->raise();
            }
        }
        break;
    case ct_USBControllersReset:
    {
        int ret = QMessageBox::warning(this, tr("wfview"),
                                       tr("Are you sure you wish to reset the USB controllers?"),
                                       QMessageBox::Ok | QMessageBox::Cancel,
                                       QMessageBox::Cancel);
        if (ret == QMessageBox::Ok) {
            qInfo(logUsbControl()) << "Resetting USB controllers to default values";
#if defined(USB_CONTROLLER)
            usbButtons.clear();
            usbKnobs.clear();
            usbDevices.clear();
#endif
            if (prefs.enableUSBControllers)
                extChangedCtPref(ct_enableUSBControllers);
        }
        break;
    }
    default:
        qWarning(logGui()) << "No UI element matches setting" << (int)i;
        break;
    }
}

void wfmain::extChangedLanPref(prefLanItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case l_enableLAN:
        ui->connectBtn->setEnabled(true); // always set, not sure why.
        break;
    case l_enableRigCtlD:
        if (connStatus == connConnected)
            enableRigCtl(prefs.enableRigCtlD);
        break;
    case l_rigCtlPort:
        // no action
        break;
    case l_tcpPort:
        // no action
        break;
    case l_waterfallFormat:
        // no action
        break;
    default:
        qWarning(logSystem()) << "Did not find matching preference in wfmain for LAN ui update:" << (int)i;
    }
}

void wfmain::extChangedClusterPref(prefClusterItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case cl_clusterUdpEnable:
        emit setClusterEnableUdp(prefs.clusterUdpEnable);
        break;
    case cl_clusterTcpEnable:
        emit setClusterEnableTcp(prefs.clusterTcpEnable);
        break;
    case cl_clusterUdpPort:
        emit setClusterUdpPort(prefs.clusterUdpPort);
        break;
    case cl_clusterTcpServerName:
        emit setClusterServerName(prefs.clusterTcpServerName);
        break;
    case cl_clusterTcpUserName:
        emit setClusterUserName(prefs.clusterTcpUserName);
        break;
    case cl_clusterTcpPassword:
        emit setClusterPassword(prefs.clusterTcpPassword);
        break;
    case cl_clusterTcpPort:
        emit setClusterTcpPort(prefs.clusterTcpPort);
        break;
    case cl_clusterTimeout:
        // Used?
        emit setClusterTimeout(prefs.clusterTimeout);
        break;
    case cl_clusterSkimmerSpotsEnable:
        emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        break;
    case cl_clusterTcpConnect:
        emit setClusterEnableTcp(false);
        emit setClusterServerName(prefs.clusterTcpServerName);
        emit setClusterUserName(prefs.clusterTcpUserName);
        emit setClusterPassword(prefs.clusterTcpPassword);
        emit setClusterTcpPort(prefs.clusterTcpPort);
        emit setClusterTimeout(prefs.clusterTimeout);
        emit setClusterSkimmerSpots(prefs.clusterSkimmerSpotsEnable);
        emit setClusterEnableTcp(true);
        break;
    case cl_clusterTcpDisconnect:
        emit setClusterEnableTcp(false);
        break;
    default:
        qWarning(logSystem()) << "Did not find matching preference element in wfmain for cluster preference " << (int)i;
        break;
    }
}

void wfmain::extChangedUdpPref(prefUDPItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case u_ipAddress:
        break;
    case u_controlLANPort:
        break;
    case u_serialLANPort:
        // Not used in the UI.
        break;
    case u_audioLANPort:
        // Not used in the UI.
        break;
    case u_scopeLANPort:
        // Not used in the UI.
        break;
    case u_username:
        break;
    case u_password:
        break;
    case u_clientName:
        // Not used in the UI.
        break;
    case u_halfDuplex:
        break;
    case u_sampleRate:
        break;
    case u_rxCodec:
        break;
    case u_txCodec:
        break;
    case u_rxLatency:
        // Should be updated immediately
        emit sendChangeLatency(prefs.rxSetup.latency);
        break;
    case u_txLatency:
        break;
    case u_audioInput:
        break;
    case u_audioOutput:
        break;
    case u_connectionType:
        break;
    case u_adminLogin:
        break;
    default:
        qWarning(logGui()) << "Did not find matching pref element in wfmain for UDP pref item " << (int)i;
        break;
    }
}


void wfmain::extChangedServerPref(prefServerItem i)
{
    prefs.settingsChanged = true;
    switch(i)
    {
    case s_enabled:
        setServerToPrefs();
        break;
    case s_disableui:
        break;
    case s_lan:
        break;
    case s_controlPort:
        break;
    case s_civPort:
        break;
    case s_audioPort:
        break;
    case s_audioOutput:
        break;
    case s_audioInput:
        break;
    case s_resampleQuality:
        break;
    case s_baudRate:
        break;
    case s_users:
        //qInfo() << "Server users updated :" << serverConfig.users.count() << "total users";
        break;
    case s_rigs:
        break;
    default:
        qWarning(logGui()) << "Did not find matching pref element in wfmain for Server pref item " << (int)i;
        break;
    }
}

void wfmain::saveSettings()
{
    qInfo(logSystem()) << "Saving settings to " << settings->fileName();
    // Basic things to load:

    prefs.settingsChanged = false;

    QString versionstr = QString(WFVIEW_VERSION);
    QString majorVersion = "-1";
    QString minorVersion = "-1";
    if(versionstr.contains(".") && (versionstr.split(".").length() == 2))
    {
        majorVersion = versionstr.split(".").at(0);
        minorVersion = versionstr.split(".").at(1);
    }

    settings->beginGroup("Program");
    settings->setValue("version", versionstr);
    settings->setValue("majorVersion", int(majorVersion.toInt()));
    settings->setValue("minorVersion", int(minorVersion.toInt()));
    settings->setValue("gitShort", QString(GITSHORT));
    settings->setValue("hasRunSetup", prefs.hasRunSetup);
    settings->endGroup();

    // UI: (full screen, dark theme, draw peaks, colors, etc)
    settings->beginGroup("Interface");
    settings->setValue("UseFullScreen", prefs.useFullScreen);
    settings->setValue("UseSystemTheme", prefs.useSystemTheme);
    settings->setValue("WFEnable", prefs.wfEnable);
    settings->setValue("DrawPeaks", prefs.drawPeaks);
    settings->setValue("underlayMode", prefs.underlayMode);
    settings->setValue("underlayBufferSize", prefs.underlayBufferSize);
    settings->setValue("WFAntiAlias", prefs.wfAntiAlias);
    settings->setValue("WFInterpolate", prefs.wfInterpolate);
    settings->setValue("MainWFTheme", prefs.mainWfTheme);
    settings->setValue("SubWFTheme", prefs.subWfTheme);
    settings->setValue("MainPlotFloor", prefs.mainPlotFloor);
    settings->setValue("SubPlotFloor", prefs.subPlotFloor);
    settings->setValue("MainPlotCeiling", prefs.mainPlotCeiling);
    settings->setValue("SubPlotCeiling", prefs.subPlotCeiling);
    settings->setValue("StylesheetPath", prefs.stylesheetPath);
    //settings->setValue("splitter", ui->splitter->saveState());
    settings->setValue("windowGeometry", saveGeometry());

    if (bandbtns != Q_NULLPTR)
        settings->setValue("BandWindowGeometry", bandbtns->getGeometry());
    if (finputbtns != Q_NULLPTR)
        settings->setValue("FreqWindowGeometry", finputbtns->getGeometry());

    settings->setValue("windowState", saveState());
    settings->setValue("MainWFLength", prefs.mainWflength);
    settings->setValue("SubWFLength", prefs.subWflength);
    settings->setValue("ConfirmExit", prefs.confirmExit);
    settings->setValue("ConfirmPowerOff", prefs.confirmPowerOff);
    settings->setValue("ConfirmSettingsChanged", prefs.confirmSettingsChanged);
    settings->setValue("ConfirmMmories", prefs.confirmMemories);
    settings->setValue("Meter1Type", (int)prefs.meter1Type);
    settings->setValue("Meter2Type", (int)prefs.meter2Type);
    settings->setValue("Meter3Type", (int)prefs.meter3Type);
    settings->setValue("compMeterReverse", prefs.compMeterReverse);
    settings->setValue("ClickDragTuning", prefs.clickDragTuningEnable);

    settings->setValue("UseUTC",prefs.useUTC);
    settings->setValue("SetRadioTime",prefs.setRadioTime);

    settings->setValue("RigCreator",prefs.rigCreatorEnable);
    settings->setValue("FrequencyUnits",prefs.frequencyUnits);
    settings->setValue("Region",prefs.region);
    settings->setValue("ShowBands",prefs.showBands);
    settings->setValue("GroupSeparator",prefs.groupSeparator);
    settings->setValue("DecimalSeparator",prefs.decimalSeparator);
    settings->setValue("ForceVfoMode",prefs.forceVfoMode);
    settings->setValue("AutoPowerOn",prefs.autoPowerOn);

    settings->endGroup();

    // Radio and Comms: C-IV addr, port to use
    settings->beginGroup("Radio");
    settings->setValue("Manufacturer", prefs.manufacturer);

    settings->setValue("RigCIVuInt", prefs.radioCIVAddr);
    settings->setValue("CIVisRadioModel", prefs.CIVisRadioModel);
    settings->setValue("PTTType", prefs.pttType);
    settings->setValue("polling_ms", prefs.polling_ms); // 0 = automatic
    if (!prefs.serialPortRadio.isEmpty())
        settings->setValue("SerialPortRadio", prefs.serialPortRadio);
    if (prefs.serialPortBaud != 0)
        settings->setValue("SerialPortBaud", prefs.serialPortBaud);
    settings->setValue("VirtualSerialPort", prefs.virtualSerialPort);
    settings->setValue("localAFgain", prefs.localAFgain);
    settings->setValue("AudioSystem", prefs.audioSystem);

    settings->endGroup();

    // Misc. user settings (enable PTT, draw peaks, etc)
    settings->beginGroup("Controls");
    settings->setValue("EnablePTT", prefs.enablePTT);
    settings->setValue("NiceTS", prefs.niceTS);
    settings->setValue("automaticSidebandSwitching", prefs.automaticSidebandSwitching);
    settings->endGroup();

    settings->beginGroup("LAN");
    settings->setValue("EnableLAN", prefs.enableLAN);
    settings->setValue("EnableRigCtlD", prefs.enableRigCtlD);
    settings->setValue("TcpServerPort", prefs.tcpPort);
    settings->setValue("RigCtlPort", prefs.rigCtlPort);
    settings->setValue("TCPServerPort", prefs.tcpPort);
    settings->setValue("TCIServerPort", prefs.tciPort);
    settings->setValue("IPAddress", udpPrefs.ipAddress);
    settings->setValue("ControlLANPort", udpPrefs.controlLANPort);
    settings->setValue("SerialLANPort", udpPrefs.serialLANPort);
    settings->setValue("AudioLANPort", udpPrefs.audioLANPort);
    settings->setValue("ScopeLANPort", udpPrefs.scopeLANPort);
    settings->setValue("AdminLogin",udpPrefs.adminLogin);
    settings->setValue("Username", udpPrefs.username);
    settings->setValue("Password", udpPrefs.password);
    settings->setValue("AudioRXLatency", prefs.rxSetup.latency);
    settings->setValue("AudioTXLatency", prefs.txSetup.latency);
    settings->setValue("AudioRXSampleRate", prefs.rxSetup.sampleRate);
    settings->setValue("AudioRXCodec", prefs.rxSetup.codec);
    settings->setValue("AudioTXSampleRate", prefs.txSetup.sampleRate);
    settings->setValue("AudioTXCodec", prefs.txSetup.codec);
    if (!prefs.rxSetup.name.isEmpty())
        settings->setValue("AudioOutput", prefs.rxSetup.name);
    if (!prefs.txSetup.name.isEmpty())
        settings->setValue("AudioInput", prefs.txSetup.name);
    settings->setValue("ResampleQuality", prefs.rxSetup.resampleQuality);
    settings->setValue("ClientName", udpPrefs.clientName);

    // TX audio processing prefs
    settings->setValue("TxProcBypass",        prefs.txAudioProc.bypass);
    settings->setValue("TxProcCompEnabled",   prefs.txAudioProc.compEnabled);
    settings->setValue("TxProcEqEnabled",     prefs.txAudioProc.eqEnabled);
    settings->setValue("TxProcEqFirst",       prefs.txAudioProc.eqFirst);
    settings->setValue("TxProcInputGain",     prefs.txAudioProc.inputGainDB);
    settings->setValue("TxProcOutputGain",    prefs.txAudioProc.outputGainDB);
    settings->setValue("TxProcCompPeak",      prefs.txAudioProc.compPeakLimit);
    settings->setValue("TxProcCompRelease",   prefs.txAudioProc.compRelease);
    settings->setValue("TxProcCompFast",      prefs.txAudioProc.compFastRatio);
    settings->setValue("TxProcCompSlow",      prefs.txAudioProc.compSlowRatio);
    settings->setValue("TxProcSidetone",         prefs.txAudioProc.sidetoneEnabled);
    settings->setValue("TxProcSidetoneLevel",    prefs.txAudioProc.sidetoneLevel);
    settings->setValue("TxProcSpectrumEnabled",     prefs.txAudioProc.spectrumEnabled);
    settings->setValue("TxProcSpectrumFps",         prefs.txAudioProc.spectrumFPS);
    settings->setValue("TxProcSpecInhibitDuringRx", prefs.txAudioProc.specInhibitDuringRx);
    for (int i = 0; i < TxAudioProcessor::EQ_BANDS; ++i)
        settings->setValue(QString("TxProcEqBand%1").arg(i), prefs.txAudioProc.eqBands[i]);
    settings->setValue("TxProcGateEnabled",   prefs.txAudioProc.gateEnabled);
    settings->setValue("TxProcGateThreshold", prefs.txAudioProc.gateThreshold);
    settings->setValue("TxProcGateAttack",    prefs.txAudioProc.gateAttack);
    settings->setValue("TxProcGateHold",      prefs.txAudioProc.gateHold);
    settings->setValue("TxProcGateDecay",     prefs.txAudioProc.gateDecay);
    settings->setValue("TxProcGateRange",     prefs.txAudioProc.gateRange);
    settings->setValue("TxProcGateLfCutoff",  prefs.txAudioProc.gateLfCutoff);
    settings->setValue("TxProcGateHfCutoff",  prefs.txAudioProc.gateHfCutoff);

    // RX audio processing prefs
    settings->setValue("RxProcBypass",           prefs.rxAudioProc.bypass);
    settings->setValue("RxProcNrEnabled",         prefs.rxAudioProc.nrEnabled);
    settings->setValue("RxProcNrMode",            static_cast<int>(prefs.rxAudioProc.nrMode));
    settings->setValue("RxProcChannelSelect",     prefs.rxAudioProc.channelSelect);
    settings->setValue("RxProcSpeexSuppression",  prefs.rxAudioProc.speexSuppression);
    settings->setValue("RxProcSpeexBandsPreset",  prefs.rxAudioProc.speexBandsPreset);
    settings->setValue("RxProcSpeexFrameMs",      prefs.rxAudioProc.speexFrameMs);
    settings->setValue("RxProcSpeexAgc",          prefs.rxAudioProc.speexAgc);
    settings->setValue("RxProcSpeexAgcLevel",     prefs.rxAudioProc.speexAgcLevel);
    settings->setValue("RxProcSpeexAgcMaxGain",   prefs.rxAudioProc.speexAgcMaxGain);
    settings->setValue("RxProcSpeexVad",          prefs.rxAudioProc.speexVad);
    settings->setValue("RxProcSpeexVadProbStart", prefs.rxAudioProc.speexVadProbStart);
    settings->setValue("RxProcSpeexVadProbCont",  prefs.rxAudioProc.speexVadProbCont);
    settings->setValue("RxProcSpeexSnrDecay",        prefs.rxAudioProc.speexSnrDecay);
    settings->setValue("RxProcSpeexNoiseUpdateRate", prefs.rxAudioProc.speexNoiseUpdateRate);
    settings->setValue("RxProcSpeexPriorBase",       prefs.rxAudioProc.speexPriorBase);
    settings->setValue("RxProcAnrNoiseReductionDb", prefs.rxAudioProc.anrNoiseReductionDb);
    settings->setValue("RxProcAnrSensitivity",      prefs.rxAudioProc.anrSensitivity);
    settings->setValue("RxProcAnrFreqSmoothing",    prefs.rxAudioProc.anrFreqSmoothing);
    settings->setValue("RxProcEqEnabled",          prefs.rxAudioProc.eqEnabled);
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        settings->setValue(QString("RxProcEqGain%1").arg(i), prefs.rxAudioProc.eqGain[i]);
        settings->setValue(QString("RxProcEqFreq%1").arg(i), prefs.rxAudioProc.eqFreq[i]);
        settings->setValue(QString("RxProcEqQ%1").arg(i),    prefs.rxAudioProc.eqQ[i]);
    }
    settings->setValue("RxProcOutputGainDB",      prefs.rxAudioProc.outputGainDB);
    settings->setValue("RxProcSpectrumEnabled",       prefs.rxAudioProc.spectrumEnabled);
    settings->setValue("RxProcSpectrumFps",           prefs.rxAudioProc.spectrumFPS);
    settings->setValue("RxProcSpecInhibitDuringTx",   prefs.rxAudioProc.specInhibitDuringTx);

    settings->setValue("WaterfallFormat", prefs.waterfallFormat);
    settings->setValue("HalfDuplex", udpPrefs.halfDuplex);
    settings->setValue("ConnectionType", udpPrefs.connectionType);

    settings->endGroup();

    // Memory channels
    settings->beginGroup("Memory");
    settings->beginWriteArray("Channel", (int)mem.getNumPresets());

    preset_kind temp;
    for (int i = 0; i < (int)mem.getNumPresets(); i++)
    {
        temp = mem.getPreset((int)i);
        settings->setArrayIndex(i);
        settings->setValue("chan", i);
        settings->setValue("freq", temp.frequency);
        settings->setValue("mode", temp.mode);
        settings->setValue("isSet", temp.isSet);
    }

    settings->endArray();
    settings->endGroup();

    // Color presets:
    settings->beginGroup("ColorPresets");
    settings->setValue("currentColorPresetNumber", prefs.currentColorPresetNumber);
    settings->beginWriteArray("ColorPreset", numColorPresetsTotal);
    colorPrefsType *p;
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        p = &(colorPreset[pn]);
        settings->setArrayIndex(pn);
        settings->setValue("presetNum", p->presetNum);
        settings->setValue("presetName", *(p->presetName));
        settings->setValue("gridColor", p->gridColor.name(QColor::HexArgb));
        settings->setValue("axisColor", p->axisColor.name(QColor::HexArgb));
        settings->setValue("textColor", p->textColor.name(QColor::HexArgb));
        settings->setValue("spectrumLine", p->spectrumLine.name(QColor::HexArgb));
        settings->setValue("spectrumFill", p->spectrumFill.name(QColor::HexArgb));
        settings->setValue("useSpectrumFillGradient", p->useSpectrumFillGradient);
        settings->setValue("spectrumFillTop", p->spectrumFillTop.name(QColor::HexArgb));
        settings->setValue("spectrumFillBot", p->spectrumFillBot.name(QColor::HexArgb));
        settings->setValue("underlayLine", p->underlayLine.name(QColor::HexArgb));
        settings->setValue("underlayFill", p->underlayFill.name(QColor::HexArgb));
        settings->setValue("useUnderlayFillGradient", p->useUnderlayFillGradient);
        settings->setValue("underlayFillTop", p->underlayFillTop.name(QColor::HexArgb));
        settings->setValue("underlayFillBot", p->underlayFillBot.name(QColor::HexArgb));
        settings->setValue("plotBackground", p->plotBackground.name(QColor::HexArgb));
        settings->setValue("tuningLine", p->tuningLine.name(QColor::HexArgb));
        settings->setValue("passband", p->passband.name(QColor::HexArgb));
        settings->setValue("pbt", p->pbt.name(QColor::HexArgb));
        settings->setValue("wfBackground", p->wfBackground.name(QColor::HexArgb));
        settings->setValue("wfGrid", p->wfGrid.name(QColor::HexArgb));
        settings->setValue("wfAxis", p->wfAxis.name(QColor::HexArgb));
        settings->setValue("wfText", p->wfText.name(QColor::HexArgb));
        settings->setValue("meterLevel", p->meterLevel.name(QColor::HexArgb));
        settings->setValue("meterAverage", p->meterAverage.name(QColor::HexArgb));
        settings->setValue("meterPeakScale", p->meterPeakScale.name(QColor::HexArgb));
        settings->setValue("meterPeakLevel", p->meterPeakLevel.name(QColor::HexArgb));
        settings->setValue("meterLowerLine", p->meterLowerLine.name(QColor::HexArgb));
        settings->setValue("meterLowText", p->meterLowText.name(QColor::HexArgb));
        settings->setValue("clusterSpots", p->clusterSpots.name(QColor::HexArgb));
        settings->setValue("buttonOff", p->buttonOff.name(QColor::HexArgb));
        settings->setValue("buttonOn", p->buttonOn.name(QColor::HexArgb));
    }
    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Server");

    settings->setValue("ServerEnabled", serverConfig.enabled);
    settings->setValue("DisableUI", serverConfig.disableUI);
    settings->setValue("ServerControlPort", serverConfig.controlPort);
    settings->setValue("ServerCivPort", serverConfig.civPort);
    settings->setValue("ServerAudioPort", serverConfig.audioPort);
    if (!serverConfig.rigs.first()->txAudioSetup.name.isEmpty())
        settings->setValue("ServerAudioOutput", serverConfig.rigs.first()->txAudioSetup.name);
    if (!serverConfig.rigs.first()->rxAudioSetup.name.isEmpty())
        settings->setValue("ServerAudioInput", serverConfig.rigs.first()->rxAudioSetup.name);

    /* Remove old format users*/
    int numUsers = settings->value("ServerNumUsers", 0).toInt();
    if (numUsers > 0) {
        settings->remove("ServerNumUsers");
        for (int f = 0; f < numUsers; f++)
        {
            settings->remove("ServerUsername_" + QString::number(f));
            settings->remove("ServerPassword_" + QString::number(f));
            settings->remove("ServerUserType_" + QString::number(f));
        }
    }

    settings->beginWriteArray("Users");

    for (int f = 0; f < serverConfig.users.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("Username", serverConfig.users[f].username);
        settings->setValue("Password", serverConfig.users[f].password);
        settings->setValue("UserType", serverConfig.users[f].userType);
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Cluster");
    settings->setValue("UdpEnabled", prefs.clusterUdpEnable);
    settings->setValue("TcpEnabled", prefs.clusterTcpEnable);
    settings->setValue("UdpPort", prefs.clusterUdpPort);

    settings->beginWriteArray("Servers");

    for (int f = 0; f < prefs.clusters.count(); f++)
    {
        settings->setArrayIndex(f);
        settings->setValue("ServerName", prefs.clusters[f].server);
        settings->setValue("UserName", prefs.clusters[f].userName);
        settings->setValue("Port", prefs.clusters[f].port);
        settings->setValue("Password", prefs.clusters[f].password);
        settings->setValue("Timeout", prefs.clusters[f].timeout);
        settings->setValue("Default", prefs.clusters[f].isdefault);
        if (prefs.clusters[f].isdefault  == true) {
            prefs.clusterTcpServerName = prefs.clusters[f].server;
            prefs.clusterTcpUserName = prefs.clusters[f].userName;
            prefs.clusterTcpPassword = prefs.clusters[f].password;
            prefs.clusterTcpPort = prefs.clusters[f].port;
            prefs.clusterTimeout = prefs.clusters[f].timeout;
        }
    }

    settings->endArray();
    settings->endGroup();

    settings->beginGroup("Keyer");

    if (cw != Q_NULLPTR) {
        prefs.cwCutNumbers = cw->getCutNumbers();
        prefs.cwSendImmediate = cw->getSendImmediate();
        prefs.cwSidetoneEnabled = cw->getSidetoneEnable();
        prefs.cwSidetoneLevel = cw->getSidetoneLevel();
        prefs.cwMacroList = cw->getMacroText();
    }

    settings->setValue("CutNumbers", prefs.cwCutNumbers);
    settings->setValue("SendImmediate", prefs.cwSendImmediate);
    settings->setValue("SidetoneEnabled", prefs.cwSidetoneEnabled);
    settings->setValue("SidetoneLevel", prefs.cwSidetoneLevel);

    if(prefs.cwMacroList.length() == 10)
    {
        settings->beginWriteArray("macros");
        for(int m=0; m < 10; m++)
        {
            settings->setArrayIndex(m);
            settings->setValue("macroText", prefs.cwMacroList.at(m));
        }
        settings->endArray();
    } else {
        qDebug(logSystem()) << "Error, CW macro list is wrong length: " << prefs.cwMacroList.length();
    }
    settings->endGroup();

#if defined(USB_CONTROLLER)
    settings->beginGroup("USB");

    settings->setValue("EnableUSBControllers", prefs.enableUSBControllers);

    QMutexLocker locker(&usbMutex);

    // Store USB Controller

    settings->remove("Controllers");
    settings->beginWriteArray("Controllers");
    int nc=0;

    for (auto it = usbDevices.begin(); it != usbDevices.end(); it++)
    {
        auto dev = &it.value();
        settings->setArrayIndex(nc);

        settings->setValue("Model", dev->product);
        settings->setValue("Path", dev->path);
        settings->setValue("Disabled", dev->disabled);
        settings->setValue("Sensitivity", dev->sensitivity);
        settings->setValue("Brightness", dev->brightness);
        settings->setValue("Orientation", dev->orientation);
        settings->setValue("Speed", dev->speed);
        settings->setValue("Timeout", dev->timeout);
        settings->setValue("Pages", dev->pages);
        settings->setValue("Color", dev->color.name(QColor::HexArgb));
        settings->setValue("LCD", dev->lcd);

        ++nc;
    }
    settings->endArray();


    settings->remove("Buttons");
    settings->beginWriteArray("Buttons");
    for (int nb = 0; nb < usbButtons.count(); nb++)
    {
        settings->setArrayIndex(nb);
        settings->setValue("Page", usbButtons[nb].page);
        settings->setValue("Dev", usbButtons[nb].dev);
        settings->setValue("Num", usbButtons[nb].num);
        settings->setValue("Path", usbButtons[nb].path);
        settings->setValue("Name", usbButtons[nb].name);
        settings->setValue("Left", usbButtons[nb].pos.left());
        settings->setValue("Top", usbButtons[nb].pos.top());
        settings->setValue("Width", usbButtons[nb].pos.width());
        settings->setValue("Height", usbButtons[nb].pos.height());
        settings->setValue("Colour", usbButtons[nb].textColour.name(QColor::HexArgb));
        settings->setValue("BackgroundOn", usbButtons[nb].backgroundOn.name(QColor::HexArgb));
        settings->setValue("BackgroundOff", usbButtons[nb].backgroundOff.name(QColor::HexArgb));
        if (usbButtons[nb].icon != Q_NULLPTR) {
            settings->setValue("Icon", *usbButtons[nb].icon);
            settings->setValue("IconName", usbButtons[nb].iconName);
        }
        settings->setValue("Toggle", usbButtons[nb].toggle);

        if (usbButtons[nb].onCommand != Q_NULLPTR)
            settings->setValue("OnCommand", usbButtons[nb].onCommand->text);
        if (usbButtons[nb].offCommand != Q_NULLPTR)
            settings->setValue("OffCommand", usbButtons[nb].offCommand->text);
        settings->setValue("Graphics",usbButtons[nb].graphics);
        if (usbButtons[nb].led) {
            settings->setValue("Led", usbButtons[nb].led);
        }
    }

    settings->endArray();

    settings->remove("Knobs");
    settings->beginWriteArray("Knobs");
    for (int nk = 0; nk < usbKnobs.count(); nk++)
    {
        settings->setArrayIndex(nk);
        settings->setValue("Page", usbKnobs[nk].page);
        settings->setValue("Dev", usbKnobs[nk].dev);
        settings->setValue("Num", usbKnobs[nk].num);
        settings->setValue("Path", usbKnobs[nk].path);
        settings->setValue("Left", usbKnobs[nk].pos.left());
        settings->setValue("Top", usbKnobs[nk].pos.top());
        settings->setValue("Width", usbKnobs[nk].pos.width());
        settings->setValue("Height", usbKnobs[nk].pos.height());
        settings->setValue("Colour", usbKnobs[nk].textColour.name());
        if (usbKnobs[nk].command != Q_NULLPTR)
            settings->setValue("Command", usbKnobs[nk].command->text);
    }

    settings->endArray();

    settings->endGroup();
#endif

    settings->sync(); // Automatic, not needed (supposedly)
}

void wfmain::setTuningSteps()
{
    // TODO: interact with preferences, tuning step drop down box, and current operating mode
    // Units are MHz:
    tsPlusControl = 0.010f;
    tsPlus =        0.001f;
    tsPlusShift =   0.0001f;
    tsPage =        1.0f;
    tsPageShift =   0.5f; // TODO, unbind this keystroke from the dial
    tsWfScroll =    0.0001f; // modified by tuning step selector
    tsKnobMHz =     0.0001f; // modified by tuning step selector

    // Units are in Hz:
    tsPlusControlHz =   10000;
    tsPlusHz =           1000;
    tsPlusShiftHz =       100;
    tsPageHz =        1000000;
    tsPageShiftHz =    500000; // TODO, unbind this keystroke from the dial
    tsWfScrollHz =        100; // modified by tuning step selector
    tsKnobHz =            100; // modified by tuning step selector

}

void wfmain::on_tuningStepCombo_currentIndexChanged(int index)
{
    tsWfScroll = (float)ui->tuningStepCombo->itemData(index).toUInt() / 1000000.0;
    tsKnobMHz =  (float)ui->tuningStepCombo->itemData(index).toUInt() / 1000000.0;

    tsWfScrollHz = ui->tuningStepCombo->itemData(index).toUInt();
    tsKnobHz = ui->tuningStepCombo->itemData(index).toUInt();
    for (auto &s: rigCaps->steps) {
        if (tsWfScrollHz == s.hz)
        {
            for (const auto& receiver: receivers)
            {
                receiver->setStepSize(s.hz);
            }
            queue->add(priorityImmediate,queueItem(funcTuningStep,QVariant::fromValue<uchar>(s.num),false));
        }
    }
}

quint64 wfmain::roundFrequency(quint64 frequency, unsigned int tsHz)
{
    quint64 rounded = 0;
    if(prefs.niceTS)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
        return rounded;
    } else {
        return frequency;
    }
}

quint64 wfmain::roundFrequencyWithStep(quint64 frequency, int steps, unsigned int tsHz)
{
    quint64 rounded = 0;

    // Avoid divide by zero (should never happen but an invalid step in the rig file could cause a crash.)
    if (tsHz < 1)
        tsHz=1;

    if(steps > 0)
    {
        frequency = frequency + (quint64)(steps*tsHz);
    } else {
        frequency = frequency - std::min((quint64)(abs(steps)*tsHz), frequency);
    }

    if(prefs.niceTS)
    {
        rounded = ((frequency % tsHz) > tsHz/2) ? frequency + tsHz - frequency%tsHz : frequency - frequency%tsHz;
        return rounded;
    } else {
        return frequency;
    }
}

funcType wfmain::getInputTypeCommand(inputTypes input)
{
    funcs func;
    switch(input)
    {
    case inputMICACCA:
    case inputMICACCB:
    case inputMICACCAACCB:
    case inputMICUSB:
    case inputMic:
        func = funcMicGain;
        break;
    case inputACCAACCB:
    case inputACCA:
    case inputACCUSB:
        func = funcACCAModLevel;
        break;
    case inputACCB:
        func = funcACCBModLevel;
        break;
    case inputUSB:
        func = funcUSBModLevel;
        break;
    case inputLAN:
        func = funcLANModLevel;
        break;
    case inputSPDIF:
        func = funcSPDIFModLevel;
        break;
    default:
        func = funcNone;
        break;
    }
    //qInfo(logSystem()) << "Input type command for" << input << "is" << funcString[func];
    funcType type;
    if(rigCaps) {
        auto f = rigCaps->commands.find(func);
        if (f != rigCaps->commands.end())
        {
            type = f.value();
        }
    } else {
        qWarning(logSystem()) << "Not connected to radio, rigCaps invalid.";
    }

    return type;
}


void wfmain:: getInitialRigState()
{

    // Initial list of queries to the radio.
    // These are made when the program starts up
    // and are used to adjust the UI to match the radio settings
    // the polling interval is set at 200ms. Faster is possible but slower
    // computers will glitch occasionally.

    queue->del(funcTransceiverId); // This command is no longer required

    if (rigCaps->commands.contains(funcAutoInformation) && (!rigCaps->hasSpectrum || prefs.enableLAN)) {
        queue->add(priorityImmediate,queueItem(funcAutoInformation,QVariant::fromValue(uchar(2)),false,0));
        // Enable metering data in the AutoInformation stream:
        queue->add(priorityImmediate,queueItem(funcALCMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcCompMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcIdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcVdMeter,QVariant::fromValue(uchar(1)),false,0));
        queue->add(priorityImmediate,queueItem(funcSWRMeter,QVariant::fromValue(uchar(1)),false,0));
    }

    if (prefs.forceVfoMode) {
        queue->add(priorityImmediate,queueItem(funcSelectVFO,QVariant::fromValue<vfo_t>(vfoA)));
        //if (rigCaps->commands.contains(funcSatelliteMode))
        //    queue->add(priorityImmediate,funcSatelliteMode,false,0); // are we in satellite mode?.

        //if (rigCaps->commands.contains(funcVFOModeSelect))
        //    queue->add(priorityImmediate,funcVFOModeSelect); // Make sure we are in VFO mode.

        //if (rigCaps->commands.contains(funcScopeMainSub))
        //    queue->add(priorityImmediate,queueItem(funcScopeMainSub,QVariant::fromValue(uchar(0)),false,0)); // Set main scope

    }
    //if (rigCaps->commands.contains(funcVFOBandMS))
    //    queue->add(priorityImmediate,queueItem(funcVFOBandMS,QVariant::fromValue(uchar(0)),false,0));

    if(rigCaps->hasSpectrum)
    {
        // Send commands to start scope immediately
        if (receivers.size()>0 && receivers[0]->isScopeEnabled()) {
            queue->add(priorityHigh,queueItem(funcScopeOnOff,QVariant::fromValue(quint8(1)),true));
            queue->add(priorityHigh,queueItem(funcScopeDataOutput,QVariant::fromValue(quint8(1)),false));
        }

        // Find the scope ref limits
        auto mr = rigCaps->commands.find(funcScopeRef);
        if (mr != rigCaps->commands.end())
        {
            for (uchar f = 0; f<receivers.size();f++) {
                receivers[f]->setRefLimits(mr.value().minVal,mr.value().maxVal);
                queue->add(priorityMediumHigh,funcScopeRef,false,f);
            }
        }
    }

    quint64 start=UINT64_MAX;
    quint64 end=0;
    for (auto &band: rigCaps->bands)
    {
        if (band.region == "" || band.region == prefs.region) {
            if (start > band.lowFreq)
                start = band.lowFreq;
            if (end < band.highFreq)
                end = band.highFreq;
        }
    }

    for (const auto& receiver: receivers)
    {
        //qInfo(logSystem()) << "Display Settings start:" << start << "end:" << end;
        receiver->displaySettings(0, start, end, 1,(FctlUnit)prefs.frequencyUnits, &rigCaps->bands);
        receiver->setBandIndicators(prefs.showBands, prefs.region, &rigCaps->bands);
    }

    if (rigCaps->commands.contains(funcRitFreq))
    {
        funcType func = rigCaps->commands.find(funcRitFreq).value();
        ui->ritTuneDial->setRange(func.minVal,func.maxVal);
    }

    if (rigCaps->commands.contains(funcTime) && prefs.setRadioTime) {
        setRadioTimeDatePrep();
    }

    if (cw != Q_NULLPTR)
    {
        cw->receiveEnabled(rigCaps->commands.contains(funcCWDecode));
    }

    if (rigCaps->commands.contains(funcVOIP))
    {
        queue->add(priorityHigh,queueItem(funcVOIP,QVariant::fromValue<uchar>(prefs.rxSetup.codec==0x80?2:1),false,0));
    }
    // IC7610_EXT_INITIAL_READ_BEGIN
    auto ext7610InitialRead = [=](funcs f) {
        auto it = rigCaps->commands.find(f);
        if (it == rigCaps->commands.end() || !it->getCmd)
            return;
        const uchar target = it->cmd29 ? currentReceiver : 0;
        queue->add(priorityImmediate, f, false, target);
    };

    ext7610InitialRead(funcAGC);
    ext7610InitialRead(funcAGCTimeConstant);
    ext7610InitialRead(funcAudioPeakFilter);
    ext7610InitialRead(funcAPFLevel);
    ext7610InitialRead(funcAPFType);
    ext7610InitialRead(funcAPFTypeLevel);
    ext7610InitialRead(funcNRLevel);
    ext7610InitialRead(funcNBLevel);
    ext7610InitialRead(funcNBDepth);
    ext7610InitialRead(funcNBWidth);
    ext7610InitialRead(funcAutoNotch);
    ext7610InitialRead(funcManualNotch);
    ext7610InitialRead(funcNotchFilter);
    ext7610InitialRead(funcManualNotchWidth);
    ext7610InitialRead(funcAFMute);
    ext7610InitialRead(funcDigiSel);
    ext7610InitialRead(funcDigiSelShift);
    ext7610InitialRead(funcTwinPeakFilter);
    ext7610InitialRead(funcDialLock);
    ext7610InitialRead(funcMainSubTracking);
    ext7610InitialRead(funcCompressorLevel);
    ext7610InitialRead(funcDriveGain);
    ext7610InitialRead(funcVoxGain);
    ext7610InitialRead(funcAntiVoxGain);
    ext7610InitialRead(funcVOXDelay);
    ext7610InitialRead(funcVOXVoiceDelay);
    ext7610InitialRead(funcBreakIn);
    ext7610InitialRead(funcBreakInDelay);
    ext7610InitialRead(funcSSBTXBandwidth);
    // IC7610_EXT_INITIAL_READ_END
    // Put Kenwood into Shift&Width mode for filter control.
    queue->add(priorityHigh,queueItem(funcFilterControlSSB,QVariant::fromValue<bool>(true),false,0));
    queue->add(priorityHigh,queueItem(funcFilterControlData,QVariant::fromValue<bool>(true),false,0));

}
void wfmain::showStatusBarText(QString text)
{
    ui->statusBar->showMessage(text, 5000);
}

void wfmain::useSystemTheme(bool checked)
{
    setAppTheme(!checked);
    prefs.useSystemTheme = checked;
}

void wfmain::setAppTheme(bool isCustom)
{
    if(isCustom)
    {
#ifndef Q_OS_LINUX
        QFile f(":"+prefs.stylesheetPath); // built-in resource
#else
        QFile f(PREFIX "/share/wfview/" + prefs.stylesheetPath);
        if (!f.exists())
        {
            // Fallback to built-in resource (AppImage / non-root installs)
            f.setFileName(":" + prefs.stylesheetPath);
        }
#endif
        if (!f.exists())
        {
            printf("Unable to set stylesheet, file not found\n");
            printf("Tried to load: [%s]\n", f.fileName().toStdString().c_str() );
        }
        else
        {
            if (f.open(QFile::ReadOnly | QFile::Text)) {
                QTextStream ts(&f);
                qApp->setStyleSheet(ts.readAll());
            }
        }
    } else {
        qApp->setStyleSheet("");
    }
}

void wfmain::setDefaultColors(int presetNumber)
{
    // These are the default color schemes
    if(presetNumber > numColorPresetsTotal-1)
        return;

    colorPrefsType *p = &colorPreset[presetNumber];

    // Begin every parameter with these safe defaults first:
    if(p->presetName == Q_NULLPTR)
    {
        p->presetName = new QString();
    }
    p->presetName->clear();
    p->presetName->append(QString("%1").arg(presetNumber));
    p->presetNum = presetNumber;
    p->gridColor = QColor(0,0,0,255);
    p->axisColor = QColor(Qt::white);
    p->textColor = QColor(Qt::white);
    p->spectrumLine = QColor(Qt::yellow);
    p->spectrumFill = QColor("transparent");
    p->underlayLine = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150).lighter(200);
    p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
    p->plotBackground = QColor(Qt::black);
    p->tuningLine = QColor(Qt::blue);
    p->passband = QColor(Qt::blue);
    p->pbt = QColor(0x32,0xff,0x00,0x00);
    p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
    p->meterAverage = QColor(0x2f,0xb7,0xcd);
    p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
    p->meterPeakScale = QColor(Qt::red);
    p->meterLowerLine = QColor(0xed,0xf0,0xf1);
    p->meterLowText = QColor(0xef,0xf0,0xf1);

    p->wfBackground = QColor(Qt::black);
    p->wfAxis = QColor(Qt::white);
    p->wfGrid = QColor(Qt::white);
    p->wfText = QColor(Qt::white);

    p->clusterSpots = QColor(Qt::red);

    p->buttonOff = QColor("transparent");
    p->buttonOn = QColor(0x50,0x50,0x50);


    //qInfo(logSystem()) << "default color preset [" << pn << "] set to pn.presetNum index [" << p->presetNum << "]" << ", with name " << *(p->presetName);

    switch (presetNumber)
    {
        case 0:
        {
            // Dark
            p->presetName->clear();
            p->presetName->append("Dark");
            p->plotBackground = QColor(0,0,0,255);
            p->axisColor = QColor(Qt::white);
            p->textColor = QColor(255,255,255,255);
            p->gridColor = QColor(0,0,0,255);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::yellow);
            p->underlayLine = QColor(0x96,0x33,0xff,0xff);
            p->underlayFill = QColor(20+200/4.0*1,70*(1.6-1/4.0), 150, 150);
            p->tuningLine = QColor(0xff,0x55,0xff,0xff);
            p->passband = QColor(0x32,0xff,0xff,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterLevel = QColor(0x14,0x8c,0xd2).darker();
            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakScale = QColor(Qt::red);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb).lighter();
            p->meterLowerLine = QColor(0xef,0xf0,0xf1);
            p->meterLowText = QColor(0xef,0xf0,0xf1);

            p->wfBackground = QColor(Qt::black);
            p->wfAxis = QColor(Qt::white);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::white);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
            break;
        }
        case 1:
        {
            // Bright
            p->presetName->clear();
            p->presetName->append("Bright");
            p->plotBackground = QColor(Qt::white);
            p->axisColor = QColor(200,200,200,255);
            p->gridColor = QColor(255,255,255,0);
            p->textColor = QColor(Qt::black);
            p->spectrumFill = QColor("transparent");
            p->spectrumLine = QColor(Qt::black);
            p->underlayLine = QColor(Qt::blue);
            p->tuningLine = QColor(Qt::darkBlue);
            p->passband = QColor(0x64,0x00,0x00,0x80);
            p->pbt = QColor(0x32,0xff,0x00,0x00);

            p->meterAverage = QColor(0x3f,0xb7,0xcd);
            p->meterPeakLevel = QColor(0x3c,0xa0,0xdb);
            p->meterPeakScale = QColor(Qt::darkRed);
            p->meterLowerLine = QColor(Qt::black);
            p->meterLowText = QColor(Qt::black);

            p->wfBackground = QColor(Qt::white);
            p->wfAxis = QColor(200,200,200,255);
            p->wfGrid = QColor("transparent");
            p->wfText = QColor(Qt::black);
            p->clusterSpots = QColor(Qt::red);
            p->buttonOff = QColor("transparent");
            p->buttonOn = QColor(0x50,0x50,0x50);
            break;
        }

        case 2:
        case 3:
        case 4:
        default:
            break;

    }
    //ui->colorPresetCombo->setItemText(presetNumber, *(p->presetName));
}

void wfmain::initPeriodicCommands()
{
    // This function places periodic polling commands into the queue.
    // Can be run multiple times as it will remove all existing entries.

    qInfo(logSystem()) << "Start periodic commands (and delete unsupported)";

    queue->clear();

    foreach (auto cap, rigCaps->periodic) {
        if (cap.receiver == char(-1)) {
            for (uchar v=0;v<rigCaps->numReceiver;v++)
            {
                qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(v);
                queue->add(queuePriority(cap.prioVal),cap.func,true,v);
            }
        }
        else {
            qDebug(logSystem()) << "Inserting command" << funcString[cap.func] << "priority" << cap.priority << "on Receiver" << QString::number(cap.receiver);
            queue->add(queuePriority(cap.prioVal),cap.func,true,cap.receiver);
        }
    }


    // IC7610_V30_VA_PERIODIC
    // Independent telemetry for the optional fourth V/A meter row.
    if (rigCaps->commands.contains(funcVdMeter))
        queue->add(priorityMediumHigh, funcVdMeter, true, 0);
    if (rigCaps->commands.contains(funcIdMeter))
        queue->add(priorityMediumHigh, funcIdMeter, true, 0);
    // Set the second meter here as I suspect we need to be connected for it to work?
    if (!rigCaps->commands.contains(funcMeterType))
    {
        prefs.meter1Type = meterS; // Just in case we have previously connected to a radio with meter type options.
    }
    changeMeterType(prefs.meter1Type, 1);
    changeMeterType(prefs.meter2Type, 2);
    changeMeterType(prefs.meter3Type, 3);
    ui->meter2Widget->blockMeterType(prefs.meter3Type);
    ui->meter3Widget->blockMeterType(prefs.meter2Type);
    if (prefs.meter2Type == meterComp)
        ui->meter2Widget->setCompReverse(prefs.compMeterReverse);
    if (prefs.meter3Type == meterComp)
        ui->meter3Widget->setCompReverse(prefs.compMeterReverse);

/*
    meter* marray[3];
    marray[0] = ui->meterSPoWidget;
    marray[1] = ui->meter2Widget;
    marray[2] = ui->meter3Widget;
    for(int m=0; m < 3; m++) {
        funcs meterCmd = meter_tToMeterCommand(marray[m]->getMeterType());
        if(meterCmd != funcNone) {
            qDebug() << "Adding meter command per current UI meters.";
            queue->add(priorityHighest,queueItem(meterCmd,true));
        }
    }
*/


}

void wfmain::receivePTTstatus(bool pttOn)
{
    // This is the only place where amTransmitting and the transmit button text should be changed:
    if (pttOn && !amTransmitting)
    {

        pttLed->setState(QLedLabel::State::StateError);
        pttLed->setToolTip("Transmitting");
        changePrimaryMeter(true);
        if(splitModeEnabled)
        {
            pttLed->setState(QLedLabel::State::StateSplitErrorOk);
            pttLed->setToolTip("TX Split");
        } else {
            pttLed->setState(QLedLabel::State::StateError);
            pttLed->setToolTip("Transmitting");
        }
    }
    else if (!pttOn && amTransmitting)
    {
        pttLed->setState(QLedLabel::State::StateOk);
        pttLed->setToolTip("Receiving");
        changePrimaryMeter(false);
        // Send updated BSR? Currently a work-in-progress M0VSE
        //receivers[currentReceiver]->updateBSR(&rigCaps->bands);
    }
    amTransmitting = pttOn;
    rpt->handleTransmitStatus(pttOn);
    changeTxBtn();
    if (audioProcWin)   audioProcWin->setTransmitting(pttOn);
    if (rxAudioProcWin) rxAudioProcWin->setTransmitting(pttOn);
}

void wfmain::changeTxBtn()
{
    if(amTransmitting)
    {
        ui->transmitBtn->setText("Receive");

    } else {
        ui->transmitBtn->setText("Transmit");
    }
}

void wfmain::changePrimaryMeter(bool transmitOn) {
    // Change the Primary UI Meter and alter the queue

    // This function is only called from one place:
    // When we receive a *new* PTT status.

    funcs newCmd;
    funcs oldCmd;
    double lowVal = 0.0;
    double highVal = 255.0;
    double lineVal = 200;

    if (rigCaps == Q_NULLPTR) {
        qWarning(logSystem()) << "Cannot swap meter type without rigCaps.";
        return;
    }

    if(transmitOn) {
        oldCmd = meter_tToMeterCommand(meterS);
        newCmd = meter_tToMeterCommand(meterPower);
        ui->meterSPoWidget->setMeterType(meterPower);
        ui->meterSPoWidget->setMeterShortString( meterString[(int)meterPower] );
        if (rigCaps->meters[meterPower].size())
            getMeterExtremities(meterPower, lowVal, highVal, lineVal);
        ui->meterSPoWidget->setMeterExtremities(lowVal, highVal, lineVal);

    } else {
        oldCmd = meter_tToMeterCommand(meterPower);
        newCmd = meter_tToMeterCommand(meterS);
        ui->meterSPoWidget->setMeterType(meterS);
        ui->meterSPoWidget->setMeterShortString( meterString[(int)meterS] );
        if (rigCaps->meters[meterS].size())
            getMeterExtremities(meterS, lowVal, highVal, lineVal);
        ui->meterSPoWidget->setMeterExtremities(lowVal, highVal, lineVal);
    }
    queue->del(oldCmd,0);
    if (rigCaps->commands.contains(newCmd)) {
        queue->add(priorityHighest,queueItem(newCmd,true,0));
    }
    ui->meterSPoWidget->clearMeterOnPTTtoggle();
    ui->meter2Widget->clearMeterOnPTTtoggle();
    ui->meter3Widget->clearMeterOnPTTtoggle();
}

void wfmain::changeFullScreenMode(bool checked)
{
    if(checked)
    {
        this->showFullScreen();
        onFullscreen = true;
    } else {
        this->showNormal();
        onFullscreen = false;
    }
    prefs.useFullScreen = checked;
}

void wfmain::changeMode(rigMode_t mode, quint8 rx)
{
    bool dataOn = false;
    if(((quint8) mode >> 4) == 0x08)
    {
        dataOn = true;
        mode = (rigMode_t)((int)mode & 0x0f);
    }

    changeMode(mode, dataOn, rx);
}

void wfmain::changeMode(rigMode_t mode, quint8 data, quint8 rx)
{
    for (modeInfo &mi: rigCaps->modes)
    {
        if (mi.mk == mode)
        {
            if(receivers.size() > rx && mi.mk != receivers[rx]->currentMode().mk)
            {
                modeInfo m;
                m = modeInfo(mi);
                m.data = data;
                m.VFO=selVFO_t::activeVFO;
                m.filter = receivers[rx]->currentFilter();
                qDebug(logRig()) << "changeMode:" << mode << "data" << data << "rx" << rx;
                vfoCommandType t = queue->getVfoCommand(vfoA,rx,true);
                queue->add(priorityImmediate,queueItem(t.modeFunc,QVariant::fromValue<modeInfo>(m),false,t.receiver));
            }

            break;
        }
    }
}

void wfmain::on_freqDial_valueChanged(int value)
{
    int fullSweep = ui->freqDial->maximum() - ui->freqDial->minimum();

    freqt f;
    f.Hz = 0;
    f.MHzDouble = 0;

    volatile int delta = 0;

    if(freqLock)
    {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }

    delta = (value - oldFreqDialVal);

    if(delta > fullSweep/2)
    {
        // counter-clockwise past the zero mark
        // ie, from +3000 to 3990, old=3000, new = 3990, new-old = 990
        // desired delta here would actually be -10
        delta = delta - fullSweep;
    } else if (delta < -fullSweep/2)
    {
        // clock-wise past the zero mark
        // ie, from +3990 to 3000, old=3990, new = 3000, new-old = -990
        // desired delta here would actually be +10
        delta = fullSweep + delta;
    }

    // The step size is 10, which forces the knob to not skip a step crossing zero.
    if(abs(delta) < ui->freqDial->singleStep()) {
        // Took a small step. Let's just round it up:
        if(delta > 0)
            delta = 1;
        else
            delta = -1;
    } else {
        delta = delta / ui->freqDial->singleStep();
    }


    // With the number of steps and direction of steps established,
    // we can now adjust the frequency:

    f.Hz = roundFrequencyWithStep(receivers[currentReceiver]->getFrequency().Hz, delta, tsKnobHz);
    f.MHzDouble = f.Hz / (double)1E6;
    if (f.Hz > 0)
    {
        oldFreqDialVal = value;
        receivers[currentReceiver]->setFrequency(f);
        queue->addUnique(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,currentReceiver,true).freqFunc,
                                                QVariant::fromValue<freqt>(f),false,uchar(currentReceiver)));
    } else {
        ui->freqDial->blockSignals(true);
        ui->freqDial->setValue(oldFreqDialVal);
        ui->freqDial->blockSignals(false);
        return;
    }
}

void wfmain::on_aboutBtn_clicked()
{
    abtBox->show();
}

void wfmain::gotoMemoryPreset(int presetNumber)
{
    preset_kind temp = mem.getPreset(presetNumber);
    if(!temp.isSet)
    {
        qWarning(logGui()) << "Recalled Preset #" << presetNumber << "is not set.";
    }
    //setFilterVal = ui->modeFilterCombo->currentIndex()+1; // TODO, add to memory
    setModeVal = temp.mode;
    freqt memFreq;
    modeInfo m;
    m.mk = temp.mode;
    //m.filter = ui->modeFilterCombo->currentIndex()+1;
    m.reg =(quint8) m.mk; // fallback, works only for some modes
    memFreq.Hz = temp.frequency * 1E6;
    memFreq.MHzDouble = memFreq.Hz / 1.0E6;
    if (receivers.size())
        receivers[0]->setFrequency(memFreq);
    qDebug(logGui()) << "Recalling preset number " << presetNumber << " as frequency " << temp.frequency << "MHz";
}

void wfmain::saveMemoryPreset(int presetNumber)
{
    // int, double, rigMode_t
    double frequency;
    if (receivers.size()) {
        if(receivers[0]->getFrequency().Hz == 0)
        {
            frequency = receivers[0]->getFrequency().MHzDouble;
        } else {
            frequency = receivers[0]->getFrequency().Hz / 1.0E6;
        }
        rigMode_t mode = currentMode;
        qDebug(logGui()) << "Saving preset number " << presetNumber << " to frequency " << frequency << " MHz";
        mem.setPreset(presetNumber, frequency, mode);
    }
}
\
void wfmain::on_rfGainSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRfGain,QVariant::fromValue<ushort>(value),false,currentReceiver));
}

void wfmain::on_afGainSlider_valueChanged(int value)
{
    if(prefs.enableLAN)
    {
        // Remember current setting.
        prefs.rxSetup.localAFgain = (quint8)(value);
        prefs.localAFgain = (quint8)(value);
    }

    queue->addUnique(priorityImmediate,queueItem(funcAfGain,QVariant::fromValue<ushort>(value),false,currentReceiver));
}

void wfmain::on_monitorSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcMonitorGain,QVariant::fromValue<ushort>(value),false,currentReceiver));
}

void wfmain::on_monitorLabel_clicked()
{
    cacheItem ca = queue->getCache(funcMonitor);
    bool mon = ca.value.toBool();
    queue->add(priorityImmediate,queueItem(funcMonitor,QVariant::fromValue<bool>(!mon)));
}


void wfmain::on_tuneNowBtn_clicked()
{

    if (!prefs.enablePTT)
    {
        showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
        return;
    }

    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(2U)));
    showStatusBarText("Starting ATU tuning cycle...");
    ATUCheckTimer.setSingleShot(true);
    ATUCheckTimer.start(5000);
}

void wfmain::on_tuneEnableChk_clicked(bool checked)
{
    queue->addUnique(priorityImmediate,queueItem(funcTunerStatus,QVariant::fromValue<uchar>(checked)));
    showStatusBarText(QString("Turning %0 ATU").arg(checked?"on":"off"));
    ATUCheckTimer.setSingleShot(true);
    ATUCheckTimer.start(5000);
}

bool wfmain::on_exitBtn_clicked()
{
    bool ret=false;
    if (prefs.settingsChanged && prefs.confirmSettingsChanged)
    {
        QCheckBox *cb = new QCheckBox("Don't ask me again");
        cb->setToolTip("Don't ask me to confirm saving settings again");
        QMessageBox msgbox;
        msgbox.setText("Settings have changed since last save, exit anyway?\n");
        msgbox.setIcon(QMessageBox::Icon::Question);
        QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
        QAbstractButton *saveButton = msgbox.addButton(QMessageBox::Save);
        msgbox.addButton(QMessageBox::No);
        msgbox.setDefaultButton(QMessageBox::Yes);
        msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
            {
                prefs.confirmSettingsChanged=false;
            } else {
                prefs.confirmSettingsChanged=true;
            }
            settings->beginGroup("Interface");
            settings->setValue("ConfirmSettingsChanged", this->prefs.confirmSettingsChanged);
            settings->endGroup();
            settings->sync();
        });

        msgbox.exec();
        delete cb;

        if (msgbox.clickedButton() == yesButton) {
            QApplication::exit();
        } else if (msgbox.clickedButton() == saveButton) {
            saveSettings();
        } else {
            return true;
        }

    }

    // Are you sure?
    if (!prefs.confirmExit) {
        QApplication::exit();
    } else {
        QCheckBox *cb = new QCheckBox(tr("Don't ask me again"));
        cb->setToolTip(tr("Don't ask me to confirm exit again"));
        QMessageBox msgbox;
        msgbox.setText(tr("Are you sure you wish to exit?\n"));
        msgbox.setIcon(QMessageBox::Icon::Question);
        QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
        msgbox.addButton(QMessageBox::No);
        msgbox.setDefaultButton(QMessageBox::Yes);
        msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
            {
                prefs.confirmExit=false;
            } else {
                prefs.confirmExit=true;
            }
            settings->beginGroup("Interface");
            settings->setValue("ConfirmExit", this->prefs.confirmExit);
            settings->endGroup();
            settings->sync();
        });

        msgbox.exec();
        delete cb;

        if (msgbox.clickedButton() == yesButton) {
            QApplication::exit();
        } else {
            ret=true;
        }
    }
    return ret;
}

void wfmain::handlePttLimit()
{
    // transmission time exceeded!
    showStatusBarText("Transmit timeout at 3 minutes. Sending PTT OFF command now.");
    queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));
}

void wfmain::on_saveSettingsBtn_clicked()
{
    saveSettings(); // save memory, UI, and radio settings
}

void wfmain::receiveATUStatus(quint8 atustatus)
{
    // qInfo(logSystem()) << "Received ATU status update: " << (unsigned int) atustatus;
    switch(atustatus)
    {
        case 0x00:
            // ATU not active
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(false);
            ui->tuneEnableChk->blockSignals(false);
            if(ATUCheckTimer.isActive())
                showStatusBarText("ATU not enabled.");
            break;
        case 0x01:
            // ATU enabled
            ui->tuneEnableChk->blockSignals(true);
            ui->tuneEnableChk->setChecked(true);
            ui->tuneEnableChk->blockSignals(false);
            if(ATUCheckTimer.isActive())
                showStatusBarText("ATU enabled.");
            break;
        case 0x02:
            // ATU tuning in-progress.
            // Add command queue to check again and update status bar
            // qInfo(logSystem()) << "Received ATU status update that *tuning* is taking place";
            showStatusBarText("ATU is Tuning...");
            ATUCheckTimer.stop();
            ATUCheckTimer.start(5000);
            queue->add(priorityHighest,funcTunerStatus);
            break;
        default:
            qInfo(logSystem()) << "Did not understand ATU status: " << (unsigned int) atustatus;
            break;
    }
}

void wfmain::handleExtConnectBtn() {
    // from settings widget
    on_connectBtn_clicked();
}

void wfmain::handleRevertSettingsBtn() {
    // from settings widget
    int ret = QMessageBox::warning(this, tr("Revert settings"),
                                   tr("Are you sure you wish to reset all wfview settings?\nIf so, wfview will exit and you will need to start the program again."),
                                   QMessageBox::Ok | QMessageBox::Cancel,
                                   QMessageBox::Cancel);
    if (ret == QMessageBox::Ok) {
        qInfo(logSystem()) << "Per user request, resetting preferences.";
        prefs = defPrefs;
        udpPrefs = udpDefPrefs;
        serverConfig.enabled = false;
        serverConfig.users.clear();

        saveSettings();
        qInfo(logSystem()) << "Closing wfview for full preference-reset.";
        QApplication::exit();
    }
}

void wfmain::on_connectBtn_clicked()
{
    this->rigStatus->setText(""); // Clear status

    if (connStatus == connDisconnected) {
        connectionHandler(true);
    }
    else
    {
        connectionHandler(false);
    }

    ui->connectBtn->clearFocus();
}

void wfmain::on_sqlSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcSquelch,QVariant::fromValue<ushort>(value),false,currentReceiver));
}

void wfmain::on_transmitBtn_clicked()
{
    if(!amTransmitting)
    {
        // Currently receiving
        if(!prefs.enablePTT)
        {
            showStatusBarText("PTT is disabled, not sending command. Change under Settings tab.");
            return;
        }

        // Are we already PTT? Not a big deal, just send again anyway.
        showStatusBarText("Sending PTT ON command. Use Control-R to receive.");
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(true),false,uchar(0)));

        // send PTT
        // Start 3 minute timer
        pttTimer->start();

    } else {
        // Currently transmitting
        queue->add(priorityImmediate,queueItem(funcTransceiverStatus,QVariant::fromValue<bool>(false),false,uchar(0)));

        pttTimer->stop();
    }
}

void wfmain::setRadioTimeDatePrep()
{
    if(!waitingToSetTimeDate)
    {
        // 1: Find the current time and date
        QDateTime now;
        if(prefs.useUTC)
        {
            now = QDateTime::currentDateTimeUtc();
            //now.setTime(QTime::currentTime());
        } else {
            now = QDateTime::currentDateTime();
            //now.setTime(QTime::currentTime());
        }

        int second = now.time().second();

        // 2: Find how many mseconds until next minute
        int msecdelay = QTime::currentTime().msecsTo( QTime::currentTime().addSecs(60-second) );

        // 3: Compute time and date at one minute later
        QDateTime setpoint = now.addMSecs(msecdelay); // at HMS or possibly HMS + some ms. Never under though.

        // 4: Prepare data structs for the time at one minute later
        timesetpoint.hours = (quint8)setpoint.time().hour();
        timesetpoint.minutes = (quint8)setpoint.time().minute();
        datesetpoint.day = (quint8)setpoint.date().day();
        datesetpoint.month = (quint8)setpoint.date().month();
        datesetpoint.year = (uint16_t)setpoint.date().year();
        unsigned int utcOffsetSeconds = (unsigned int)abs(setpoint.offsetFromUtc());
        bool isMinus = setpoint.offsetFromUtc() < 0;
        utcsetting.hours = utcOffsetSeconds / 60 / 60;
        utcsetting.minutes = (utcOffsetSeconds - (utcsetting.hours*60*60) ) / 60;
        utcsetting.isMinus = isMinus;

        timeSync->setInterval(msecdelay);
        timeSync->setSingleShot(true);

        // 5: start one-shot timer for the delta computed in #2.
        timeSync->start();
        waitingToSetTimeDate = true;
        showStatusBarText(QString("Setting time, date, and UTC offset for radio in %1 seconds.").arg(msecdelay/1000));
    }
}

void wfmain::setRadioTimeDateSend()
{
    // Issue priority commands for UTC offset, date, and time
    // UTC offset must come first, otherwise the radio may "help" and correct for any changes.

    showStatusBarText(QString("Setting time, date, and UTC offset for radio now."));

    queue->add(priorityImmediate,queueItem(funcUTCOffset,QVariant::fromValue<timekind>(utcsetting)));
    queue->add(priorityHighest,queueItem(funcTime,QVariant::fromValue<timekind>(timesetpoint)));
    queue->add(priorityHighest,queueItem(funcDate,QVariant::fromValue<datekind>(datesetpoint)));

    waitingToSetTimeDate = false;
}

void wfmain::showAndRaiseWidget(QWidget *w)
{
    if(!w)
        return;

    if(w->isMinimized())
    {
        w->raise();
        w->activateWindow();
        return;
    }
    w->show();
    w->raise();
    w->activateWindow();
}

void wfmain::changeSliderQuietly(QSlider *slider, int value)
{
    if (slider->value() != value) {
        slider->blockSignals(true);
        slider->setValue(value);
        slider->blockSignals(false);
    }
}

void wfmain::statusFromSliderRaw(QString name, int rawValue)
{
    showStatusBarText(name + QString(": %1").arg(rawValue));
}

void wfmain::statusFromSliderPercent(QString name, int rawValue)
{
    showStatusBarText(name + QString(": %1%").arg((int)(100*rawValue/255.0)));
}

void wfmain::processModLevel(inputTypes source, quint8 level)
{
    if (receivers.size()) {
        quint8 data = receivers[0]->getDataMode();

        if(prefs.inputSource[data].type == source)
        {
            prefs.inputSource[data].level = level;
            changeSliderQuietly(ui->micGainSlider, level);
        }
    }
}

void wfmain::receiveModInput(rigInput input, quint8 data)
{
    // This will ONLY fire if the input type is different to the current one
    if (currentModSrc[data].type != input.type && receivers.size())
    {
        qInfo() << QString("Data: %0 Input: %1 current: %2").arg(data).arg(input.name).arg(prefs.inputSource[data].name);
        queue->del(getInputTypeCommand(prefs.inputSource[data].type).cmd,currentReceiver);
        prefs.inputSource[data] = input;
        if (receivers[0]->getDataMode() == data)
        {
            queue->addUnique(priorityHigh,getInputTypeCommand(input.type).cmd,true,currentReceiver);
            changeModLabel(input,false);
        }
        switch (data) {
        case 0:
            setupui->updateRsPrefs((int)rs_dataOffMod);
            break;
        case 1:
            setupui->updateRsPrefs((int)rs_data1Mod);
            break;
        case 2:
            setupui->updateRsPrefs((int)rs_data2Mod);
            break;
        case 3:
            setupui->updateRsPrefs((int)rs_data3Mod);
            break;
        }
        currentModSrc[data] = rigInput(input);
    }
}

void wfmain::receiveTuningStep(quint8 step)
{
    if (step > 0)
    {
        for (auto &s: rigCaps->steps)
        {
            if (step == s.num && ui->tuningStepCombo->currentData().toUInt() != s.hz) {
                qDebug(logSystem()) << QString("Received new Tuning Step %0").arg(s.name);
                ui->tuningStepCombo->setCurrentIndex(ui->tuningStepCombo->findData(s.hz));
                for (const auto& receiver: receivers)
                {
                    receiver->setStepSize(s.hz);
                }
                break;
            }
        }
    }
}

void wfmain::receiveMeter(meter_t inMeter, double level)
{

    switch(inMeter)
    {
    // These first two meters, S and Power,
    // are automatically assigned to the primary meter.
        case meterS:
                ui->meterSPoWidget->setMeterType(meterS);
                ui->meterSPoWidget->setLevel(level);
                ui->meterSPoWidget->repaint();
            break;
        case meterPower:
            ui->meterSPoWidget->setMeterType(meterPower);
            ui->meterSPoWidget->setLevel(level);
            ui->meterSPoWidget->update();
            break;
        default:
            meter* marray[2];
            marray[0] = ui->meter2Widget;
            marray[1] = ui->meter3Widget;
            for(int m=0; m < 2; m++) {
                if(marray[m]->getMeterType() == inMeter)
                {
                    // The incoming meter data matches the UI meter
                    marray[m]->setLevel(level);
                } else if ( (marray[m]->getMeterType() == meterAudio) &&
                            (inMeter == meterTxMod) && amTransmitting) {
                    marray[m]->setLevel(level);
                } else if (  (marray[m]->getMeterType() == meterAudio) &&
                             (inMeter == meterRxAudio) && !amTransmitting) {
                    marray[m]->setLevel(level);
                }
            }

            break;
    }
}


void wfmain::receiveMonitor(bool en)
{
    ui->monitorLabel->setActive(en);
}



void wfmain::on_txPowerSlider_valueChanged(int value)
{
    queue->addUnique(priorityImmediate,queueItem(funcRFPower,QVariant::fromValue<ushort>(value),false));
}

void wfmain::on_micGainSlider_valueChanged(int value)
{
    processChangingCurrentModLevel((quint8) value);
}


void wfmain::changeModLabelAndSlider(rigInput source)
{
    changeModLabel(source, true);
}

void wfmain::changeModLabel(rigInput input)
{
    changeModLabel(input, false);
}

void wfmain::changeModLabel(rigInput input, bool updateLevel)
{

    funcType f = getInputTypeCommand(input.type);

    queue->add(priorityMedium,f.cmd,true,currentReceiver);

    ui->micGainSlider->setRange(f.minVal,f.maxVal);

    ui->modSliderLbl->setText(input.name);

    if(updateLevel)
    {
        changeSliderQuietly(ui->micGainSlider, input.level);
    }
}

void wfmain::processChangingCurrentModLevel(quint8 level)
{
    // slider moved, so find the current mod and issue the level set command.

    funcType f;
    if (receivers.size()) {
        quint8 d = receivers[currentReceiver]->getDataMode();
        f = getInputTypeCommand(prefs.inputSource[d].type);

        qDebug(logSystem()) << "Updating mod level for" << funcString[f.cmd] << "setting to" << level;
        queue->addUnique(priorityImmediate,queueItem(f.cmd,QVariant::fromValue<ushort>(level),false,currentReceiver));
    }
}

void wfmain::on_tuneLockChk_clicked(bool checked)
{
    freqLock = checked;
    for (const auto &rx: receivers)
    {
        rx->setFreqLock(checked);
    }
}

void wfmain::on_rptSetupBtn_clicked()
{
    if(rpt->isMinimized())
    {
        rpt->raise();
        rpt->activateWindow();
        return;
    }
    rpt->show();
    rpt->raise();
    rpt->activateWindow();
}

void wfmain::on_attSelCombo_activated(int index)
{
    queue->add(priorityImmediate,queueItem(funcAttenuator,QVariant::fromValue<uchar>(ui->attSelCombo->itemData(index).toInt()),false,currentReceiver));
    queue->add(priorityHigh,funcPreamp,false);
}

void wfmain::on_preampSelCombo_activated(int index)
{
    queue->add(priorityImmediate,queueItem(funcPreamp,QVariant::fromValue<uchar>(ui->preampSelCombo->itemData(index).toInt()),false,currentReceiver));
    queue->add(priorityHigh,funcAttenuator,false);
}

void wfmain::on_antennaSelCombo_activated(int index)
{
    antennaInfo ant;
    ant.antenna = (quint8)ui->antennaSelCombo->itemData(index).toInt();
    ant.rx = ui->rxAntennaCheck->isChecked();
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false,currentReceiver));
}

void wfmain::on_rxAntennaCheck_clicked(bool value)
{
    antennaInfo ant;
    ant.antenna = (quint8)ui->antennaSelCombo->currentData().toInt();
    ant.rx = value;
    queue->add(priorityImmediate,queueItem(funcAntenna,QVariant::fromValue<antennaInfo>(ant),false,currentReceiver));
}

void wfmain::receivePreamp(quint8 pre, uchar receiver)
{
    if (receiver == currentReceiver) {
        ui->preampSelCombo->setCurrentIndex(ui->preampSelCombo->findData(pre));
    }
}

void wfmain::receiveAttenuator(quint8 att, uchar receiver)
{
    if (receiver == currentReceiver) {
        ui->attSelCombo->setCurrentIndex(ui->attSelCombo->findData(att));
    }
}


void wfmain::calculateTimingParameters()
{
    // Function for calculating polling parameters.
    // Requires that we know the "baud rate" of the actual
    // radio connection.

    // baud on the serial port reflects the actual rig connection,
    // even if a client-server connection is being used.
    // Computed time for a 10 byte message, with a safety factor of 2.

    if (prefs.serialPortBaud == 0)
    {
        prefs.serialPortBaud = 9600;
        qInfo(logSystem()) << "WARNING: baud rate received was zero. Assuming 9600 baud, performance may suffer.";
    }

    unsigned int usPerByte = 9600*1000 / prefs.serialPortBaud;
    unsigned int msMinTiming=usPerByte * 10*2/1000;
    if(msMinTiming < 25)
        msMinTiming = 25;

    if(rigCaps != Q_NULLPTR && rigCaps->hasFDcomms)
    {
        queue->interval(msMinTiming);
    } else {
        queue->interval(msMinTiming * 4);
    }

    qInfo(logSystem()) << "Delay command interval timing: " << queue->interval() << "ms";

    // Normal:
    delayedCmdIntervalLAN_ms =  queue->interval();
    delayedCmdIntervalSerial_ms = queue->interval();

    // startup initial state:
    delayedCmdStartupInterval_ms =  queue->interval() * 3;
}

void wfmain::receiveBaudRate(quint32 baud)
{
    qInfo() << "Received serial port baud rate from remote server:" << baud;
    prefs.serialPortBaud = baud;
    calculateTimingParameters();
}

void wfmain::on_rigPowerOnBtn_clicked()
{
    powerRigOn();
}

void wfmain::on_rigPowerOffBtn_clicked()
{
    // Are you sure?
    if (!prefs.confirmPowerOff) {
        powerRigOff();
        return;
    }
    QCheckBox* cb = new QCheckBox(tr("Don't ask me again"));
    QMessageBox msgbox;
    msgbox.setWindowTitle(tr("Power"));
    msgbox.setText(tr("Power down the radio?\n"));
    msgbox.setIcon(QMessageBox::Icon::Question);
    QAbstractButton* yesButton = msgbox.addButton(QMessageBox::Yes);
    msgbox.addButton(QMessageBox::No);
    msgbox.setDefaultButton(QMessageBox::Yes);
    msgbox.setCheckBox(cb);
#if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
        QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
            if (state == Qt::CheckState::Checked)
#else
        QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
            if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
#endif
        {
            prefs.confirmPowerOff = false;
        }
        else {
            prefs.confirmPowerOff = true;
        }
        settings->beginGroup("Interface");
        settings->setValue("ConfirmPowerOff", this->prefs.confirmPowerOff);
        settings->endGroup();
        settings->sync();
    });

    msgbox.exec();

    if (msgbox.clickedButton() == yesButton) {
        powerRigOff();
    }
}

void wfmain::powerRigOn()
{
    // Bypass the queue
    emit sendPowerOn();
    // Need to allow time for the rig to power-on.
    // If we have no rigCaps then usual rig detection should occur.
    if (rigCaps != Q_NULLPTR) {
        calculateTimingParameters(); // Set queue interval
        QTimer::singleShot(5000, this, SLOT(initPeriodicCommands()));
        QTimer::singleShot(6500, this, SLOT(getInitialRigState()));
    }
}

void wfmain::powerRigOff()
{
    // Clear the queue to stop sending lots of data.
    queue->clear();
    emit sendPowerOff();
    queue->interval(-1); // Queue Disabled
}

void wfmain::on_ritTuneDial_valueChanged(int value)
{
    queue->add(priorityImmediate,queueItem(funcRitFreq,QVariant::fromValue(short(value))));
}

void wfmain::on_ritEnableChk_clicked(bool checked)
{
    queue->add(priorityImmediate,queueItem(funcRitStatus,QVariant::fromValue(checked)));
}

void wfmain::receiveRITStatus(bool ritEnabled)
{
    ui->ritEnableChk->blockSignals(true);
    ui->ritEnableChk->setChecked(ritEnabled);
    ui->ritEnableChk->blockSignals(false);
}

void wfmain::receiveRITValue(int ritValHz)
{
    if((ritValHz > -500) && (ritValHz < 500))
    {
        ui->ritTuneDial->blockSignals(true);
        ui->ritTuneDial->setValue(ritValHz);
        ui->ritTuneDial->blockSignals(false);
    } else {
        qInfo(logSystem()) << "Warning: out of range RIT value received: " << ritValHz << " Hz";
    }
}

void wfmain::showButton(QPushButton *btn)
{
    btn->setHidden(false);
}

void wfmain::hideButton(QPushButton *btn)
{
    btn->setHidden(true);
}

funcs wfmain::meter_tToMeterCommand(meter_t m)
{
    funcs c;
    switch(m)
    {

    case meterNone:
            c = funcNone;
            break;
        case meterS:
        case meterSubS:
            c = funcSMeter;
            break;
        case meterCenter:
            c = funcCenterMeter;
            break;
        case meterPower:
            c = funcPowerMeter;
            break;
        case meterSWR:
            c = funcSWRMeter;
            break;
        case meterALC:
            c = funcALCMeter;
            break;
        case meterComp:
            c = funcCompMeter;
            break;
        case meterCurrent:
            c = funcIdMeter;
            break;
        case meterVoltage:
            c = funcVdMeter;
            break;
        case meterdBu:
        case meterdBm:
        case meterdBuEMF:
            c = funcAbsoluteMeter;
            break;
        default:
            c = funcNone;
            break;
    }

    return c;
}

void wfmain::getMeterExtremities(meter_t m, double &lowVal, double &highVal, double &redLineVal) {
    lowVal = UINT16_MAX;
    highVal = (-1)*UINT16_MAX;
    redLineVal = 200.0;
    if (m==meterSubS)
    {
        m = meterS;
    }

    if(m==meterNone)
    {
        return;
    } else if (rigCaps != Q_NULLPTR){
        redLineVal = rigCaps->meterLines[m];
        for (auto it = rigCaps->meters[m].keyValueBegin(); it != rigCaps->meters[m].keyValueEnd(); ++it) {
            if (it->second < lowVal)
                lowVal = it->second;
            if (it->second > highVal || highVal == 255.0)
                highVal = it->second;
        }
        qDebug(logSystem()) << "Meter extremities:" << meterString[m] << "values, low:" << lowVal << ", high:" << highVal << ", red line:" << redLineVal;
        return;
    } else {
        // Hopefully we come back here after connection and do this correctly.
        qWarning(logSystem()) << "Cannot setup meter correctly without rigCaps.";
        return;
    }
}

void wfmain::changeMeterType(meter_t m, int meterNum)
{
    qDebug() << "Changing meter type.";
    meter_t newMeterType;
    meter_t oldMeterType;
    meter* uiMeter = NULL;
    if(meterNum == 1) {
        uiMeter = ui->meterSPoWidget;
    } else if(meterNum == 2) {
        uiMeter = ui->meter2Widget;
    } else if (meterNum == 3) {
        uiMeter = ui->meter3Widget;
    } else {
        qCritical() << "Error, invalid meter requested: meterNum ==" << meterNum;
        return;
    }
    newMeterType = m;
    oldMeterType = uiMeter->getMeterType();

    if(newMeterType == oldMeterType) {
        qDebug() << "Debug note: the old meter was the same as the new meter.";
    }

    funcs newCmd = meter_tToMeterCommand(newMeterType);
    funcs oldCmd = meter_tToMeterCommand(oldMeterType);

    //if (oldCmd != funcSMeter && oldCmd != funcNone)
    queue->del(oldCmd,(oldMeterType==meterSubS)?uchar(1):uchar(0));

    double lowVal = 0.0;
    double highVal = 2.0;
    double lineVal = 1.0;

    if (rigCaps == Q_NULLPTR && newMeterType != meterNone) {
        qWarning(logSystem()) << "Cannot change meter type without rigCaps.";
        return;
    }

    switch (newMeterType)
    {
    case meterdBu:
        lowVal = 0.0;
        highVal = 80.0;
        lineVal = 80.0;
        break;
    case meterdBuEMF:
        lowVal = 0.0;
        highVal = 85.0;
        lineVal = 85.0;
        break;
    case meterdBm:
        lowVal = -100.0;
        highVal = -20.0;
        lineVal = -20.0;
        break;
    default:
        if (rigCaps->meters[newMeterType].size())
            getMeterExtremities(newMeterType, lowVal, highVal, lineVal);
        break;
    }


    if(newMeterType==meterNone)
    {
        uiMeter->setMeterType(newMeterType);
        uiMeter->setMeterShortString("None");
    } else if (rigCaps != Q_NULLPTR){
        uiMeter->show();
        uiMeter->setMeterType(newMeterType);
        uiMeter->setMeterExtremities(lowVal, highVal, lineVal);
        uiMeter->setMeterShortString( meterString[(int)newMeterType] );

        if((newMeterType!=meterRxAudio) && (newMeterType!=meterTxMod) && (newMeterType!=meterAudio))
        {
            if (rigCaps->commands.contains(newCmd)) {
                queue->addUnique(priorityHighest,queueItem(newCmd,true,(newMeterType==meterSubS)?uchar(1):uchar(0)));
            }
        }

        if (meterNum == 1)
        {
            if (rigCaps->commands.contains(funcMeterType))
            {
                if (newMeterType == meterS)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(0),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                if (newMeterType == meterSubS)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(0),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBu)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(1),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBuEMF)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(2),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
                else if (newMeterType == meterdBm)
                    queue->add(priorityImmediate,queueItem(funcMeterType,QVariant::fromValue<uchar>(3),false,(newMeterType==meterSubS)?uchar(1):uchar(0)));
            }
            uiMeter->enableCombo(rigCaps->commands.contains(funcMeterType));
        }
    }
}

void wfmain::enableRigCtl(bool enabled)
{
    // migrated to this, keep
    if (rigCtl != Q_NULLPTR)
    {
        rigCtl->disconnect();
        delete rigCtl;
        rigCtl = Q_NULLPTR;
    }

    if (enabled) {
        // Start rigctld
        rigCtl = new rigCtlD(this);
        rigCtl->startServer(prefs.rigCtlPort);
    }
}

void wfmain::radioSelection(QList<radio_cap_packet> radios)
{
    selRad->populate(radios);
}

void wfmain::on_radioStatusBtn_clicked()
{
    if (selRad->isVisible())
    {
        selRad->hide();
    }
    else
    {
        selRad->show();
    }
}

void wfmain::setAudioDevicesUI()
{

}


// --- DEBUG FUNCTION ---
void wfmain::debugBtn_clicked()
{
    qInfo(logSystem()) << "Debug button pressed.";
    debugWindow* debug = new debugWindow();
    debug->setAttribute(Qt::WA_DeleteOnClose);
    debug->show();
}

// ----------   color helper functions:   ---------- //

void wfmain::useColorPreset(colorPrefsType *cp)
{
    // Apply the given preset to the UI elements
    // prototyped from setPlotTheme()
    if(cp == Q_NULLPTR)
        return;

    //qInfo(logSystem()) << "Setting plots to color preset number " << cp->presetNum << ", with name " << *(cp->presetName);
    ui->meterSPoWidget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
    ui->meter2Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);
    ui->meter3Widget->setColors(cp->meterLevel, cp->meterPeakScale, cp->meterPeakLevel, cp->meterAverage, cp->meterLowerLine, cp->meterLowText);

    ui->scopeDualBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    ui->dualWatchBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    ui->splitBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
                                    .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));
    //ui->mainSubTrackingBtn->setStyleSheet(QString("QPushButton {background-color: %0;} QPushButton:checked {background-color: %1;border: 1px solid;}")
    //                                .arg(cp->buttonOff.name(QColor::HexArgb),cp->buttonOn.name(QColor::HexArgb)));

    for (const auto& receiver: receivers) {
        receiver->colorPreset(cp);
    }
    if (this->colorPrefs != Q_NULLPTR)
        delete this->colorPrefs;
    this->colorPrefs = new colorPrefsType(*cp);
    qInfo() << "Setting color Preset" << cp->presetNum << "name" << *(cp->presetName);
}


void wfmain::setDefaultColorPresets()
{
    // Default wfview colors in each preset
    // gets overridden after preferences are loaded
    for(int pn=0; pn < numColorPresetsTotal; pn++)
    {
        setDefaultColors(pn);
    }
}

void wfmain::on_showLogBtn_clicked()
{
    if(logWindow->isMinimized())
    {
        logWindow->raise();
        logWindow->activateWindow();
        return;
    }
    logWindow->show();
    logWindow->raise();
    logWindow->activateWindow();
}

void wfmain::initLogging()
{
    // Set the logging file before doing anything else.
    m_logFile.reset(new QFile(logFilename));
    // Open the file logging
    m_logFile.data()->open(QFile::WriteOnly | QFile::Truncate | QFile::Text);
    logStream.reset(new QTextStream(m_logFile.data()));
    // Set handler
    qInstallMessageHandler(messageHandler);

    connect(logWindow, SIGNAL(setDebugMode(bool)), this, SLOT(setDebugLogging(bool)));
    connect(logWindow, SIGNAL(setInsaneLoggingMode(bool)), this, SLOT(setInsaneDebugLogging(bool)));
    connect(logWindow, SIGNAL(setRigctlLoggingMode(bool)), this, SLOT(setRigctlDebugLogging(bool)));

    // Interval timer for log window updates:
    logCheckingTimer.setInterval(100);
    connect(&logCheckingTimer, SIGNAL(timeout()), this, SLOT(logCheck()));
    logCheckingTimer.start();
}

void wfmain::logCheck()
{
    // This is called by a timer to check for new log messages and copy
    // the messages into the logWindow.
    QMutexLocker locker(&logTextMutex);
    int size = logStringBuffer.size();
    for(int i=0; i < size; i++)
    {
        handleLogText(logStringBuffer.back());
        logStringBuffer.pop_back();
    }
}

void wfmain::handleLogText(QPair<QtMsgType,QString> logMessage)
{
    // This function is just a pass-through
    logWindow->acceptLogText(logMessage);
}

void wfmain::setDebugLogging(bool debugModeOn)
{
    this->debugMode = debugModeOn;
    debugModeLogging = debugModeOn;
}

void wfmain::setInsaneDebugLogging(bool insaneLoggingOn)
{
    insaneDebugLogging = insaneLoggingOn;
}

void wfmain::setRigctlDebugLogging(bool rigctlLoggingOn)
{
    rigctlDebugLogging = rigctlLoggingOn;
}

void wfmain::messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    // Open stream file writes
    // bool insaneDebugLogging = true;// global
    if (type == QtDebugMsg && !debugModeLogging)
    {
        return;
    }

    if( (type == QtWarningMsg) && (msg.contains("QPainter::")) ) {
        // This is a message from QCP about a collapsed plot area.
        // Ignore.
        return;
    }

    if( (type == QtDebugMsg) && (!insaneDebugLogging) && (qstrncmp(context.category, "rigTraffic", 10)==0) ) {
        return;
    }

    if( (type == QtDebugMsg) && (!rigctlDebugLogging) && (qstrncmp(context.category, "rigctld", 10)==0) ) {
        return;
    }

    QMutexLocker locker(&logMutex);
    QString text;

    // Write the date of recording
    *logStream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz ");
    text.append(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz "));
    // By type determine to what level belongs message

    switch (type)
    {
        case QtDebugMsg:
            *logStream << "DBG ";
            text.append("DBG ");
            break;
        case QtInfoMsg:
            *logStream << "INF ";
            text.append("INF ");
            break;
        case QtWarningMsg:
            *logStream << "WRN ";
            text.append("WRN ");
            break;
        case QtCriticalMsg:
            *logStream << "CRT ";
            text.append("CRT ");
            break;
        case QtFatalMsg:
            *logStream << "FTL ";
            text.append("FLT ");
            break;
    }
    // Write to the output category of the message and the message itself
    *logStream << context.category << ": " << msg << "\n";
    logStream->flush();    // Clear the buffered data
    text.append(context.category);
    text.append(": ");
    text.append(msg);
    logTextMutex.lock();
    logStringBuffer.push_front(QPair<QtMsgType,QString>(type,text));
    logTextMutex.unlock();
}

void wfmain::receiveClusterOutput(QString text) {
    setupui->insertClusterOutputText(text);
}

void wfmain::changePollTiming(int timing_ms, bool setUI)
{
    queue->interval(timing_ms);
    qInfo(logSystem()) << "User changed radio polling interval to " << timing_ms << "ms.";
    showStatusBarText("User changed radio polling interval to " + QString("%1").arg(timing_ms) + "ms.");
    prefs.polling_ms = timing_ms;
    delayedCmdIntervalLAN_ms =  timing_ms;
    delayedCmdIntervalSerial_ms = timing_ms;
    (void)setUI;
}

void wfmain::connectionHandler(bool connect)
{
    queue->clear();
    emit sendCloseComm();

    rigName->setText("NONE");
    connStatus = connDisconnected;

    emit connectionStatus(connect); // Signal any other parts that need to know if we are connecting/connected.


    if (connect) {
        openRig();
    } else {
        ui->connectBtn->setText("Connect to Radio");
        enableRigCtl(false);
        removeRig();
        // Stop time sync timer if running.
        if(timeSync != Q_NULLPTR) {
            if (timeSync->isActive())
                timeSync->stop();
        } else {
            qWarning(logSystem()) << "In handling connection status, did not find valid timeSync timer. Connect: " << connect;
        }
    }

    // Whatever happened, make sure we delete the memories window.
    if (memWindow != Q_NULLPTR) {
        memWindow->close();
        delete memWindow;
        memWindow = Q_NULLPTR;
    }


}

void wfmain::connectionTimeout()
{
    qWarning(logSystem()) << "No response received to connection request";
    connectionHandler(false);
    connectionHandler(true);
}

void wfmain::on_cwButton_clicked()
{
    if (cw != Q_NULLPTR) {
        if(cw->isMinimized())
        {
            cw->raise();
            cw->activateWindow();
            return;
        }
        cw->show();
        cw->raise();
        cw->activateWindow();
    }
}

void wfmain::on_memoriesBtn_clicked()
{
    if (rigCaps != Q_NULLPTR) {

        if (memWindow == Q_NULLPTR) {
            // Add slowload option for background loading.
            memWindow = new memories(isRadioAdmin, false);
            this->memWindow->connect(this, SIGNAL(haveMemory(memoryType)), memWindow, SLOT(receiveMemory(memoryType)));
            this->memWindow->connect(this, SIGNAL(haveMemoryName(memoryTagType)), memWindow, SLOT(receiveMemoryName(memoryTagType)));
            this->memWindow->connect(this, SIGNAL(haveMemorySplit(memorySplitType)), memWindow, SLOT(receiveMemorySplit(memorySplitType)));
            for(const auto& r: receivers) {
                connect(memWindow,SIGNAL(memoryMode(bool)),r,SLOT(memoryMode(bool)));
            }
            memWindow->setRegion(prefs.region);
            memWindow->populate(); // Call populate to get the initial memories
        }

        // Are you sure?
        if (prefs.confirmMemories) {
            memWindow->show();
        } else {
            QCheckBox *cb = new QCheckBox(tr("Don't ask me again"));
            cb->setToolTip(tr("Don't ask me to confirm memories again"));
            QMessageBox msgbox;
            msgbox.setText(tr("Memories are considered an experimental feature,\nPlease make sure you have a full backup of your radio before making changes.\nAre you sure you want to continue?\n"));
            msgbox.setIcon(QMessageBox::Icon::Question);
            QAbstractButton *yesButton = msgbox.addButton(QMessageBox::Yes);
            msgbox.addButton(QMessageBox::No);
            msgbox.setDefaultButton(QMessageBox::Yes);
            msgbox.setCheckBox(cb);
    #if (QT_VERSION >= QT_VERSION_CHECK(6,7,0))
            QObject::connect(cb, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state){
                if (state == Qt::CheckState::Checked)
    #else
            QObject::connect(cb, &QCheckBox::stateChanged, this, [this](int state){
                if (static_cast<Qt::CheckState>(state) == Qt::CheckState::Checked)
    #endif
                {
                    prefs.confirmMemories=true;
                } else {
                    prefs.confirmMemories=false;
                }
                settings->beginGroup("Interface");
                settings->setValue("ConfirmMemories", this->prefs.confirmMemories);
                settings->endGroup();
                settings->sync();
            });

            msgbox.exec();
            delete cb;

            if (msgbox.clickedButton() == yesButton) {
                memWindow->show();
            }
        }
    } else {
        if (memWindow != Q_NULLPTR)
        {
            delete memWindow;
            memWindow = Q_NULLPTR;
        }
    }
}

void wfmain::on_rigCreatorBtn_clicked()
{
    rigCreator* create = new rigCreator();
    create->setAttribute(Qt::WA_DeleteOnClose);
    create->show();
}

void wfmain::on_TXaudioProcBtn_clicked()
{
    if (!audioProcWin) {
        audioProcWin = new AudioProcessingWidget(this);
        audioProcWin->setPrefs(prefs.txAudioProc);
        audioProcWin->setAntiAlias(prefs.wfAntiAlias);

        connect(audioProcWin, &AudioProcessingWidget::prefsChanged,
                this, &wfmain::onAudioProcPrefsChanged);

        connect(this, &wfmain::connectionStatus,
                audioProcWin, &AudioProcessingWidget::setConnected);
        audioProcWin->setConnected(connStatus == connConnected);

        if (txProc) {
            connect(txProc, &TxAudioProcessor::txInputLevel,
                    audioProcWin, &AudioProcessingWidget::updateInputLevel);
            connect(txProc, &TxAudioProcessor::txOutputLevel,
                    audioProcWin, &AudioProcessingWidget::updateOutputLevel);
            connect(txProc, &TxAudioProcessor::txInputLevels,
                    audioProcWin, &AudioProcessingWidget::updateInputLevels);
            connect(txProc, &TxAudioProcessor::txOutputLevels,
                    audioProcWin, &AudioProcessingWidget::updateOutputLevels);
            connect(txProc, &TxAudioProcessor::txGainReduction,
                    audioProcWin, &AudioProcessingWidget::updateGainReduction);
            connect(txProc, &TxAudioProcessor::txSpectrumBins,
                    audioProcWin, &AudioProcessingWidget::onSpectrumBins);
        }
    }
    audioProcWin->show();
    audioProcWin->raise();
    audioProcWin->activateWindow();
}

void wfmain::onAudioProcPrefsChanged(txAudioProcessingPrefs p)
{
    prefs.txAudioProc = p;
    applyAudioProcPrefs(p);
}

void wfmain::applyAudioProcPrefs(const txAudioProcessingPrefs& p)
{
    if (!txProc) return;

    txProc->setCompEnabled(p.compEnabled);
    txProc->setEqEnabled(p.eqEnabled);
    txProc->setEqFirst(p.eqFirst);
    txProc->setInputGainDB(p.inputGainDB);
    txProc->setOutputGainDB(p.outputGainDB);
    for (int i = 0; i < TxAudioProcessor::EQ_BANDS; ++i)
        txProc->setEqBand(i, p.eqBands[i]);
    txProc->setCompPeakLimit(p.compPeakLimit);
    txProc->setCompRelease(p.compRelease);
    txProc->setCompFastRatio(p.compFastRatio);
    txProc->setCompSlowRatio(p.compSlowRatio);
    txProc->setSidetoneEnabled(p.sidetoneEnabled);
    txProc->setSidetoneLevel(p.sidetoneLevel);
    txProc->setBypassed(p.bypass);
    txProc->setMuteRx(p.muteRx); // we do not restore this as it will only confuse people.
    txProc->setSpectrumEnabled(p.spectrumEnabled);
    txProc->setSpectrumFps(p.spectrumFPS);
    txProc->setGateEnabled(p.gateEnabled);
    txProc->setGateThreshold(p.gateThreshold);
    txProc->setGateAttack(p.gateAttack);
    txProc->setGateHold(p.gateHold);
    txProc->setGateDecay(p.gateDecay);
    txProc->setGateRange(p.gateRange);
    txProc->setGateLfCutoff(p.gateLfCutoff);
    txProc->setGateHfCutoff(p.gateHfCutoff);
}

void wfmain::on_RXaudioProcBtn_clicked()
{
    if (!rxAudioProcWin) {
        rxAudioProcWin = new RxAudioProcessingWidget(this);
        rxAudioProcWin->setDebugMode(debugMode);
        rxAudioProcWin->setPrefs(prefs.rxAudioProc);
        rxAudioProcWin->setAntiAlias(prefs.wfAntiAlias);

        connect(rxAudioProcWin, &RxAudioProcessingWidget::prefsChanged,
                this, &wfmain::onRxAudioProcPrefsChanged);

        connect(this, &wfmain::connectionStatus,
                rxAudioProcWin, &RxAudioProcessingWidget::setConnected);
        rxAudioProcWin->setConnected(connStatus == connConnected);

        if (rxProc) {
            connect(rxProc, &RxAudioProcessor::anrProfileReady,
                    rxAudioProcWin, &RxAudioProcessingWidget::onAnrProfileReady);
            connect(rxProc, &RxAudioProcessor::anrNoiseProfileBins,
                    rxAudioProcWin, &RxAudioProcessingWidget::onAnrNoiseProfileBins);
            connect(rxProc, &RxAudioProcessor::rxSpectrumBins,
                    rxAudioProcWin, &RxAudioProcessingWidget::onSpectrumBins);
            connect(rxProc, &RxAudioProcessor::anrModeProfileStatus,
                    rxAudioProcWin, &RxAudioProcessingWidget::onAnrModeChanged);
            connect(rxProc, &RxAudioProcessor::rxAudioChannelsChanged,
                    rxAudioProcWin, &RxAudioProcessingWidget::setAudioChannels);
            rxAudioProcWin->setAudioChannels(rxProc->activeChannels());
            // Deliver the current mode and noise profile to the newly-opened widget.
            if (!receivers.empty() && currentReceiver < (int)receivers.size()) {
                rxAudioProcWin->onAnrModeChanged(
                    receivers[currentReceiver]->currentMode().name,
                    rxProc->anrHasProfile());
                // Populate the noise floor plot if a profile is already active.
                auto pb = rxProc->getAnrNoiseProfileBins();
                if (!pb.bins.isEmpty())
                    rxAudioProcWin->onAnrNoiseProfileBins(pb.bins, pb.sampleRate, pb.windowSize);
            }
        }

        // ANR profile collection: widget toggle → wfmain → rxProc
        connect(rxAudioProcWin, &RxAudioProcessingWidget::anrCollectToggled,
                this, &wfmain::onAnrCollectToggled);

        // Debug audio capture: widget → rxProc, rxProc → widget
        if (rxProc) {
            connect(rxAudioProcWin, &RxAudioProcessingWidget::debugCaptureRequested,
                    rxProc, &RxAudioProcessor::startDebugCapture);
            connect(rxProc, &RxAudioProcessor::debugCaptureComplete,
                    rxAudioProcWin, &RxAudioProcessingWidget::onDebugCaptureComplete);
        }
    }
    rxAudioProcWin->show();
    rxAudioProcWin->raise();
    rxAudioProcWin->activateWindow();
}

void wfmain::onRxAudioProcPrefsChanged(rxAudioProcessingPrefs p)
{
    prefs.rxAudioProc = p;
    applyRxAudioProcPrefs(p);
}

void wfmain::applyRxAudioProcPrefs(const rxAudioProcessingPrefs& p)
{
    if (!rxProc) return;

    rxProc->setBypassed(p.bypass);
    rxProc->setNrEnabled(!p.bypass);   // NR active whenever bypass is off
    rxProc->setNrMode(p.nrMode);
    rxProc->setChannelSelect(p.channelSelect);
    rxProc->setSpeexSuppression(p.speexSuppression);
    rxProc->setSpeexBandsPreset(p.speexBandsPreset);
    rxProc->setSpeexFrameMs(p.speexFrameMs);
    rxProc->setSpeexAgc(p.speexAgc);
    rxProc->setSpeexAgcLevel(p.speexAgcLevel);
    rxProc->setSpeexAgcMaxGain(p.speexAgcMaxGain);
    rxProc->setSpeexVad(p.speexVad);
    rxProc->setSpeexVadProbStart(p.speexVadProbStart);
    rxProc->setSpeexVadProbCont(p.speexVadProbCont);
    rxProc->setSpeexSnrDecay(p.speexSnrDecay);
    rxProc->setSpeexNoiseUpdateRate(p.speexNoiseUpdateRate);
    rxProc->setSpeexPriorBase(p.speexPriorBase);
    rxProc->setAnrNoiseReductionDb(p.anrNoiseReductionDb);
    rxProc->setAnrSensitivity(p.anrSensitivity);
    rxProc->setAnrFreqSmoothing(p.anrFreqSmoothing);
    rxProc->setEqEnabled(p.eqEnabled);
    for (int i = 0; i < rxAudioProcessingPrefs::RX_EQ_BANDS; ++i) {
        rxProc->setEqBandGain(i, p.eqGain[i]);
        rxProc->setEqBandFreq(i, p.eqFreq[i]);
        rxProc->setEqBandQ(i, p.eqQ[i]);
    }
    rxProc->setOutputGainDB(p.outputGainDB);
    rxProc->setSpectrumEnabled(p.spectrumEnabled);
    rxProc->setSpectrumFps(p.spectrumFPS);
}

void wfmain::onAnrCollectToggled(bool collecting)
{
    if (!rxProc) return;
    if (collecting)
        rxProc->startAnrProfile();
    else
        rxProc->stopAnrProfile();  // emits anrProfileReady(bool) → widget
}

void wfmain::receiveValue(cacheItem val){

    uchar vfo=0;

    // Sometimes we can receive data before the rigCaps have been determined, so return if this happens:

    if (rigCaps == Q_NULLPTR)
    {
        qWarning(logSystem()) << "Data received before we have rigCaps(), aborting";
        return;
    }
    else if (val.receiver >= receivers.size())
    {
        qWarning(logSystem()) << "Data received for Radio/VFO that doesn't exist!" << val.receiver << "(" << funcString[val.command] << ")";
        return;
    }

    /* Certain radios (IC-9700) cannot provide direct access to Sub receiver
     * In this situation, set the receiver to currentReceiver so most commands received
     * work on this receiver only.
     *
     * This functionality has now been moved to the xxxxCommander class.
     */

    /*
    if (!rigCaps->hasCommand29 && val.receiver != currentReceiver)
    {
        switch (val.command) {
        case funcSelectedFreq: case funcSelectedMode: case funcUnselectedFreq: case funcUnselectedMode: case funcScopeMode: case funcScopeSpan:
        case funcScopeRef: case funcScopeHold: case funcScopeSpeed: case funcScopeRBW: case funcScopeVBW: case funcScopeCenterType: case funcScopeEdge:
           break;
        default:
            val.receiver=currentReceiver;
            break;
        }
    }
    */

// IC7610_EXT_MIRROR_BEGIN
    auto ext7610ValueApplies = [&](funcs f) -> bool {
        auto it = rigCaps->commands.find(f);
        if (it == rigCaps->commands.end())
            return false;
        return (!it->cmd29 || val.receiver == currentReceiver);
    };

    auto ext7610SetSpin = [&](const char *name, funcs f, int value) {
        if (!ext7610ValueApplies(f))
            return;
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w != Q_NULLPTR) {
            w->blockSignals(true);
            w->setValue(value);
            w->blockSignals(false);
        }
    };    auto ext7610SetMappedSpin = [&](const char *name,
                                    funcs f,
                                    int raw,
                                    int rawMin,
                                    int rawMax,
                                    int displayMin,
                                    int displayMax,
                                    int displayStep) {
        if (!ext7610ValueApplies(f) || rawMax == rawMin)
            return;

        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;

        int display =
                displayMin +
                static_cast<int>(
                    ((static_cast<qint64>(raw - rawMin) *
                      static_cast<qint64>(displayMax - displayMin)) +
                     ((rawMax - rawMin) / 2)) /
                    (rawMax - rawMin));

        if (display < displayMin)
            display = displayMin;
        if (display > displayMax)
            display = displayMax;

        if (displayStep > 1) {
            if (display >= 0)
                display = ((display + (displayStep / 2)) / displayStep) * displayStep;
            else
                display = -(((-display + (displayStep / 2)) / displayStep) * displayStep);
        }

        w->blockSignals(true);
        w->setValue(display);
        w->blockSignals(false);
    };
 auto ext7610SetCombo = [&](const char *name, funcs f, int value) {
        if (!ext7610ValueApplies(f))
            return;
        QComboBox *w = findChild<QComboBox*>(QString::fromLatin1(name));
        if (w != Q_NULLPTR) {
            const int index = w->findData(value);
            if (index >= 0) {
                w->blockSignals(true);
                w->setCurrentIndex(index);
                w->blockSignals(false);
            }
        }
    };

    auto ext7610SetCheck = [&](const char *name, funcs f, bool value) {
        if (!ext7610ValueApplies(f))
            return;
        QCheckBox *w = findChild<QCheckBox*>(QString::fromLatin1(name));
        if (w != Q_NULLPTR) {
            w->blockSignals(true);
            w->setChecked(value);
            w->blockSignals(false);
        }
    };

    // IC7610_V23_SET_RX_HELPERS
    auto set7610EnableWidget = [&](const char *name) {
        QWidget *w = findChild<QWidget*>(QString::fromLatin1(name));
        if (w != Q_NULLPTR)
            w->setEnabled(true);
    };

    auto set7610SpinDirect = [&](const char *name, funcs f, int value) {
        ext7610SetSpin(name, f, value);
        set7610EnableWidget(name);
    };

    auto set7610SpinMapped = [&](const char *name, funcs f, int raw,
                                 int rawMin, int rawMax,
                                 int displayMin, int displayMax) {
        ext7610SetMappedSpin(
                    name, f, raw, rawMin, rawMax,
                    displayMin, displayMax, 1);
        set7610EnableWidget(name);
    };

    auto set7610Tone = [&](const char *name, funcs f, int raw) {
        ext7610SetSpin(name, f, raw - 5);
        set7610EnableWidget(name);
    };

    auto set7610Combo = [&](const char *name, funcs f, int value) {
        ext7610SetCombo(name, f, value);
        set7610EnableWidget(name);
    };

    auto set7610Check = [&](const char *name, funcs f, bool value) {
        ext7610SetCheck(name, f, value);
        set7610EnableWidget(name);
    };
    switch (val.command)
    {
    case funcAGCTimeConstant:
        ext7610SetCombo("agcTcCombo", val.command, val.value.value<uchar>());
        break;
    case funcAudioPeakFilter:
        ext7610SetCombo("apfModeCombo", val.command, val.value.value<uchar>());
        break;
    case funcAPFLevel:
        ext7610SetMappedSpin("apfPositionSpin", val.command, val.value.value<uchar>(), 0, 255, -550, 550, 10);
        break;
    case funcAPFType:
        ext7610SetCombo("apfTypeCombo", val.command, val.value.value<uchar>());
        break;
    case funcAPFTypeLevel:
        ext7610SetSpin("apfAfLevelSpin", val.command, val.value.value<uchar>());
        break;
    case funcNRLevel:
        ext7610SetMappedSpin("nrLevelSpin", val.command, val.value.value<uchar>(), 8, 248, 0, 15, 1);
        break;
    case funcNBLevel:
        ext7610SetMappedSpin("nbLevelSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100, 1);
        break;
    case funcNBDepth:
        ext7610SetMappedSpin("nbDepthSpin", val.command, val.value.value<uchar>(), 0, 9, 1, 10, 1);
        break;
    case funcNBWidth:
        ext7610SetMappedSpin("nbWidthSpin", val.command, val.value.value<uchar>(), 0, 255, 1, 100, 1);
        break;
    case funcAutoNotch:
        ext7610SetCheck("autoNotchChk", val.command, val.value.value<bool>());
        ext7610SetCheck("mainAutoNotchChk", val.command, val.value.value<bool>()); // IC7610_V43_FINAL_MAIN_NOTCH_RX
        {
            QCheckBox *w =
                    findChild<QCheckBox*>(
                        QStringLiteral("mainAutoNotchChk"));
            if (w != Q_NULLPTR)
                w->setEnabled(true);
        }
        break;
    case funcManualNotch:
        ext7610SetCheck("manualNotchChk", val.command, val.value.value<bool>());
        ext7610SetCheck("mainManualNotchChk", val.command, val.value.value<bool>());
        {
            QCheckBox *w =
                    findChild<QCheckBox*>(
                        QStringLiteral("mainManualNotchChk"));
            if (w != Q_NULLPTR)
                w->setEnabled(true);
        }
        break;
    case funcNotchFilter:
        // IC7610_V23_NOTCH_RX
        // IC7610_V45_NOTCH_CENTER_RX
        // IC-7610 CI-V 14 0D: 0000=max CCW, 0128=center, 0255=max CW.
        // Use separate 128-step and 127-step halves so center is exactly 0%.
        {
            const int v45NotchRaw =
                    static_cast<int>(
                        val.value.value<uchar>());

            const int v45NotchPercent =
                    (v45NotchRaw <= 128)
                        ? qRound(
                              (static_cast<double>(v45NotchRaw) - 128.0) *
                              100.0 / 128.0)
                        : qRound(
                              (static_cast<double>(v45NotchRaw) - 128.0) *
                              100.0 / 127.0);

            ext7610SetSpin(
                    "notchPositionSpin",
                    val.command,
                    qBound(-100, v45NotchPercent, 100));

            ext7610SetSpin(
                    "mainNotchPositionSpin",
                    val.command,
                    qBound(-100, v45NotchPercent, 100));
            // IC7610_V46_NOTCH_RAW_TOOLTIP
            // Keep the human-friendly percent display, but expose the radio's
            // actual 14 0D value for field verification.
            const QString v46NotchTip =
                    tr("Manual Notch Position: CI-V raw %1; "
                       "0128 is documented center; display %2%")
                        .arg(v45NotchRaw, 4, 10, QChar('0'))
                        .arg(v45NotchPercent);

            QSpinBox *v46ExtNotch =
                    findChild<QSpinBox*>(
                        QStringLiteral("notchPositionSpin"));
            if (v46ExtNotch != Q_NULLPTR) {
                v46ExtNotch->setToolTip(v46NotchTip);
                // IC7610_V47_NOTCH_RX_LAST
                v46ExtNotch->setProperty(
                        "ic7610LastRxPercent",
                        qBound(-100, v45NotchPercent, 100));
            }
            qInfo(logRig()) << "IC7610 v47 notch RX: raw"
                            << v45NotchRaw << "percent"
                            << v45NotchPercent << "rx"
                            << val.receiver;

            QSpinBox *v46MainNotch =
                    findChild<QSpinBox*>(
                        QStringLiteral("mainNotchPositionSpin"));
            if (v46MainNotch != Q_NULLPTR) {
                v46MainNotch->setToolTip(v46NotchTip);
                v46MainNotch->setProperty(
                        "ic7610LastRxPercent",
                        qBound(-100, v45NotchPercent, 100));
            }
        }
        {
            QSpinBox *w =
                    findChild<QSpinBox*>(
                        QStringLiteral("mainNotchPositionSpin"));
            if (w != Q_NULLPTR)
                w->setEnabled(true);
        }
        break;
    case funcManualNotchWidth:
        ext7610SetCombo("manualNotchWidthCombo", val.command, val.value.value<uchar>());
        break;
    case funcAFMute:
        ext7610SetCheck("afMuteChk", val.command, val.value.value<bool>());
        break;
    case funcDigiSelShift:
        ext7610SetMappedSpin("digiSelShiftSpin", val.command, val.value.value<uchar>(), 0, 255, -100, 100, 1); // IC7610_V23_DIGISEL_RX
        break;
    case funcTwinPeakFilter:
        ext7610SetCheck("twinPeakChk", val.command, val.value.value<bool>());
        break;
    case funcDialLock:
        ext7610SetCheck("dialLockChk", val.command, val.value.value<bool>());
        break;
    case funcMainSubTracking:
        ext7610SetCheck("mainSubTrackingChk", val.command, val.value.value<bool>());
        break;
    case funcCompressorLevel:
        ext7610SetMappedSpin("compLevelSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 10, 1);
        break;
    case funcDriveGain:
        ext7610SetMappedSpin("driveGainSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100, 1);
        break;
    case funcVoxGain:
        ext7610SetMappedSpin("voxGainSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100, 1);
        break;
    case funcAntiVoxGain:
        ext7610SetMappedSpin("antiVoxGainSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100, 1);
        break;
    case funcVOXDelay:
        ext7610SetSpin("voxDelaySpin", val.command, val.value.value<uchar>());
        break;
    case funcVOXVoiceDelay:
        ext7610SetCombo("voxVoiceDelayCombo", val.command, val.value.value<uchar>());
        break;
    case funcBreakIn:
        ext7610SetCombo("breakInModeCombo", val.command, val.value.value<uchar>());
        break;
    case funcBreakInDelay:
        ext7610SetMappedSpin("breakInDelaySpin", val.command, val.value.value<uchar>(), 0, 255, 20, 130, 1);
        break;
    case funcSSBTXBandwidth:
        ext7610SetCombo("ssbTxBwCombo", val.command, val.value.value<uchar>());
        break;
    // IC7610_V23_SET_RX_CASES
    // IC7610_V27_SET_RX_CASES
    case funcACCOutputSelect:
        set7610Combo("setAccOutputSelect", val.command, val.value.value<uchar>());
        break;
    case funcACCAFOutputLevel:
        set7610SpinMapped("setAccAfOutputSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcACCIFOutputLevel:
        set7610SpinMapped("setAccIfOutputSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcUSBOutputSelect:
        set7610Combo("setUsbOutputSelect", val.command, val.value.value<uchar>());
        break;
    case funcUSBAFOutputLevel:
        set7610SpinMapped("setUsbAfOutputSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcUSBIFOutputLevel:
        set7610SpinMapped("setUsbIfOutputSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcLANOutputSelect:
        set7610Combo("setLanOutputSelect", val.command, val.value.value<uchar>());
        break;
    case funcREFInput:
        set7610Combo("setRefInputCombo", val.command, val.value.value<uchar>());
        break;    // IC7610_V37_SET_RX_CASES
    // IC7610_V39_RTTY_SET_RX_CASES
    case func7610ExternalKeypadRTTY:
        set7610Check("setExternalKeypadRttyChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610DecodeBaud:
        set7610Combo("setDecodeBaudCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYFFTAverage:
        set7610Combo("setRttyFftAvgCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYDecodeUSOS:
        set7610Check("setRttyDecodeUsosChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYNewLine:
        set7610Combo("setRttyNewLineCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYDiddle:
        set7610Combo("setRttyDiddleCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYTXUSOS:
        set7610Check("setRttyTxUsosChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYAutoCRLF:
        set7610Check("setRttyAutoCrlfChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYTimestamp:
        set7610Check("setRttyTimestampChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYTimestampTime:
        set7610Combo("setRttyTimestampTimeCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYTimestampFreq:
        set7610Check("setRttyTimestampFreqChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYDecodeLog:
        set7610Check("setRttyDecodeLogChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610RTTYLogFileType:
        set7610Combo("setRttyLogFileTypeCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610CIVTimeout:
        set7610Combo("setCivTimeoutCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYMarkFreq:
        set7610Combo("setRttyMarkCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYShiftWidth:
        set7610Combo("setRttyShiftCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610RTTYKeyPolarity:
        set7610Combo("setRttyPolarityCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610SSBCWSyncTune:
        set7610Check("setCwSyncChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610CWNormalSide:
        set7610Combo("setCwNormalSideCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610USBKeyCW:
        set7610Combo("setUsbKeyCwCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610USBKeyRTTY:
        set7610Combo("setUsbKeyRttyCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610CIVRemoteAddress:
        set7610Combo("setCivRemoteAddrCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610CIVUSBPort:
        set7610Combo("setCivUsbPortCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610CIVUSBEcho:
        set7610Check("setCivUsbEchoChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610SideToneLevel:
        set7610SpinMapped(
            "setSideToneLevelSpin", val.command,
            val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case func7610SideToneLimit:
        set7610Check("setSideToneLimitChk", val.command,
                     val.value.value<uchar>() != 0);
        break;
    case func7610KeyerRepeat:
        set7610SpinDirect(
            "setKeyerRepeatSpin", val.command,
            val.value.value<uchar>());
        break;
    case func7610CWRiseTime:
        set7610Combo("setCwRiseCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610PaddlePolarity:
        set7610Combo("setPaddlePolarityCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610KeyType:
        set7610Combo("setKeyTypeCombo", val.command,
                     val.value.value<uchar>());
        break;
    case func7610MicUpDownKeyer:
        set7610Check("setMicUpDownKeyerChk", val.command,
                     val.value.value<uchar>() != 0);
        break;    case funcQuickDualWatch:
        set7610Check("setQuickDualChk", val.command, val.value.value<bool>());
        break;
    case funcQuickSplit:
        set7610Check("setQuickSplitChk", val.command, val.value.value<uchar>() != 0);
        break;
    case funcCIVTransceive:
        set7610Check("setCivTransceiveChk", val.command, val.value.value<bool>());
        break;
    case funcCIVOutput:
        set7610Check("setCivOutputChk", val.command, val.value.value<bool>());
        break;
    case funcDashRatio:
        set7610SpinDirect("setDashRatioSpin", val.command, val.value.value<uchar>());
        break;
    case funcACCAModLevel:
        set7610SpinMapped("setAccModSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcUSBModLevel:
        set7610SpinMapped("setUsbModSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcLANModLevel:
        set7610SpinMapped("setLanModSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcSSBRXBass:
        set7610Tone("setSsbRxBass", val.command, val.value.value<uchar>());
        break;
    case funcSSBRXTreble:
        set7610Tone("setSsbRxTreble", val.command, val.value.value<uchar>());
        break;
    case funcAMRXBass:
        set7610Tone("setAmRxBass", val.command, val.value.value<uchar>());
        break;
    case funcAMRXTreble:
        set7610Tone("setAmRxTreble", val.command, val.value.value<uchar>());
        break;
    case funcFMRXBass:
        set7610Tone("setFmRxBass", val.command, val.value.value<uchar>());
        break;
    case funcFMRXTreble:
        set7610Tone("setFmRxTreble", val.command, val.value.value<uchar>());
        break;
    case funcSSBTXBass:
        set7610Tone("setSsbTxBass", val.command, val.value.value<uchar>());
        break;
    case funcSSBTXTreble:
        set7610Tone("setSsbTxTreble", val.command, val.value.value<uchar>());
        break;
    case funcAMTXBass:
        set7610Tone("setAmTxBass", val.command, val.value.value<uchar>());
        break;
    case funcAMTXTreble:
        set7610Tone("setAmTxTreble", val.command, val.value.value<uchar>());
        break;
    case funcFMTXBass:
        set7610Tone("setFmTxBass", val.command, val.value.value<uchar>());
        break;
    case funcFMTXTreble:
        set7610Tone("setFmTxTreble", val.command, val.value.value<uchar>());
        break;
    case funcBeepLevel:
        set7610SpinMapped("setBeepLevelSpin", val.command, val.value.value<uchar>(), 0, 255, 0, 100);
        break;
    case funcBeepLevelLimit:
        set7610Check("setBeepLimitChk", val.command, val.value.value<bool>());
        break;
    case funcBeepConfirmation:
        set7610Check("setBeepConfirmChk", val.command, val.value.value<bool>());
        break;
    case funcBandEdgeBeep:
        set7610Combo("setBandEdgeBeepCombo", val.command, val.value.value<uchar>());
        break;
    case funcRFSQLControl:
        set7610Combo("setRfSqlCombo", val.command, val.value.value<uchar>());
        break;
    case funcTXDelayHF:
        set7610Combo("setTxDelayHfCombo", val.command, val.value.value<uchar>());
        break;
    case funcTXDelay50m:
        set7610Combo("setTxDelay50Combo", val.command, val.value.value<uchar>());
        break;    default:
        break;
    }
    // IC7610_EXT_MIRROR_END
    switch (val.command)
    {
#if defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
    case funcUnselectedFreq:
        vfo = 1;
    case funcFreqGet:
    case funcFreqTR:
    case funcFreq:
    case funcSelectedFreq:
        receivers[val.receiver]->setFrequency(val.value.value<freqt>(),vfo);
        if (val.receiver==0 || vfo == 0)
            rpt->handleUpdateCurrentMainFrequency(val.value.value<freqt>());
        break;
    case funcModeGet:
    case funcModeTR:
        // These commands don't include filter, so queue an immediate request for filter
        queue->addUnique(priorityImmediate,funcDataModeWithFilter,false,0);
    case funcDataModeWithFilter:
    case funcUnselectedMode:
        if (val.command == funcUnselectedMode)
            vfo=1;
    case funcDataMode:
    case funcSelectedMode:
    case funcMode:
    {
        modeInfo m = val.value.value<modeInfo>();
        receivers[val.receiver]->receiveMode(m,vfo);
        // We are ONLY interested in VFOA
        if (val.receiver == currentReceiver && vfo == 0) {
            finputbtns->updateCurrentMode(m.mk);
            finputbtns->updateFilterSelection(m.filter);
            rpt->handleUpdateCurrentMainMode(m);
            if (cw != Q_NULLPTR) {
                cw->handleCurrentModeUpdate(m.mk);
            }
            // Notify the ANR engine so it can swap in the saved noise profile
            // for this mode (or save the current one before switching away).
            if (rxProc && !m.name.isEmpty())
                rxProc->setRxMode(m.name);
        }
        //qDebug() << funcString[val.command] << "receiver:" << val.receiver << "vfo:" << vfo << "mk:" << m.mk << "name:" << m.name << "data:" << m.data << "filter:" << m.filter;

        break;
    }
    case funcTXFreq:
        // Not sure if we want to do anything with this? M0VSE
        break;
    case funcVFODualWatch:        
        if (receivers.size()>1) // How can it not be?
        {
            ui->dualWatchBtn->blockSignals(true);
            ui->dualWatchBtn->setChecked(val.value.value<bool>());
            ui->dualWatchBtn->blockSignals(false);
        }
        break;
#if defined __GNUC__
#pragma GCC diagnostic pop
#endif
    case funcVFOBandMS:
    {
        // This indicates whether main or sub is currently "active"
        // Swap recievers if necessary.                
        uchar r = val.value.value<uchar>();
        if (currentReceiver != r)
        {
            // VFO has swapped, make sure scope follows if we only have a single scope.
            // Th radio doesn't do this, but I don't see why we would want it without a scope?
            if (!rigCaps->hasCommand29 && rigCaps->commands.contains(funcScopeMainSub))
                queue->add(priorityImmediate,queueItem(funcScopeMainSub,val.value,false,0));

            for (const auto& rx: receivers)
            {
                rx->selected(rx->getReceiver() == r);
                rx->setVisible(rx->isSelected() || ui->scopeDualBtn->isChecked());
                rx->updateInfo();
            }
            currentReceiver = r;
            // IC7610_EXT_RECEIVER_READ_BEGIN
            QLabel *ext7610ReceiverLabel =
                    findChild<QLabel*>(QStringLiteral("ext7610ReceiverLabel"));
            if (ext7610ReceiverLabel != Q_NULLPTR) {
                ext7610ReceiverLabel->setText(
                            currentReceiver ? tr("Receiver: SUB") : tr("Receiver: MAIN"));
            }

            auto ext7610ReceiverRead = [=](funcs f) {
                auto it = rigCaps->commands.find(f);
                if (it == rigCaps->commands.end() || !it->getCmd)
                    return;
                const uchar target = it->cmd29 ? currentReceiver : 0;
                queue->add(priorityImmediate, f, false, target);
            };

            ext7610ReceiverRead(funcAGC);
            ext7610ReceiverRead(funcAGCTimeConstant);
            ext7610ReceiverRead(funcAudioPeakFilter);
            ext7610ReceiverRead(funcAPFLevel);
            ext7610ReceiverRead(funcAPFType);
            ext7610ReceiverRead(funcAPFTypeLevel);
            ext7610ReceiverRead(funcNRLevel);
            ext7610ReceiverRead(funcNBLevel);
            ext7610ReceiverRead(funcNBDepth);
            ext7610ReceiverRead(funcNBWidth);
            ext7610ReceiverRead(funcAutoNotch);
            ext7610ReceiverRead(funcManualNotch);
            ext7610ReceiverRead(funcNotchFilter);
            ext7610ReceiverRead(funcManualNotchWidth);
            ext7610ReceiverRead(funcAFMute);
            ext7610ReceiverRead(funcDigiSel);
            ext7610ReceiverRead(funcDigiSelShift);
            ext7610ReceiverRead(funcTwinPeakFilter);
            ext7610ReceiverRead(funcDialLock);
            ext7610ReceiverRead(funcMainSubTracking);
            ext7610ReceiverRead(funcCompressorLevel);
            ext7610ReceiverRead(funcDriveGain);
            ext7610ReceiverRead(funcVoxGain);
            ext7610ReceiverRead(funcAntiVoxGain);
            ext7610ReceiverRead(funcVOXDelay);
            ext7610ReceiverRead(funcVOXVoiceDelay);
            ext7610ReceiverRead(funcBreakIn);
            ext7610ReceiverRead(funcBreakInDelay);
            ext7610ReceiverRead(funcSSBTXBandwidth);
            // IC7610_EXT_RECEIVER_READ_END
        }
        break;
    }
    case funcSatelliteMode:
        // If satellite mode is enabled, disable mode/freq query commands.
        for (auto r: receivers){
            if (val.value.value<bool>()) {
                queue->del(funcUnselectedMode,r->getReceiver());
                queue->del(funcUnselectedFreq,r->getReceiver());
            }
            r->setSatMode(val.value.value<bool>());
        }
        //qInfo(logRig()) << "Is radio currently in satellite mode?" << val.value.value<bool>();
        break;
    case funcSatelliteMemory:
    case funcMemoryContents:
        emit haveMemory(val.value.value<memoryType>());
        break;
    case funcMemoryTag:
    case funcMemoryTagB:
        emit haveMemoryName(val.value.value<memoryTagType>());
        break;
    case funcSplitMemory:
        emit haveMemorySplit(val.value.value<memorySplitType>());
        break;
    case funcMemoryClear:
    case funcMemoryKeyer:
    case funcMemoryToVFO:
    case funcMemoryWrite:
        break;
    case funcScanning:
        break;
    case funcReadFreqOffset:
        break;
    case funcSplitStatus:
        ui->splitBtn->setChecked(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        rpt->receiveDuplexMode(val.value.value<duplexMode_t>());
        receivers[val.receiver]->setSplit(val.value.value<duplexMode_t>()==dmSplitOn?true:false);
        break;
    case funcQuickSplit:
        rpt->receiveQuickSplit(val.value.value<bool>());
        break;
    case funcTuningStep:
        receiveTuningStep(val.value.value<uchar>());
        break;
    case funcAttenuator:
        receiveAttenuator(val.value.value<uchar>(),val.receiver);
        break;
    case funcAntenna:
        if (val.receiver == currentReceiver) {
            ui->antennaSelCombo->setCurrentIndex(ui->antennaSelCombo->findData(val.value.value<antennaInfo>().antenna));
            ui->rxAntennaCheck->setChecked(val.value.value<antennaInfo>().rx);
        }
        break;
    case funcPBTOuter:

        receivers[val.receiver]->setPBTOuter(val.value.value<uchar>());
        break;
    case funcPBTInner:
    {
        receivers[val.receiver]->setPBTInner(val.value.value<uchar>());
        break;
    }
    case funcIFShift:
        receivers[val.receiver]->setIFShift(val.value.value<uchar>());
        break;
        /*
    connect(this->rig, &rigCommander::haveDashRatio,
        [=](const quint8& ratio) { cw->handleDashRatio(ratio); });
    connect(this->rig, &rigCommander::haveCWBreakMode,
            [=](const quint8 &bm) { cw->handleBreakInMode(bm);});
*/
    case funcCwPitch:
        // There is only a single CW Pitch setting, so send to all scopes
        for (const auto& receiver: receivers) {
            receiver->receiveCwPitch(val.value.value<quint16>());
        }
        // Also send to CW window
        if (cw != Q_NULLPTR) {
            cw->handlePitch(val.value.value<quint16>());
        }
        // IC7610_V49_CW_DECODER_PITCH
        if (auto *cwDec = findChild<Ic7610CwDecoderWindow*>(
                    QStringLiteral("ic7610CwDecoderWin"), Qt::FindDirectChildrenOnly)) {
            cwDec->setRadioPitch(static_cast<int>(val.value.value<quint16>()));
        }
        break;

    case funcMicGain:
        processModLevel(inputMic,val.value.value<uchar>());
        break;
    case funcKeySpeed:
        // Only used by CW window
        if (cw != Q_NULLPTR) {
            cw->handleKeySpeed(val.value.value<uchar>());
        }
        break;
    case funcNotchFilter:
        break;
    case funcAfGain:
        if (val.receiver == currentReceiver)
            changeSliderQuietly(ui->afGainSlider, val.value.value<uchar>());
        break;
    case funcMonitorGain:
        changeSliderQuietly(ui->monitorSlider, val.value.value<uchar>());
        break;
    case funcRfGain:
        if (val.receiver == currentReceiver)
            changeSliderQuietly(ui->rfGainSlider, val.value.value<uchar>());
        break;
    case funcSquelch:
        if (val.receiver == currentReceiver) {
            changeSliderQuietly(ui->sqlSlider, val.value.value<uchar>());
        }
        break;
    case funcRFPower:
        changeSliderQuietly(ui->txPowerSlider, val.value.value<uchar>());
        break;
    case funcCompressorLevel:
    case funcNBLevel:
    case funcNRLevel:
    case funcAPFLevel:
    case funcDriveGain:
    case funcVoxGain:
    case funcAntiVoxGain:
        break;
    case funcBreakInDelay:
        break;
    case funcDigiSelShift:
        break;
    // 0x15 Meters
    case funcSMeterSqlStatus:
        break;
    case funcSMeter:
        if (val.receiver ) {
            receiveMeter(meter_t::meterSubS,val.value.value<double>());
        }
        else {
            receiveMeter(meter_t::meterS,val.value.value<double>());
        }
        break;
    case funcAbsoluteMeter:
    {
        meterkind m = val.value.value<meterkind>();
        if (ui->meterSPoWidget->getMeterType() != m.type) {

            changeMeterType(m.type,1);
        }
        ui->meterSPoWidget->setLevel(m.value);
        break;
    }
    case funcMeterType:
    {
        meter_t m = val.value.value<meter_t>();
        if (ui->meterSPoWidget->getMeterType() != m)
            changeMeterType(m, 1);
        break;

    }
    case funcVariousSql:
        break;
    case funcOverflowStatus:
        receivers[val.receiver]->overflow(val.value.value<bool>());
        break;
    case funcCenterMeter:
        receiveMeter(meter_t::meterCenter,val.value.value<double>());
        break;
    case funcPowerMeter:
        receiveMeter(meter_t::meterPower,val.value.value<double>());
        break;
    case funcSWRMeter:
        receiveMeter(meter_t::meterSWR,val.value.value<double>());
        break;
    case funcALCMeter:
        receiveMeter(meter_t::meterALC,val.value.value<double>());
        break;
    case funcCompMeter:
        receiveMeter(meter_t::meterComp,val.value.value<double>());
        break;
    case funcVdMeter:
        receiveMeter(meter_t::meterVoltage,val.value.value<double>());
        // IC7610_V30_VA_RECEIVE
        {
            const double vd = val.value.value<double>();
            QProgressBar *bar =
                    findChild<QProgressBar*>(QStringLiteral("ic7610VdBar"));
            QLabel *text =
                    findChild<QLabel*>(QStringLiteral("ic7610VdText"));
            if (bar != Q_NULLPTR)
                bar->setValue(qBound(100, qRound(vd * 10.0), 160));
            // IC7610_V35_VD_COLOR
            if (bar != Q_NULLPTR) {
                QString color;
                if ((vd < 11.9) || (vd >= 15.4))
                    color = QStringLiteral("#d83b3b");
                else if ((vd < 12.2) || (vd > 15.18))
                    color = QStringLiteral("#d49a00");
                else
                    color = QStringLiteral("#2e9d50");

                bar->setStyleSheet(
                    QStringLiteral(
                        "QProgressBar::chunk { background-color: %1; }")
                    .arg(color));
            } // IC7610_V32_VD_SCALE
            if (text != Q_NULLPTR)
                text->setText(QStringLiteral("%1 V").arg(vd, 0, 'f', 1));
        }        break;
    case funcIdMeter:
        receiveMeter(meter_t::meterCurrent,val.value.value<double>());
        {
            const double amps = val.value.value<double>();
            QProgressBar *bar =
                    findChild<QProgressBar*>(QStringLiteral("ic7610IdBar"));
            QLabel *text =
                    findChild<QLabel*>(QStringLiteral("ic7610IdText"));
            if (bar != Q_NULLPTR)
                bar->setValue(qBound(0, qRound(amps * 10.0), 300));
            // IC7610_V35_ID_COLOR
            if (bar != Q_NULLPTR) {
                const QString color =
                    (amps <= 23.0)
                    ? QStringLiteral("#2e9d50")
                    : QStringLiteral("#d49a00");

                bar->setStyleSheet(
                    QStringLiteral(
                        "QProgressBar::chunk { background-color: %1; }")
                    .arg(color));
            }
            if (text != Q_NULLPTR)
                text->setText(QStringLiteral("%1 A").arg(amps, 0, 'f', 1));
        }        break;
    // 0x16 enable/disable functions:
    case funcPreamp:
        receivePreamp(val.value.value<uchar>(),val.receiver);
        break;
    case funcAGC:
    {
        QComboBox *agcSelCombo = findChild<QComboBox*>(QStringLiteral("agcSelCombo"));
        if (agcSelCombo != Q_NULLPTR && val.receiver == currentReceiver) {
            const int agc = static_cast<int>(val.value.value<uchar>());
            const int index = agcSelCombo->findData(agc);
            if (index >= 0) {
                agcSelCombo->blockSignals(true);
                agcSelCombo->setCurrentIndex(index);
                agcSelCombo->blockSignals(false);
            }
        }
        // IC7610_V23_AGC_RECEIVER_REPLY
        if (val.receiver < receivers.size()) {
            receivers[val.receiver]->setIC7610AGC(
                        val.value.value<uchar>());
        }
        break;
    }
    case funcAGCTimeConstant:
        break;
    case funcNoiseBlanker:
        if (val.receiver == currentReceiver) {
            ui->nbEnableChk->blockSignals(true);
            if (ui->nbEnableChk->isTristate())
                ui->nbEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nbEnableChk->setChecked(val.value.value<bool>());

            ui->nbEnableChk->blockSignals(false);
        }
        break;
    case funcAudioPeakFilter:
        break;
    case funcFilterShape:
        if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->currentFilter() == val.value.value<uchar>() / 10)
        {
            receivers[val.receiver]->setFilterShape(val.value.value<uchar>() % 10);
        }
        break;
    case funcRoofingFilter:
        if (rigCaps->manufacturer == manufIcom || receivers[val.receiver]->currentFilter() == val.value.value<uchar>() / 10)
        {
            receivers[val.receiver]->setRoofing(val.value.value<uchar>() % 10);
        }
        break;
    case funcNoiseReduction:
        if (val.receiver == currentReceiver) {
            ui->nrEnableChk->blockSignals(true);
            if (ui->nrEnableChk->isTristate())
                ui->nrEnableChk->setCheckState(Qt::CheckState(val.value.value<uchar>()));
            else
                ui->nrEnableChk->setChecked(val.value.value<bool>());

            ui->nrEnableChk->blockSignals(false);
        }
        break;
    case funcAutoNotch:
        break;
    case funcRepeaterTone:
        rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTONEon:ratrTONEoff));
        break;
    case funcRepeaterTSQL:
        rpt->handleRptAccessMode(rptAccessTxRx_t((val.value.value<bool>())?ratrTSQLon:ratrTSQLoff));
        break;
    case funcRepeaterDTCS:
        break;
    case funcRepeaterCSQL:
        break;
    case funcCompressor:
        if (val.receiver == currentReceiver) {
            ui->compEnableChk->setChecked(val.value.value<bool>());
        }
        break;
    case funcMonitor:
        receiveMonitor(val.value.value<bool>());
        break;
    case funcVox:
        ui->voxEnableChk->setChecked(val.value.value<bool>());
        break;
    case funcManualNotch:
        break;
    case funcDigiSel:
        ui->digiselEnableChk->setChecked(val.value.value<bool>());
        ui->preampSelCombo->setEnabled(!val.value.value<bool>());
        break;
    case funcTwinPeakFilter:
        break;
    case funcDialLock:
        break;
    case funcRXAntenna:
        ui->rxAntennaCheck->setChecked(val.value.value<bool>());
        break;
    case funcManualNotchWidth:
        break;
    case funcSSBTXBandwidth:
        break;
    case funcMainSubTracking:

        //ui->mainSubTrackingBtn->setChecked(val.value.value<bool>());
        //for (const auto& receiver:receivers)
        //{
        //    receiver->setTracking(val.value.value<bool>());
        //}
        break;
    case funcToneSquelchType:
        break;
    case funcIPPlus:
        if (val.receiver == currentReceiver) {
            ui->ipPlusEnableChk->setChecked(val.value.value<bool>());
        }
        break;
    case funcBreakIn:
        if (cw != Q_NULLPTR) {
            cw->handleBreakInMode(val.value.value<uchar>());
        }
        break;
    // 0x17 is CW send and 0x18 is power control (no reply)
    // 0x19 it automatically added.
    case funcTransceiverId:
        break;
    // 0x1a
    case funcBandStackReg:
    {
        bandStackType bsr = val.value.value<bandStackType>();
        qDebug(logRig()) << __func__ << "BSR received into" << val.receiver << "Freq:" << bsr.freq.Hz << ", mode: " << bsr.mode << ", filter: " << bsr.filter << ", data mode: " << bsr.data;

        queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).freqFunc,
                                                QVariant::fromValue<freqt>(bsr.freq),false,uchar(val.receiver)));

        for (auto &md: rigCaps->modes)
        {
            if (md.reg == bsr.mode) {
                modeInfo m(md);
                m.filter=bsr.filter;
                m.data=bsr.data;
                qDebug(logRig()) << __func__ << "Setting Mode/Data for new mode" << m.name << "data" << m.data << "filter" << m.filter << "reg" << m.reg;
                queue->add(priorityImmediate,queueItem(queue->getVfoCommand(vfoA,val.receiver,true).modeFunc,
                                                        QVariant::fromValue<modeInfo>(m),false,uchar(val.receiver)));
                break;
            }
        }
        break;
    }
    case funcFilterWidth:
        receivers[val.receiver]->receivePassband(val.value.value<ushort>());
        break;

    case funcAFMute:
        break;
    // 0x1a 0x05 various registers!
    case funcREFAdjust:
        break;
    case funcREFAdjustFine:
        //break;
    case funcACCAModLevel:
        processModLevel(inputACCA,val.value.value<uchar>());
        break;
    case funcACCBModLevel:
        processModLevel(inputACCB,val.value.value<uchar>());
        break;
    case funcUSBModLevel:
        processModLevel(inputUSB,val.value.value<uchar>());
        break;
    case funcSPDIFModLevel:
        processModLevel(inputSPDIF,val.value.value<uchar>());
        break;
    case funcLANModLevel:
        processModLevel(inputLAN,val.value.value<uchar>());
        break;
    case funcDATAOffMod:
        receiveModInput(val.value.value<rigInput>(), 0);
        break;
    case funcDATA1Mod:
        receiveModInput(val.value.value<rigInput>(), 1);
        break;
    case funcDATA2Mod:
        receiveModInput(val.value.value<rigInput>(), 2);
        break;
    case funcDATA3Mod:
        receiveModInput(val.value.value<rigInput>(), 3);
        break;
    case funcDashRatio:
        if (cw != Q_NULLPTR) {
            cw->handleDashRatio(val.value.value<uchar>());
        }
        break;
    case funcTXFreqMon:
        break;
    // 0x1b register
    case funcToneFreq:
        rpt->handleTone(val.value.value<toneInfo>().tone);
        break;
    case funcTSQLFreq:
        rpt->handleTSQL(val.value.value<toneInfo>().tone);
        break;
    case funcDTCSCode:
    {
        toneInfo t = val.value.value<toneInfo>();
        rpt->handleDTCS(t.tone,t.rinv,t.tinv);
        break;
    }
    case funcCSQLCode:
        break;
    // 0x1c register        
    case funcRitStatus:
        receiveRITStatus(val.value.value<bool>());
        break;
    case funcTransceiverStatus:
        receivePTTstatus(val.value.value<bool>());
        break;
    case funcTunerStatus:
        receiveATUStatus(val.value.value<uchar>());
        break;
    // 0x21 RIT:
    case funcRitFreq:
        ui->ritTuneDial->blockSignals(true);
        ui->ritTuneDial->setValue(val.value.value<short>());
        ui->ritTuneDial->blockSignals(false);
        break;
    case funcRitTXStatus:
        // Not sure what this is used for?
        break;
    // 0x27
    case funcScopeWaveData:
        receivers[val.receiver]->updateScope(val.value.value<scopeData>());
        break;
    case funcScopeOnOff:
        // confirming scope is on
        break;
    case funcScopeDataOutput:
        // confirming output enabled/disabled of wf data.
        break;
    case funcScopeMainSub:
    {
        // Has the primary scope changed?
        break;
    }
    case funcScopeSingleDual:
    {
        if (receivers.size()>1)
        {
            if (QThread::currentThread() != QCoreApplication::instance()->thread())
            {
                qCritical(logSystem()) << "Thread is NOT the main UI thread, cannot hide/unhide VFO";
            } else {
                // This tells us whether we are receiving single or dual scopes
                ui->scopeDualBtn->blockSignals(true);
                ui->scopeDualBtn->setChecked(val.value.value<bool>());
                ui->scopeDualBtn->blockSignals(false);
                for (const auto& rx: receivers) {
                    if (!rx->isSelected()) {
                        rx->setVisible(val.value.value<bool>());
                    }
                }
            }
        }
        break;
    }
    case funcScopeMode:
        //qDebug() << "Got new scope mode for receiver" << val.receiver << "mode" << val.value.value<uchar>();
        receivers[val.receiver]->setScopeMode(val.value.value<uchar>());
        break;
    case funcScopeSpan:
        receivers[val.receiver]->setSpan(val.value.value<centerSpanData>());
        break;
    case funcScopeEdge:
        receivers[val.receiver]->setEdge(val.value.value<uchar>());
        break;
    case funcScopeHold:
        receivers[val.receiver]->setHold(val.value.value<bool>());
        break;
    case funcScopeRef:
        receivers[val.receiver]->setRef(val.value.value<int>());
        break;
    case funcScopeSpeed:
        receivers[val.receiver]->setSpeed(val.value.value<uchar>());
        break;
    case funcScopeDuringTX:
    case funcScopeCenterType:
    case funcScopeVBW:
    case funcScopeFixedEdgeFreq:
    case funcScopeRBW:
        break;
    // 0x28
    case funcVoiceTX:
        break;
    //0x29 - Prefix certain commands with this to get/set certain values without changing current VFO
    // If we use it for get commands, need to parse the \x29\x<VFO> first.
    case funcMainSubPrefix:
        break;
    case funcPowerControl:
        // We could indicate the rig being powered-on somehow?
        break;
    case funcCWDecode:
        cw->receive(val.value.value<QString>());
    default:
        //qWarning(logSystem()) << "Unhandled command received from rigcommander()" << funcString[val.command] << "Contact support!";
        break;
    }
}


void wfmain::on_showBandsBtn_clicked()
{
    showAndRaiseWidget(bandbtns);
}

void wfmain::on_showFreqBtn_clicked()
{
    showAndRaiseWidget(finputbtns);
}

void wfmain::on_showSettingsBtn_clicked()
{
    showAndRaiseWidget(setupui);
}

void wfmain::on_scopeMainSubBtn_clicked()
{
    if (rigCaps->commands.contains(funcVFOBandMS)) {
        queue->add(priorityImmediate,queueItem(funcVFOBandMS,QVariant::fromValue<uchar>(currentReceiver==0),false,0));
    }

 }

void wfmain::on_scopeDualBtn_toggled(bool en)
{
    if (rigCaps->commands.contains(funcScopeSingleDual))
    {
        queue->add(priorityImmediate,queueItem(funcScopeSingleDual,QVariant::fromValue(en),false,0));
        queue->add(priorityImmediate,funcScopeMainSub,false,0);
    } else {
        // As the IC9700 doesn't have a single/dual command, pretend it does!
        for (const auto& rx: receivers) {
            if (rx->getReceiver() != currentReceiver) {
                rx->selected(false);
                rx->setVisible(en);
            }
        }
    }
}

void wfmain::on_dualWatchBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcVFODualWatch,QVariant::fromValue(en),false,0));
    if (!rigCaps->hasCommand29)
        queue->add(priorityImmediate,funcScopeMainSub,false,0);

}

void wfmain::on_swapMainSubBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOSwapMS);
    for (const auto &receiver: receivers)
    {        
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
    }
}

/*
void wfmain::on_mainSubTrackingBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcMainSubTracking,QVariant::fromValue(en),false));
}
*/

void wfmain::on_splitBtn_toggled(bool en)
{
    queue->add(priorityImmediate,queueItem(funcSplitStatus,QVariant::fromValue(en),false));
}

void wfmain::on_mainEqualsSubBtn_clicked()
{
    queue->add(priorityImmediate,funcVFOEqualMS);
    for (const auto &receiver: receivers)
    {
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoA,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).modeFunc,false,receiver->getReceiver());
        queue->add(priorityImmediate,queue->getVfoCommand(vfoB,receiver->getReceiver(),true).freqFunc,false,receiver->getReceiver());
    }
}

void wfmain::dataModeChanged(modeInfo m)
{
    // As the data mode may have changed, we need to make sure that we invalidate the input selection.
    // Also request the current input from the rig.
    //qInfo(logSystem()) << "*** DATA MODE HAS CHANGED ***";
    if (m.data != 0xff) {
        currentModSrc[m.data] = rigInput();

        // Request the current inputSource.
        switch(m.data)
        {
        case 0:
            queue->add(priorityImmediate,funcDATAOffMod,false,false);
            break;
        case 1:
            queue->add(priorityImmediate,funcDATA1Mod,false,false);
            break;
        case 2:
            queue->add(priorityImmediate,funcDATA2Mod,false,false);
            break;
        case 3:
            queue->add(priorityImmediate,funcDATA3Mod,false,false);
            break;
        }
    }

}

void wfmain::receiveScopeSettings(uchar receiver, int theme, quint16 len, int floor, int ceiling)
{
    if (receiver) {
        prefs.subWfTheme = theme;
        prefs.subWflength = len;
        prefs.subPlotFloor = floor;
        prefs.subPlotCeiling = ceiling;
    }
    else
    {
        prefs.mainWfTheme = theme;
        prefs.mainWflength = len;
        prefs.mainPlotFloor = floor;
        prefs.mainPlotCeiling = ceiling;
    }
}


void wfmain::receiveElapsed(bool sub, qint64 us)
{
    if (sub)
        subElapsed = us;
    else
        mainElapsed = us;
}

void wfmain::receiveRigCaps(rigCapabilities* caps)
{
    this->rigCaps = caps;

    // Convenient place to stop the connection timer as rigCaps has changed.
    ConnectionTimer.stop();

    //bandbtns->acceptRigCaps(rigCaps);

    if(caps == Q_NULLPTR)
    {
        // Note: This line makes it difficult to accept a different radio connecting.
        return;
    } else {

        // Enable all controls
        enableControls(true);

        if (prefs.manufacturer == manufIcom)
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID,2,16,QChar('0')));
        else
            showStatusBarText(QString("Found radio of name %0 and model ID %1.").arg(rigCaps->modelName).arg(rigCaps->modelID));

        qDebug(logSystem()) << "Rig name: " << rigCaps->modelName;
        qDebug(logSystem()) << "Has LAN capabilities: " << rigCaps->hasLan;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectLenMax: " << rigCaps->spectLenMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectAmpMax: " << rigCaps->spectAmpMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: spectSeqMax: " << rigCaps->spectSeqMax;
        qDebug(logSystem()) << "Rig ID received into wfmain: hasSpectrum: " << rigCaps->hasSpectrum;

        configureVFOs(); // Now we have a rig connection, need to configure the VFOs

        rigName->setText(rigCaps->modelName);
        if (serverConfig.enabled) {
            serverConfig.rigs.first()->modelName = rigCaps->modelName;
            serverConfig.rigs.first()->rigName = rigCaps->modelName;
            serverConfig.rigs.first()->civAddr = rigCaps->modelID;
            serverConfig.rigs.first()->baudRate = rigCaps->baudRate;
        }
        setWindowTitle(rigCaps->modelName);

        if(rigCaps->hasSpectrum)
        {
            for (const auto& receiver: receivers)
            {
                receiver->prepareScope(rigCaps->spectAmpMax, rigCaps->spectLenMax);
            }
        }

        ui->transmitBtn->setEnabled(rigCaps->hasTransmit);
        ui->micGainSlider->setEnabled(rigCaps->hasTransmit);
        ui->txPowerSlider->setEnabled(rigCaps->hasTransmit);
        const bool audioProcAvail = rigCaps->hasTransmit && prefs.enableLAN;
        ui->TXaudioProcBtn->setEnabled(audioProcAvail);
        if (!audioProcAvail && audioProcWin) {
            audioProcWin->close();
        }
        const bool rxAudioProcAvail = prefs.enableLAN;
        ui->RXaudioProcBtn->setEnabled(rxAudioProcAvail);
        if (!rxAudioProcAvail && rxAudioProcWin) {
            rxAudioProcWin->close();
        }

        if (rigCaps->commands.contains(funcSendCW)) {
            // We have a send CW function, so enable the window.
            cw = new cwSender(this,rig);
            cw->setCutNumbers(prefs.cwCutNumbers);
            cw->setSendImmediate(prefs.cwSendImmediate);
            cw->setSidetoneEnable(prefs.cwSidetoneEnabled);
            cw->setSidetoneLevel(prefs.cwSidetoneLevel);
            cw->setMacroText(prefs.cwMacroList);
        }

        ui->cwButton->setEnabled(rigCaps->commands.contains(funcSendCW));
        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));
        ui->monitorSlider->setEnabled(rigCaps->commands.contains(funcMonitorGain));
        ui->rfGainSlider->setEnabled(rigCaps->commands.contains(funcRfGain));

        // Only show settingsgroup if rig has sub
        ui->scopeSettingsGroup->setVisible(rigCaps->commands.contains(funcVFODualWatch)||rigCaps->commands.contains(funcVFOBandMS));

        ui->scopeDualBtn->setVisible(rigCaps->commands.contains(funcVFODualWatch));
        ui->mainEqualsSubBtn->setVisible(rigCaps->commands.contains(funcVFOEqualMS));
        ui->swapMainSubBtn->setVisible(rigCaps->commands.contains(funcVFOSwapMS));
        //ui->mainSubTrackingBtn->setVisible(rigCaps->commands.contains(funcMainSubTracking));
        // Only show this split button on IC7610/IC785x
        ui->splitBtn->setVisible(rigCaps->commands.contains(funcSplitStatus) && rigCaps->commands.contains(funcVFOEqualMS));
        ui->antennaGroup->setVisible(rigCaps->commands.contains(funcAntenna));
        ui->preampAttGroup->setVisible(rigCaps->commands.contains(funcPreamp));
        //ui->dualWatchBtn->setVisible(rigCaps->hasCommand29);

        ui->nbEnableChk->setTristate(false);
        ui->nrEnableChk->setTristate(false);
        ui->nbEnableChk->setChecked(false);
        ui->nrEnableChk->setChecked(false);

        if (rigCaps->commands.contains(funcNoiseBlanker)) {
            qInfo () << "nb max val" << rigCaps->commands.find(funcNoiseBlanker)->maxVal;
            if (rigCaps->commands.find(funcNoiseBlanker)->maxVal > 1)
                ui->nbEnableChk->setTristate(true);
        }
        ui->nbEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseBlanker));

        if (rigCaps->commands.contains(funcNoiseReduction)) {
            qInfo () << "nr max val" << rigCaps->commands.find(funcNoiseReduction)->maxVal;
            if (rigCaps->commands.find(funcNoiseReduction)->maxVal > 1)
                ui->nrEnableChk->setTristate(true);
        }

        if (rigCaps->commands.contains(funcRFPower)) {
            auto f = rigCaps->commands.find(funcRFPower);
            ui->txPowerSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcMonitorGain)) {
            auto f = rigCaps->commands.find(funcMonitorGain);
            ui->monitorSlider->setRange(f->minVal,f->maxVal);
        }

        if (rigCaps->commands.contains(funcSquelch)) {
            auto f = rigCaps->commands.find(funcSquelch);
            ui->sqlSlider->setRange(f->minVal,f->maxVal);
        }

        ui->nrEnableChk->setEnabled(rigCaps->commands.contains(funcNoiseReduction));

        ui->ipPlusEnableChk->setEnabled(rigCaps->commands.contains(funcIPPlus));
        ui->compEnableChk->setEnabled(rigCaps->commands.contains(funcCompressor));
        ui->voxEnableChk->setEnabled(rigCaps->commands.contains(funcVox));
        ui->digiselEnableChk->setEnabled(rigCaps->commands.contains(funcDigiSel));

        // Icom CI-V 0x16 0x12: 1=FAST, 2=MID, 3=SLOW.
        QComboBox *agcSelCombo = findChild<QComboBox*>(QStringLiteral("agcSelCombo"));
        if (agcSelCombo != Q_NULLPTR) {
            agcSelCombo->blockSignals(true);
            agcSelCombo->clear();
            agcSelCombo->addItem(tr("FAST"), 1);
            agcSelCombo->addItem(tr("MID"), 2);
            agcSelCombo->addItem(tr("SLOW"), 3);
            agcSelCombo->setCurrentIndex(-1);
            agcSelCombo->setEnabled(rigCaps->commands.contains(funcAGC));
            agcSelCombo->blockSignals(false);
        }






















        // IC7610_EXT_CAPS_BEGIN
        const bool ext7610IsRig =
                rigCaps->modelName.contains(QStringLiteral("7610"), Qt::CaseInsensitive);

        QPushButton *ext7610Btn =
                findChild<QPushButton*>(QStringLiteral("ext7610Btn"));
        if (ext7610Btn != Q_NULLPTR) {
            ext7610Btn->setVisible(false); // IC7610_V23_HIDE_LEGACY_EXT
            ext7610Btn->setEnabled(ext7610IsRig);
        }

        QLabel *ext7610ReceiverLabel =
                findChild<QLabel*>(QStringLiteral("ext7610ReceiverLabel"));
        if (ext7610ReceiverLabel != Q_NULLPTR) {
            ext7610ReceiverLabel->setText(
                        currentReceiver ? tr("Receiver: SUB") : tr("Receiver: MAIN"));
        }

        auto ext7610Enable = [=](const char *name, funcs f) {
            QWidget *w = findChild<QWidget*>(QString::fromLatin1(name));
            if (w != Q_NULLPTR)
                w->setEnabled(ext7610IsRig && rigCaps->commands.contains(f));
        };

        ext7610Enable("agcTcCombo", funcAGCTimeConstant);
        ext7610Enable("apfModeCombo", funcAudioPeakFilter);
        ext7610Enable("apfPositionSpin", funcAPFLevel);
        ext7610Enable("apfTypeCombo", funcAPFType);
        ext7610Enable("apfAfLevelSpin", funcAPFTypeLevel);
        ext7610Enable("nrLevelSpin", funcNRLevel);
        ext7610Enable("nbLevelSpin", funcNBLevel);
        ext7610Enable("nbDepthSpin", funcNBDepth);
        ext7610Enable("nbWidthSpin", funcNBWidth);
        ext7610Enable("autoNotchChk", funcAutoNotch);
        ext7610Enable("manualNotchChk", funcManualNotch);
        ext7610Enable("notchPositionSpin", funcNotchFilter);
        ext7610Enable("manualNotchWidthCombo", funcManualNotchWidth);
        ext7610Enable("afMuteChk", funcAFMute);
        ext7610Enable("digiSelShiftSpin", funcDigiSelShift);
        ext7610Enable("twinPeakChk", funcTwinPeakFilter);
        ext7610Enable("dialLockChk", funcDialLock);
        ext7610Enable("mainSubTrackingChk", funcMainSubTracking);
        ext7610Enable("compLevelSpin", funcCompressorLevel);
        ext7610Enable("driveGainSpin", funcDriveGain);
        ext7610Enable("voxGainSpin", funcVoxGain);
        ext7610Enable("antiVoxGainSpin", funcAntiVoxGain);
        ext7610Enable("voxDelaySpin", funcVOXDelay);
        ext7610Enable("voxVoiceDelayCombo", funcVOXVoiceDelay);
        ext7610Enable("breakInModeCombo", funcBreakIn);
        ext7610Enable("breakInDelaySpin", funcBreakInDelay);
        ext7610Enable("ssbTxBwCombo", funcSSBTXBandwidth);
        // IC7610_EXT_CAPS_END



        for (const auto& receiver: receivers) {
            if (receiver->getReceiver() == 0) {
                // Report scope redraw time to Select Radio window (only scope 0)
                connect(receiver,SIGNAL(spectrumTime(double)),selRad,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),selRad,SLOT(waterfallTime(double)));
                connect(receiver,SIGNAL(spectrumTime(double)),rig,SLOT(spectrumTime(double)));
                connect(receiver,SIGNAL(waterfallTime(double)),rig,SLOT(waterfallTime(double)));
            }
            // Setup various combo box up for each VFO:
            receiver->clearMode();
            for (auto &m: rigCaps->modes)
            {
                receiver->addMode(m.name,QVariant::fromValue(m));
            }

            receiver->clearFilter();
            for (auto& f: rigCaps->filters)
            {
                receiver->addFilter(f.name,f.num);
            }

            receiver->clearRoofing();
            for (auto& f: rigCaps->roofing)
            {
                receiver->addRoofing(f.name,f.num);
            }

            receiver->clearData();

            receiver->addData("Data Off",0);

            if (rigCaps->commands.contains(funcDATA1Mod))
            {
                setupui->updateModSourceList(1, rigCaps->inputs);
                if (!rigCaps->commands.contains(funcDATA2Mod))
                {
                    receiver->addData("Data On", 2);
                }
            }

            if (rigCaps->commands.contains(funcDATA2Mod))
            {
                setupui->updateModSourceList(2, rigCaps->inputs);
                receiver->addData("Data 1", 2);
                receiver->addData("Data 2", 2);
            }

            if (rigCaps->commands.contains(funcDATA3Mod))
            {
                setupui->updateModSourceList(3, rigCaps->inputs);
                receiver->addData("Data 3", 3);
            }
            setupui->enableModSource(0,rigCaps->commands.contains(funcDATAOffMod));
            setupui->enableModSource(1,rigCaps->commands.contains(funcDATA1Mod));
            setupui->enableModSource(2,rigCaps->commands.contains(funcDATA2Mod));
            setupui->enableModSource(3,rigCaps->commands.contains(funcDATA3Mod));

            // Disable unsupported mod sources
            for (const auto &r: rigCaps->inputs)
            {
                setupui->enableModSourceItem(0,r,rigCaps->commands.contains(funcDATAOffMod));
                setupui->enableModSourceItem(1,r,(rigCaps->commands.contains(funcDATA1Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(2,r,(rigCaps->commands.contains(funcDATA2Mod) && r.reg > -1) ? true : false);
                setupui->enableModSourceItem(3,r,(rigCaps->commands.contains(funcDATA3Mod) && r.reg > -1) ? true : false);
            }

            receiver->clearSpans();
            if(rigCaps->hasSpectrum)
            {
                for(unsigned int i=0; i < rigCaps->scopeCenterSpans.size(); i++)
                {
                    receiver->addSpan(rigCaps->scopeCenterSpans.at(i).name, QVariant::fromValue(rigCaps->scopeCenterSpans.at(i)));
                }
                receiver->setRange(prefs.mainPlotFloor, prefs.mainPlotCeiling);
            }
        }

        // Set the tuning step combo box up:
        ui->tuningStepCombo->blockSignals(true);
        ui->tuningStepCombo->clear();
        for (auto &s: rigCaps->steps)
        {
            ui->tuningStepCombo->addItem(s.name, s.hz);
        }

        ui->tuningStepCombo->setCurrentIndex(2);
        ui->tuningStepCombo->blockSignals(false);

        setupui->updateModSourceList(0, rigCaps->inputs);

        ui->attSelCombo->clear();
        if(rigCaps->commands.contains(funcAttenuator))
        {
            ui->attSelCombo->setDisabled(false);
            for (auto &att: rigCaps->attenuators)
            {
                ui->attSelCombo->addItem(att.name,att.num);
            }
        } else {
            ui->attSelCombo->setDisabled(true);
        }

        ui->preampSelCombo->clear();
        if(rigCaps->commands.contains(funcPreamp))
        {
            ui->preampSelCombo->setDisabled(false);
            for (auto &pre: rigCaps->preamps)
            {
                ui->preampSelCombo->addItem(pre.name, pre.num);
            }
        } else {
            ui->preampSelCombo->setDisabled(true);
        }

        ui->antennaSelCombo->clear();
        if(rigCaps->commands.contains(funcAntenna))
        {
            ui->antennaSelCombo->setDisabled(false);
            for (auto &ant: rigCaps->antennas)
            {
                ui->antennaSelCombo->addItem(ant.name,ant.num);
            }
        } else {
            ui->antennaSelCombo->setDisabled(true);
        }

        ui->rxAntennaCheck->setEnabled(rigCaps->commands.contains(funcRXAntenna));
        ui->rxAntennaCheck->setChecked(false);

        ui->tuneEnableChk->setEnabled(rigCaps->commands.contains(funcTunerStatus));
        ui->tuneNowBtn->setEnabled(rigCaps->commands.contains(funcTunerStatus));

        ui->memoriesBtn->setEnabled(rigCaps->commands.contains(funcMemoryContents));

        ui->connectBtn->setText("Disconnect from Radio"); // We must be connected now.
        connStatus = connConnected;

        // Now we know that we are connected, enable rigctld
        enableRigCtl(prefs.enableRigCtlD);

        if(prefs.enableLAN)
        {
            ui->afGainSlider->setValue(prefs.localAFgain);
            queue->receiveValue(funcAfGain,quint8(prefs.localAFgain),currentReceiver);
        } else {
            // If not network connected, select the requested PTT type.
            emit setPTTType(prefs.pttType);
        }
        // Adding these here because clearly at this point we have valid
        // rig comms. In the future, we should establish comms and then
        // do all the initial grabs. For now, this hack of adding them here and there:
        // recalculate command timing now that we know the rig better:
        if(prefs.polling_ms != 0)
        {
            changePollTiming(prefs.polling_ms, true);
        } else {
            calculateTimingParameters();
        }

    }

    initPeriodicCommands();
    getInitialRigState();
}


void wfmain::radioInUse(quint8 radio, bool admin, quint8 busy, QString user, QString ip)
{
   Q_UNUSED(busy)
   Q_UNUSED(radio)
   Q_UNUSED(user)
   Q_UNUSED(ip)
   qDebug(logSystem()) << "Is this user an admin? " << ((admin)?"yes":"no");
   isRadioAdmin = admin;
}

void wfmain::receiveScopeImage(uchar receiver)
{

    #if defined (USB_CONTROLLER)
            // Send to USB Controllers if requested
        if (receiver == 0) {
            for (auto it = usbDevices.begin(); it != usbDevices.end(); it++)
            {
                auto dev = &it.value();

                if (dev->connected && dev->type.model == usbDeviceType::StreamDeckPlus && dev->lcd == funcLCDWaterfall )
                {
                    lcdImage = receivers[receiver]->getWaterfallImage();
                    emit sendControllerRequest(dev, usbFeatureType::featureLCD, 0, "", &lcdImage);
                }
                else if (dev->connected && dev->type.model == usbDeviceType::StreamDeckPlus && dev->lcd == funcLCDSpectrum)
                {
                    lcdImage = receivers[receiver]->getSpectrumImage();
                    emit sendControllerRequest(dev, usbFeatureType::featureLCD, 0, "", &lcdImage);
                }
            }
        }
    #endif

}

// Assorted checkboxes

// lambda slots, these cuts-down on number of dedicated slot functions
void wfmain::setupLambdaSlots()
{

// As checkState is deprecated, use checkStateChanged if available
#if (QT_VERSION < QT_VERSION_CHECK(6,7,0))
#define CHKFUNC &QCheckBox::stateChanged, this, [=](int checked)
#else
#define CHKFUNC &QCheckBox::checkStateChanged, this, [=](Qt::CheckState checked)
#endif

    connect(ui->nrEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcNoiseReduction,
                QVariant::fromValue<uchar>(ui->nrEnableChk->isTristate()?checked:bool(checked)),false,currentReceiver));
    });

    connect(ui->nbEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcNoiseBlanker,
                QVariant::fromValue<uchar>(ui->nbEnableChk->isTristate()?checked:bool(checked)),false,currentReceiver));
    });

    QComboBox *agcSelCombo = findChild<QComboBox*>(QStringLiteral("agcSelCombo"));
    if (agcSelCombo != Q_NULLPTR) {
        connect(agcSelCombo,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                this, [=](int index) {
            if (index < 0)
                return;
            const uchar agc =
                    static_cast<uchar>(agcSelCombo->itemData(index).toUInt());
            queue->addUnique(priorityImmediate,
                             queueItem(funcAGC,
                                       QVariant::fromValue<uchar>(agc),
                                       false,
                                       currentReceiver));
        });
    }

    connect(ui->ipPlusEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcIPPlus,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->compEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcCompressor,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->voxEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcVox,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });

    connect(ui->digiselEnableChk, CHKFUNC {
        queue->addUnique(priorityImmediate,queueItem(funcDigiSel,QVariant::fromValue<bool>(checked),false,currentReceiver));
    });




// IC7610_EXT_SIGNALS_BEGIN
    QPushButton *ext7610Btn =
            findChild<QPushButton*>(QStringLiteral("ext7610Btn"));
    QPushButton *ext7610RefreshBtn =
            findChild<QPushButton*>(QStringLiteral("ext7610RefreshBtn"));
    QPushButton *ext7610CloseBtn =
            findChild<QPushButton*>(QStringLiteral("ext7610CloseBtn"));
    QDialog *ext7610Dialog =
            findChild<QDialog*>(QStringLiteral("ext7610Dialog"));

    auto ext7610Target = [=](funcs f) -> uchar {
        if (rigCaps == Q_NULLPTR)
            return 0;
        auto it = rigCaps->commands.find(f);
        if (it == rigCaps->commands.end())
            return 0;
        return it->cmd29 ? currentReceiver : 0;
    };

    auto ext7610Read = [=](funcs f) {
        if (rigCaps == Q_NULLPTR)
            return;
        auto it = rigCaps->commands.find(f);
        if (it == rigCaps->commands.end() || !it->getCmd)
            return;
        queue->add(priorityImmediate, f, false, ext7610Target(f));
    };

    auto ext7610ReadAll = [=]() {
        QLabel *label =
                findChild<QLabel*>(QStringLiteral("ext7610ReceiverLabel"));
        if (label != Q_NULLPTR)
            label->setText(currentReceiver ? tr("Receiver: SUB") : tr("Receiver: MAIN"));

        ext7610Read(funcAGC);
        ext7610Read(funcAGCTimeConstant);
        ext7610Read(funcAudioPeakFilter);
        ext7610Read(funcAPFLevel);
        ext7610Read(funcAPFType);
        ext7610Read(funcAPFTypeLevel);
        ext7610Read(funcNRLevel);
        ext7610Read(funcNBLevel);
        ext7610Read(funcNBDepth);
        ext7610Read(funcNBWidth);
        ext7610Read(funcAutoNotch);
        ext7610Read(funcManualNotch);
        ext7610Read(funcNotchFilter);
        ext7610Read(funcManualNotchWidth);
        ext7610Read(funcAFMute);
        ext7610Read(funcDigiSel);
        ext7610Read(funcDigiSelShift);
        ext7610Read(funcTwinPeakFilter);
        ext7610Read(funcDialLock);
        ext7610Read(funcMainSubTracking);
        ext7610Read(funcCompressorLevel);
        ext7610Read(funcDriveGain);
        ext7610Read(funcVoxGain);
        ext7610Read(funcAntiVoxGain);
        ext7610Read(funcVOXDelay);
        ext7610Read(funcVOXVoiceDelay);
        ext7610Read(funcBreakIn);
        ext7610Read(funcBreakInDelay);
        ext7610Read(funcSSBTXBandwidth);
        // IC7610_V23_SET_READS
        ext7610Read(funcQuickDualWatch);
        ext7610Read(funcQuickSplit);
        ext7610Read(funcCIVTransceive);
        ext7610Read(funcCIVOutput);
        ext7610Read(funcDashRatio);
        ext7610Read(funcACCAModLevel);
        ext7610Read(funcUSBModLevel);
        ext7610Read(funcLANModLevel);
        ext7610Read(funcSSBRXBass);
        ext7610Read(funcSSBRXTreble);
        ext7610Read(funcAMRXBass);
        ext7610Read(funcAMRXTreble);
        ext7610Read(funcFMRXBass);
        ext7610Read(funcFMRXTreble);
        ext7610Read(funcSSBTXBass);
        ext7610Read(funcSSBTXTreble);
        ext7610Read(funcAMTXBass);
        ext7610Read(funcAMTXTreble);
        ext7610Read(funcFMTXBass);
        ext7610Read(funcFMTXTreble);
        ext7610Read(funcBeepLevel);
        ext7610Read(funcBeepLevelLimit);
        ext7610Read(funcBeepConfirmation);
        ext7610Read(funcBandEdgeBeep);
        ext7610Read(funcRFSQLControl);
        ext7610Read(funcTXDelayHF);
        ext7610Read(funcTXDelay50m);
        // IC7610_V27_SET_READS
        ext7610Read(funcACCOutputSelect);
        ext7610Read(funcACCAFOutputLevel);
        ext7610Read(funcACCIFOutputLevel);
        ext7610Read(funcUSBOutputSelect);
        ext7610Read(funcUSBAFOutputLevel);
        ext7610Read(funcUSBIFOutputLevel);
        ext7610Read(funcLANOutputSelect);
        ext7610Read(funcREFInput);
        // IC7610_V37_SET_READS
        ext7610Read(func7610CIVTimeout);
        ext7610Read(func7610RTTYMarkFreq);
        ext7610Read(func7610RTTYShiftWidth);
        ext7610Read(func7610RTTYKeyPolarity);
        ext7610Read(func7610SSBCWSyncTune);
        ext7610Read(func7610CWNormalSide);
        ext7610Read(func7610USBKeyCW);
        ext7610Read(func7610USBKeyRTTY);
        ext7610Read(func7610CIVRemoteAddress);
        ext7610Read(func7610CIVUSBPort);
        ext7610Read(func7610CIVUSBEcho);
        ext7610Read(func7610SideToneLevel);
        ext7610Read(func7610SideToneLimit);
        ext7610Read(func7610KeyerRepeat);
        ext7610Read(func7610CWRiseTime);
        ext7610Read(func7610PaddlePolarity);
        ext7610Read(func7610KeyType);
        ext7610Read(func7610MicUpDownKeyer);
        // IC7610_V39_RTTY_SET_READS
        ext7610Read(func7610ExternalKeypadRTTY);
        ext7610Read(func7610DecodeBaud);
        ext7610Read(func7610RTTYFFTAverage);
        ext7610Read(func7610RTTYDecodeUSOS);
        ext7610Read(func7610RTTYNewLine);
        ext7610Read(func7610RTTYDiddle);
        ext7610Read(func7610RTTYTXUSOS);
        ext7610Read(func7610RTTYAutoCRLF);
        ext7610Read(func7610RTTYTimestamp);
        ext7610Read(func7610RTTYTimestampTime);
        ext7610Read(func7610RTTYTimestampFreq);
        ext7610Read(func7610RTTYDecodeLog);
        ext7610Read(func7610RTTYLogFileType);
    };

    // IC7610_V46_CIV_RUNTIME_REFRESH
    // IC7610_V46_CIV_RUNTIME_SCOPE_FIX
    //
    // setupLambdaSlots() is a different C++ scope from setupMainUI().
    // Resolve the Extended Controls labels from their object names instead
    // of referencing setupMainUI() local pointer variables.
    auto v46ValidCivBaud =
            [](quint32 baud) -> bool {
        switch (baud) {
        case 4800:
        case 9600:
        case 19200:
        case 38400:
        case 57600:
        case 115200:
            return true;
        default:
            return false;
        }
    };

    auto v46RefreshCivInfo =
            [=]() {
        QLabel *v46CivTransport =
                findChild<QLabel*>(
                    QStringLiteral("setCivTransportValue"));

        QLabel *v46CivPort =
                findChild<QLabel*>(
                    QStringLiteral("setCivPortValue"));

        QLabel *v46CivBaud =
                findChild<QLabel*>(
                    QStringLiteral("setCivBaudValue"));

        QLabel *v46CivLocalAddress =
                findChild<QLabel*>(
                    QStringLiteral("setCivLocalAddressValue"));

        QLabel *v46CivRadioBaud =
                findChild<QLabel*>(
                    QStringLiteral("setCivRadioBaudInfo"));

        QLabel *v46CivRadioAddress =
                findChild<QLabel*>(
                    QStringLiteral("setCivRadioAddressInfo"));

        QLabel *v46CivUsbBaud =
                findChild<QLabel*>(
                    QStringLiteral("setCivUsbBaudInfo"));

        if (v46CivTransport == Q_NULLPTR ||
            v46CivPort == Q_NULLPTR ||
            v46CivBaud == Q_NULLPTR ||
            v46CivLocalAddress == Q_NULLPTR ||
            v46CivRadioBaud == Q_NULLPTR ||
            v46CivRadioAddress == Q_NULLPTR ||
            v46CivUsbBaud == Q_NULLPTR) {
            qWarning(logGui())
                    << "IC-7610 CI-V runtime information widgets "
                       "could not all be resolved.";
            return;
        }

        v46CivTransport->setText(
            prefs.enableLAN
                ? tr("LAN / Network")
                : tr("Serial / USB"));

        if (prefs.enableLAN) {
            v46CivPort->setText(
                tr("Native LAN"));
        }
        else {
            QString v46Port = serialPortRig;

            if (v46Port.isEmpty() ||
                v46Port.compare(
                    QStringLiteral("auto"),
                    Qt::CaseInsensitive) == 0) {
                v46Port = prefs.serialPortRadio;
            }

            if (v46Port.isEmpty())
                v46Port = tr("Unknown");

            v46CivPort->setText(v46Port);
        }

        quint32 v46EffectiveBaud = 0;

        if (!prefs.enableLAN &&
            rigCaps != Q_NULLPTR &&
            v46ValidCivBaud(rigCaps->baudRate)) {
            v46EffectiveBaud = rigCaps->baudRate;
        }

        if (!prefs.enableLAN &&
            v46EffectiveBaud == 0 &&
            v46ValidCivBaud(prefs.serialPortBaud)) {
            v46EffectiveBaud = prefs.serialPortBaud;
        }

        if (!prefs.enableLAN &&
            v46EffectiveBaud == 0 &&
            v46ValidCivBaud(serverConfig.baudRate)) {
            v46EffectiveBaud = serverConfig.baudRate;
        }

        if (prefs.enableLAN) {
            v46CivBaud->setText(
                tr("N/A (LAN transport)"));

            v46CivRadioBaud->setText(
                tr("Not readable over LAN"));

            v46CivUsbBaud->setText(
                tr("Not readable over LAN"));
        }
        else if (v46EffectiveBaud != 0) {
            v46CivBaud->setText(
                tr("%1 baud")
                    .arg(v46EffectiveBaud));

            v46CivRadioBaud->setText(
                tr("%1 baud - live/effective serial CI-V link; "
                   "front-panel CI-V Baud may be AUTO or fixed")
                    .arg(v46EffectiveBaud));

            v46CivUsbBaud->setText(
                tr("%1 baud - live/effective serial CI-V link; "
                   "literal USB Baud versus [REMOTE]-linked selection "
                   "is not separately observable")
                    .arg(v46EffectiveBaud));
        }
        else {
            v46CivBaud->setText(
                tr("Unknown"));

            v46CivRadioBaud->setText(
                tr("Effective serial CI-V baud is not available"));

            v46CivUsbBaud->setText(
                tr("Effective serial CI-V baud is not available"));
        }

        if (prefs.radioCIVAddr != 0) {
            const QString v46Address =
                    QStringLiteral("0x%1")
                        .arg(
                            prefs.radioCIVAddr & 0xff,
                            2,
                            16,
                            QChar('0'))
                        .toUpper();

            v46CivLocalAddress->setText(v46Address);

            v46CivRadioAddress->setText(
                tr("wfview target %1")
                    .arg(v46Address));
        }
        else {
            v46CivLocalAddress->setText(
                tr("Auto"));

            v46CivRadioAddress->setText(
                tr("Auto (wfview detects radio)"));
        }
    };

    if (ext7610Btn != Q_NULLPTR && ext7610Dialog != Q_NULLPTR) {
        connect(ext7610Btn, &QPushButton::clicked, this, [=]() {
            v46RefreshCivInfo();
            ext7610ReadAll();
            ext7610Dialog->show();
            ext7610Dialog->raise();
            ext7610Dialog->activateWindow();
        });
    }

    if (ext7610RefreshBtn != Q_NULLPTR) {
        connect(ext7610RefreshBtn, &QPushButton::clicked, this, [=]() {
            v46RefreshCivInfo();
            ext7610ReadAll();
        });
    }
    if (ext7610CloseBtn != Q_NULLPTR && ext7610Dialog != Q_NULLPTR) {
        connect(ext7610CloseBtn, &QPushButton::clicked,
                ext7610Dialog, &QDialog::hide);
    }

    auto ext7610ConnectSpin16 = [=](const char *name, funcs f) {
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QSpinBox::editingFinished, this, [=]() {
            if (rigCaps == Q_NULLPTR)
                return;
            auto it = rigCaps->commands.find(f);
            if (it == rigCaps->commands.end() || !it->setCmd)
                return;
            queue->addUnique(
                        priorityImmediate,
                        queueItem(f,
                                  QVariant::fromValue<ushort>(
                                      static_cast<ushort>(w->value())),
                                  false,
                                  ext7610Target(f)));
        });
    };    auto ext7610ConnectMappedSpin16 = [=](const char *name,
                                          funcs f,
                                          int displayMin,
                                          int displayMax,
                                          int rawMin,
                                          int rawMax) {
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;

        connect(w, &QSpinBox::editingFinished, this, [=]() {
            if (rigCaps == Q_NULLPTR || displayMax == displayMin)
                return;

            auto it = rigCaps->commands.find(f);
            if (it == rigCaps->commands.end() || !it->setCmd)
                return;

            int display = w->value();
            if (display < displayMin)
                display = displayMin;
            if (display > displayMax)
                display = displayMax;

            int raw =
                    rawMin +
                    static_cast<int>(
                        ((static_cast<qint64>(display - displayMin) *
                          static_cast<qint64>(rawMax - rawMin)) +
                         ((displayMax - displayMin) / 2)) /
                        (displayMax - displayMin));

            if (raw < rawMin)
                raw = rawMin;
            if (raw > rawMax)
                raw = rawMax;

            queue->addUnique(
                        priorityImmediate,
                        queueItem(f,
                                  QVariant::fromValue<ushort>(
                                      static_cast<ushort>(raw)),
                                  false,
                                  ext7610Target(f)));
        });
    };
 auto ext7610ConnectSpin8 = [=](const char *name, funcs f) {
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QSpinBox::editingFinished, this, [=]() {
            if (rigCaps == Q_NULLPTR)
                return;
            auto it = rigCaps->commands.find(f);
            if (it == rigCaps->commands.end() || !it->setCmd)
                return;
            queue->addUnique(
                        priorityImmediate,
                        queueItem(f,
                                  QVariant::fromValue<uchar>(
                                      static_cast<uchar>(w->value())),
                                  false,
                                  ext7610Target(f)));
        });
    };

    auto ext7610ConnectCombo8 = [=](const char *name, funcs f) {
        QComboBox *w = findChild<QComboBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                this, [=](int index) {
            if (index < 0 || rigCaps == Q_NULLPTR)
                return;
            auto it = rigCaps->commands.find(f);
            if (it == rigCaps->commands.end() || !it->setCmd)
                return;
            queue->addUnique(
                        priorityImmediate,
                        queueItem(f,
                                  QVariant::fromValue<uchar>(
                                      static_cast<uchar>(
                                          w->itemData(index).toUInt())),
                                  false,
                                  ext7610Target(f)));
        });
    };

    auto ext7610ConnectCheck = [=](const char *name, funcs f) {
        QCheckBox *w = findChild<QCheckBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QCheckBox::toggled, this, [=](bool checked) {
            if (rigCaps == Q_NULLPTR)
                return;
            auto it = rigCaps->commands.find(f);
            if (it == rigCaps->commands.end() || !it->setCmd)
                return;
            queue->addUnique(
                        priorityImmediate,
                        queueItem(f,
                                  QVariant::fromValue<bool>(checked),
                                  false,
                                  ext7610Target(f)));
        });
    };

    // IC7610_V43_FINAL_MAIN_NOTCH_UI
    QCheckBox *v43MainAutoNotch =
            findChild<QCheckBox*>(
                QStringLiteral("mainAutoNotchChk"));

    if (v43MainAutoNotch == Q_NULLPTR) {
        v43MainAutoNotch =
                new QCheckBox(
                    tr("AN"),
                    ui->OtherControlsGrp);

        v43MainAutoNotch->setObjectName(
                QStringLiteral("mainAutoNotchChk"));
        v43MainAutoNotch->setEnabled(false);
        v43MainAutoNotch->setMaximumWidth(46);
        v43MainAutoNotch->setToolTip(
                tr("Auto Notch"));
        ui->horizontalLayout_6->addWidget(
                v43MainAutoNotch);
    }

    QCheckBox *v43MainManualNotch =
            findChild<QCheckBox*>(
                QStringLiteral("mainManualNotchChk"));

    if (v43MainManualNotch == Q_NULLPTR) {
        v43MainManualNotch =
                new QCheckBox(
                    tr("MN"),
                    ui->OtherControlsGrp);

        v43MainManualNotch->setObjectName(
                QStringLiteral("mainManualNotchChk"));
        v43MainManualNotch->setEnabled(false);
        v43MainManualNotch->setMaximumWidth(46);
        v43MainManualNotch->setToolTip(
                tr("Manual Notch"));
        ui->horizontalLayout_6->addWidget(
                v43MainManualNotch);
    }

    QSpinBox *v43MainNotchPosition =
            findChild<QSpinBox*>(
                QStringLiteral("mainNotchPositionSpin"));

    if (v43MainNotchPosition == Q_NULLPTR) {
        v43MainNotchPosition =
                new QSpinBox(
                    ui->OtherControlsGrp);

        v43MainNotchPosition->setObjectName(
                QStringLiteral("mainNotchPositionSpin"));
        v43MainNotchPosition->setRange(-100, 100);
        v43MainNotchPosition->setPrefix(tr("N "));
        v43MainNotchPosition->setSuffix(tr("%"));
        v43MainNotchPosition->setAlignment(
                Qt::AlignRight);
        v43MainNotchPosition->setEnabled(false);
        v43MainNotchPosition->setMinimumWidth(76);
        v43MainNotchPosition->setMaximumWidth(92);
        // IC7610_V44_NOTCH_POSITION_WIDTH
        // Guarantee enough room for the full "N -100%" / "N +100%" text
        // plus the spin arrows. AN and MN sizing is intentionally untouched.
        v43MainNotchPosition->setFixedWidth(112);
        v43MainNotchPosition->setToolTip(
                tr("Manual Notch Position"));
        ui->horizontalLayout_6->addWidget(
                v43MainNotchPosition);
    }

    // IC7610_V45_NOTCH_WIDTH_LOCK
    // IC7610_V46_NOTCH_SIZEHINT
    // Use the actual font/style size hint rather than a hard logical-pixel
    // assumption.  This remains readable on the high-DPI Windows test setup.
    if (v43MainNotchPosition != Q_NULLPTR) {
        v43MainNotchPosition->setPrefix(tr("N "));
        v43MainNotchPosition->setSuffix(tr("%"));
        v43MainNotchPosition->setRange(-100, 100);
        // IC7610_V47_NOTCH_VCLIP
        // Override only this widget's padding so the text has full height.
        v43MainNotchPosition->setStyleSheet(
                QStringLiteral(
                    "QSpinBox#mainNotchPositionSpin {"
                    " padding-top: 0px; padding-bottom: 0px;"
                    " padding-left: 2px; padding-right: 2px;"
                    " min-width: 0px; }"));
        v43MainNotchPosition->setAlignment(
                Qt::AlignRight | Qt::AlignVCenter);
        v43MainNotchPosition->setSizePolicy(
                    QSizePolicy::Fixed, QSizePolicy::Fixed);

        const int v46NotchWidth =
                qMax(112, v43MainNotchPosition->sizeHint().width() + 28);

        v43MainNotchPosition->setMinimumWidth(v46NotchWidth);
        v43MainNotchPosition->setMaximumWidth(v46NotchWidth);
        v43MainNotchPosition->setFixedWidth(v46NotchWidth);

        const int v47NotchHeight =
                qMax(v43MainNotchPosition->sizeHint().height(),
                     v43MainNotchPosition->fontMetrics().height() + 8);
        v43MainNotchPosition->setFixedHeight(v47NotchHeight);
    }
    // Reuse the exact same sender helpers as EXT RX/DSP.
    ext7610ConnectCheck(
            "mainAutoNotchChk",
            funcAutoNotch);

    ext7610ConnectCheck(
            "mainManualNotchChk",
            funcManualNotch);

    // IC7610_V45_NOTCH_CENTER_TX
    auto v45ConnectNotchPosition =
            [=](const char *name) {
        QSpinBox *w =
                findChild<QSpinBox*>(
                    QString::fromLatin1(name));

        if (w == Q_NULLPTR)
            return;

        w->setRange(-100, 100);

        connect(
            w,
            &QSpinBox::editingFinished,
            this,
            [=]() {
                if (rigCaps == Q_NULLPTR)
                    return;

                auto it =
                        rigCaps->commands.find(
                            funcNotchFilter);

                if (it == rigCaps->commands.end() ||
                    !it->setCmd)
                    return;

                const int percent =
                        qBound(-100, w->value(), 100);

                // IC7610_V47_NOTCH_TX_GUARD
                // editingFinished also fires on focus-out with no edit.
                // Never push a value back that simply mirrors the radio.
                const QVariant v47LastRx =
                        w->property("ic7610LastRxPercent");
                if (v47LastRx.isValid() &&
                    v47LastRx.toInt() == percent)
                    return;

                int raw = 128;

                if (percent <= 0) {
                    raw =
                            128 +
                            qRound(
                                static_cast<double>(percent) *
                                128.0 / 100.0);
                }
                else {
                    raw =
                            128 +
                            qRound(
                                static_cast<double>(percent) *
                                127.0 / 100.0);
                }

                raw = qBound(0, raw, 255);

                queue->addUnique(
                    priorityImmediate,
                    queueItem(
                        funcNotchFilter,
                        QVariant::fromValue<ushort>(
                            static_cast<ushort>(raw)),
                        false,
                        ext7610Target(
                            funcNotchFilter)));
            });
    };
    v45ConnectNotchPosition("mainNotchPositionSpin");

    // GET readback after SET from EITHER copy of each control.
    auto v43ScheduleNotchRead =
            [=](funcs f) {
        QTimer::singleShot(
            120,
            this,
            [=]() {
                ext7610Read(f);
            });
    };

    QCheckBox *v43ExtAutoNotch =
            findChild<QCheckBox*>(
                QStringLiteral("autoNotchChk"));

    QCheckBox *v43ExtManualNotch =
            findChild<QCheckBox*>(
                QStringLiteral("manualNotchChk"));

    QSpinBox *v43ExtNotchPosition =
            findChild<QSpinBox*>(
                QStringLiteral("notchPositionSpin"));

    if (v43MainAutoNotch != Q_NULLPTR)
        connect(
            v43MainAutoNotch,
            &QCheckBox::toggled,
            this,
            [=](bool) {
                v43ScheduleNotchRead(
                    funcAutoNotch);
            });

    if (v43ExtAutoNotch != Q_NULLPTR)
        connect(
            v43ExtAutoNotch,
            &QCheckBox::toggled,
            this,
            [=](bool) {
                v43ScheduleNotchRead(
                    funcAutoNotch);
            });

    if (v43MainManualNotch != Q_NULLPTR)
        connect(
            v43MainManualNotch,
            &QCheckBox::toggled,
            this,
            [=](bool) {
                v43ScheduleNotchRead(
                    funcManualNotch);
            });

    if (v43ExtManualNotch != Q_NULLPTR)
        connect(
            v43ExtManualNotch,
            &QCheckBox::toggled,
            this,
            [=](bool) {
                v43ScheduleNotchRead(
                    funcManualNotch);
            });

    if (v43MainNotchPosition != Q_NULLPTR)
        connect(
            v43MainNotchPosition,
            &QSpinBox::editingFinished,
            this,
            [=]() {
                v43ScheduleNotchRead(
                    funcNotchFilter);
            });

    if (v43ExtNotchPosition != Q_NULLPTR)
        connect(
            v43ExtNotchPosition,
            &QSpinBox::editingFinished,
            this,
            [=]() {
                v43ScheduleNotchRead(
                    funcNotchFilter);
            });

    // Main controls remain disabled until these replies return.
    ext7610Read(funcAutoNotch);
    ext7610Read(funcManualNotch);
    ext7610Read(funcNotchFilter);
    // Two-byte BCD 0..255 level commands.
    ext7610ConnectMappedSpin16("apfPositionSpin", funcAPFLevel, -550, 550, 0, 255);
    ext7610ConnectMappedSpin16("nrLevelSpin", funcNRLevel, 0, 15, 8, 248);
    ext7610ConnectMappedSpin16("nbLevelSpin", funcNBLevel, 0, 100, 0, 255);
    ext7610ConnectMappedSpin16("nbWidthSpin", funcNBWidth, 1, 100, 0, 255);
    v45ConnectNotchPosition("notchPositionSpin"); // IC7610_V23_NOTCH_TX
    ext7610ConnectMappedSpin16("digiSelShiftSpin", funcDigiSelShift, -100, 100, 0, 255); // IC7610_V23_DIGISEL_TX
    ext7610ConnectMappedSpin16("compLevelSpin", funcCompressorLevel, 0, 10, 0, 255);
    ext7610ConnectMappedSpin16("driveGainSpin", funcDriveGain, 0, 100, 0, 255);
    ext7610ConnectMappedSpin16("voxGainSpin", funcVoxGain, 0, 100, 0, 255);
    ext7610ConnectMappedSpin16("antiVoxGainSpin", funcAntiVoxGain, 0, 100, 0, 255);
    ext7610ConnectMappedSpin16("breakInDelaySpin", funcBreakInDelay, 20, 130, 0, 255);

    // One-byte BCD controls.
    ext7610ConnectSpin8("apfAfLevelSpin", funcAPFTypeLevel);
    {
        QSpinBox *nbDepthSpin =
                findChild<QSpinBox*>(QStringLiteral("nbDepthSpin"));
        if (nbDepthSpin != Q_NULLPTR) {
            connect(nbDepthSpin, &QSpinBox::editingFinished, this, [=]() {
                if (rigCaps == Q_NULLPTR)
                    return;
                auto it = rigCaps->commands.find(funcNBDepth);
                if (it == rigCaps->commands.end() || !it->setCmd)
                    return;
                const uchar raw =
                        static_cast<uchar>(qBound(1, nbDepthSpin->value(), 10) - 1);
                queue->addUnique(
                            priorityImmediate,
                            queueItem(funcNBDepth,
                                      QVariant::fromValue<uchar>(raw),
                                      false,
                                      ext7610Target(funcNBDepth)));
            });
        }
    }
    ext7610ConnectSpin8("voxDelaySpin", funcVOXDelay);

    ext7610ConnectCombo8("agcTcCombo", funcAGCTimeConstant);
    ext7610ConnectCombo8("apfModeCombo", funcAudioPeakFilter);
    ext7610ConnectCombo8("apfTypeCombo", funcAPFType);
    ext7610ConnectCombo8("manualNotchWidthCombo", funcManualNotchWidth);
    ext7610ConnectCombo8("voxVoiceDelayCombo", funcVOXVoiceDelay);
    ext7610ConnectCombo8("breakInModeCombo", funcBreakIn);
    ext7610ConnectCombo8("ssbTxBwCombo", funcSSBTXBandwidth);

    ext7610ConnectCheck("autoNotchChk", funcAutoNotch);
    ext7610ConnectCheck("manualNotchChk", funcManualNotch);
    ext7610ConnectCheck("afMuteChk", funcAFMute);
    ext7610ConnectCheck("twinPeakChk", funcTwinPeakFilter);
    ext7610ConnectCheck("dialLockChk", funcDialLock);
    ext7610ConnectCheck("mainSubTrackingChk", funcMainSubTracking);
        // IC7610_V23_SET_SIGNAL_HELPERS
    auto set7610ReadBack = [=](funcs f) {
        ext7610Read(f);
    };

    auto set7610ConnectCheckBool = [=](const char *name, funcs f) {
        QCheckBox *w = findChild<QCheckBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QCheckBox::toggled, this, [=](bool checked) {
            if (!w->isEnabled() || rigCaps == Q_NULLPTR)
                return;
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<bool>(checked),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    auto set7610ConnectCheckU8 = [=](const char *name, funcs f) {
        QCheckBox *w = findChild<QCheckBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QCheckBox::toggled, this, [=](bool checked) {
            if (!w->isEnabled() || rigCaps == Q_NULLPTR)
                return;
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<uchar>(
                                     static_cast<uchar>(checked ? 1 : 0)),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    auto set7610ConnectSpinU8 = [=](const char *name, funcs f, int add) {
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QSpinBox::editingFinished, this, [=]() {
            if (!w->isEnabled() || rigCaps == Q_NULLPTR)
                return;
            const int value = w->value() + add;
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<uchar>(
                                     static_cast<uchar>(value)),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    auto set7610ConnectPercent16 = [=](const char *name, funcs f) {
        QSpinBox *w = findChild<QSpinBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w, &QSpinBox::editingFinished, this, [=]() {
            if (!w->isEnabled() || rigCaps == Q_NULLPTR)
                return;
            const ushort raw =
                    static_cast<ushort>(
                        qRound((w->value() * 255.0) / 100.0));
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<ushort>(raw),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    auto set7610ConnectComboU8 = [=](const char *name, funcs f) {
        QComboBox *w = findChild<QComboBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                this, [=](int index) {
            if (!w->isEnabled() || index < 0 || rigCaps == Q_NULLPTR)
                return;
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<uchar>(
                                     static_cast<uchar>(
                                         w->itemData(index).toUInt())),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    auto set7610ConnectComboU16 = [=](const char *name, funcs f) {
        QComboBox *w = findChild<QComboBox*>(QString::fromLatin1(name));
        if (w == Q_NULLPTR)
            return;
        connect(w,
                static_cast<void (QComboBox::*)(int)>(&QComboBox::activated),
                this, [=](int index) {
            if (!w->isEnabled() || index < 0 || rigCaps == Q_NULLPTR)
                return;
            queue->add(priorityImmediate,
                       queueItem(f,
                                 QVariant::fromValue<ushort>(
                                     static_cast<ushort>(
                                         w->itemData(index).toUInt())),
                                 false, 0));
            set7610ReadBack(f);
        });
    };

    set7610ConnectCheckBool("setQuickDualChk", funcQuickDualWatch);
    set7610ConnectCheckU8("setQuickSplitChk", funcQuickSplit);
    set7610ConnectCheckBool("setCivTransceiveChk", funcCIVTransceive);
    set7610ConnectCheckBool("setCivOutputChk", funcCIVOutput);
    set7610ConnectSpinU8("setDashRatioSpin", funcDashRatio, 0);

    set7610ConnectPercent16("setAccModSpin", funcACCAModLevel);
    set7610ConnectPercent16("setUsbModSpin", funcUSBModLevel);
    set7610ConnectPercent16("setLanModSpin", funcLANModLevel);

    set7610ConnectSpinU8("setSsbRxBass", funcSSBRXBass, 5);
    set7610ConnectSpinU8("setSsbRxTreble", funcSSBRXTreble, 5);
    set7610ConnectSpinU8("setAmRxBass", funcAMRXBass, 5);
    set7610ConnectSpinU8("setAmRxTreble", funcAMRXTreble, 5);
    set7610ConnectSpinU8("setFmRxBass", funcFMRXBass, 5);
    set7610ConnectSpinU8("setFmRxTreble", funcFMRXTreble, 5);
    set7610ConnectSpinU8("setSsbTxBass", funcSSBTXBass, 5);
    set7610ConnectSpinU8("setSsbTxTreble", funcSSBTXTreble, 5);
    set7610ConnectSpinU8("setAmTxBass", funcAMTXBass, 5);
    set7610ConnectSpinU8("setAmTxTreble", funcAMTXTreble, 5);
    set7610ConnectSpinU8("setFmTxBass", funcFMTXBass, 5);
    set7610ConnectSpinU8("setFmTxTreble", funcFMTXTreble, 5);

    set7610ConnectPercent16("setBeepLevelSpin", funcBeepLevel);
    set7610ConnectCheckBool("setBeepLimitChk", funcBeepLevelLimit);
    set7610ConnectCheckBool("setBeepConfirmChk", funcBeepConfirmation);
    set7610ConnectComboU8("setBandEdgeBeepCombo", funcBandEdgeBeep);
    set7610ConnectComboU16("setRfSqlCombo", funcRFSQLControl);
    set7610ConnectComboU16("setTxDelayHfCombo", funcTXDelayHF);
    set7610ConnectComboU16("setTxDelay50Combo", funcTXDelay50m);
    // IC7610_V27_SET_SIGNALS
    set7610ConnectComboU8("setAccOutputSelect", funcACCOutputSelect);
    set7610ConnectPercent16("setAccAfOutputSpin", funcACCAFOutputLevel);
    set7610ConnectPercent16("setAccIfOutputSpin", funcACCIFOutputLevel);
    set7610ConnectComboU8("setUsbOutputSelect", funcUSBOutputSelect);
    set7610ConnectPercent16("setUsbAfOutputSpin", funcUSBAFOutputLevel);
    set7610ConnectPercent16("setUsbIfOutputSpin", funcUSBIFOutputLevel);
    set7610ConnectComboU8("setLanOutputSelect", funcLANOutputSelect);
    set7610ConnectComboU8("setRefInputCombo", funcREFInput);
    // IC7610_V37_SET_SIGNALS
    // IC7610_V43_FINAL_RTTY_REHOME
    // Find the target group by title. Do not rely on construction-scope
    // local variables, which is what broke the discarded early v43.
    QGroupBox *v43RttyConfigGroup = Q_NULLPTR;

    const QList<QGroupBox*> v43Groups =
            findChildren<QGroupBox*>();

    for (QGroupBox *group : v43Groups) {
        if (group != Q_NULLPTR &&
            group->title() == tr("RTTY Configuration")) {
            v43RttyConfigGroup = group;
            break;
        }
    }

    QFormLayout *v43RttyConfigForm =
            v43RttyConfigGroup
                ? qobject_cast<QFormLayout*>(
                    v43RttyConfigGroup->layout())
                : Q_NULLPTR;

    auto v43DetachFormField =
            [=](QWidget *field) {
        if (field == Q_NULLPTR)
            return;

        const QList<QFormLayout*> forms =
                findChildren<QFormLayout*>();

        for (QFormLayout *form : forms) {
            if (form == Q_NULLPTR)
                continue;

            int row = -1;
            QFormLayout::ItemRole role =
                    QFormLayout::FieldRole;

            form->getWidgetPosition(
                    field,
                    &row,
                    &role);

            if (row < 0)
                continue;

            QFormLayout::TakeRowResult taken =
                    form->takeRow(row);

            QWidget *oldLabel =
                    taken.labelItem
                        ? taken.labelItem->widget()
                        : Q_NULLPTR;

            if (oldLabel != Q_NULLPTR)
                oldLabel->deleteLater();

            delete taken.labelItem;
            delete taken.fieldItem;
            return;
        }
    };

    if (v43RttyConfigForm != Q_NULLPTR) {
        QComboBox *v43RttyMark =
                findChild<QComboBox*>(
                    QStringLiteral("setRttyMarkCombo"));
        QComboBox *v43RttyShift =
                findChild<QComboBox*>(
                    QStringLiteral("setRttyShiftCombo"));
        QComboBox *v43RttyPolarity =
                findChild<QComboBox*>(
                    QStringLiteral("setRttyPolarityCombo"));
        QComboBox *v43RttyUsbKey =
                findChild<QComboBox*>(
                    QStringLiteral("setUsbKeyRttyCombo"));
        QCheckBox *v43RttyKeypad =
                findChild<QCheckBox*>(
                    QStringLiteral("setExternalKeypadRttyChk"));
        QComboBox *v43RttyDecodeBaud =
                findChild<QComboBox*>(
                    QStringLiteral("setDecodeBaudCombo"));

        v43DetachFormField(v43RttyMark);
        v43DetachFormField(v43RttyShift);
        v43DetachFormField(v43RttyPolarity);
        v43DetachFormField(v43RttyUsbKey);
        v43DetachFormField(v43RttyKeypad);
        v43DetachFormField(v43RttyDecodeBaud);

        if (v43RttyMark != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("RTTY Mark Frequency"),
                v43RttyMark);

        if (v43RttyShift != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("RTTY Shift Width"),
                v43RttyShift);

        if (v43RttyPolarity != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("RTTY Keying Polarity"),
                v43RttyPolarity);

        if (v43RttyUsbKey != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("USB Keying (RTTY)"),
                v43RttyUsbKey);

        if (v43RttyKeypad != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("External Keypad RTTY"),
                v43RttyKeypad);

        if (v43RttyDecodeBaud != Q_NULLPTR)
            v43RttyConfigForm->addRow(
                tr("Decoded Data Baud Rate"),
                v43RttyDecodeBaud);
    }
// IC7610_V39_RTTY_SET_SIGNALS
    set7610ConnectCheckU8(
        "setExternalKeypadRttyChk", func7610ExternalKeypadRTTY);
    set7610ConnectComboU8(
        "setDecodeBaudCombo", func7610DecodeBaud);
    set7610ConnectComboU8(
        "setRttyFftAvgCombo", func7610RTTYFFTAverage);
    set7610ConnectCheckU8(
        "setRttyDecodeUsosChk", func7610RTTYDecodeUSOS);
    set7610ConnectComboU8(
        "setRttyNewLineCombo", func7610RTTYNewLine);
    set7610ConnectComboU8(
        "setRttyDiddleCombo", func7610RTTYDiddle);
    set7610ConnectCheckU8(
        "setRttyTxUsosChk", func7610RTTYTXUSOS);
    set7610ConnectCheckU8(
        "setRttyAutoCrlfChk", func7610RTTYAutoCRLF);
    set7610ConnectCheckU8(
        "setRttyTimestampChk", func7610RTTYTimestamp);
    set7610ConnectComboU8(
        "setRttyTimestampTimeCombo", func7610RTTYTimestampTime);
    set7610ConnectCheckU8(
        "setRttyTimestampFreqChk", func7610RTTYTimestampFreq);
    set7610ConnectCheckU8(
        "setRttyDecodeLogChk", func7610RTTYDecodeLog);
    set7610ConnectComboU8(
        "setRttyLogFileTypeCombo", func7610RTTYLogFileType);
    set7610ConnectComboU8(
        "setCivTimeoutCombo", func7610CIVTimeout);

    set7610ConnectComboU8(
        "setRttyMarkCombo", func7610RTTYMarkFreq);
    set7610ConnectComboU8(
        "setRttyShiftCombo", func7610RTTYShiftWidth);
    set7610ConnectComboU8(
        "setRttyPolarityCombo", func7610RTTYKeyPolarity);

    set7610ConnectCheckU8(
        "setCwSyncChk", func7610SSBCWSyncTune);
    set7610ConnectComboU8(
        "setCwNormalSideCombo", func7610CWNormalSide);

    set7610ConnectComboU8(
        "setUsbKeyCwCombo", func7610USBKeyCW);
    set7610ConnectComboU8(
        "setUsbKeyRttyCombo", func7610USBKeyRTTY);

    set7610ConnectComboU16(
        "setCivRemoteAddrCombo", func7610CIVRemoteAddress);
    set7610ConnectComboU8(
        "setCivUsbPortCombo", func7610CIVUSBPort);
    set7610ConnectCheckU8(
        "setCivUsbEchoChk", func7610CIVUSBEcho);

    set7610ConnectPercent16(
        "setSideToneLevelSpin", func7610SideToneLevel);
    set7610ConnectCheckU8(
        "setSideToneLimitChk", func7610SideToneLimit);
    set7610ConnectSpinU8(
        "setKeyerRepeatSpin", func7610KeyerRepeat, 0);

    set7610ConnectComboU8(
        "setCwRiseCombo", func7610CWRiseTime);
    set7610ConnectComboU8(
        "setPaddlePolarityCombo", func7610PaddlePolarity);
    set7610ConnectComboU8(
        "setKeyTypeCombo", func7610KeyType);
    set7610ConnectCheckU8(
        "setMicUpDownKeyerChk", func7610MicUpDownKeyer);

    {
        QCheckBox *showVa =
                findChild<QCheckBox*>(QStringLiteral("setShowVaMeterChk"));
        QWidget *vaRow =
                findChild<QWidget*>(QStringLiteral("ic7610VaMeterRow"));
        if (showVa != Q_NULLPTR && vaRow != Q_NULLPTR) {
            showVa->blockSignals(true);
            showVa->setChecked(vaRow->isVisible());
            showVa->blockSignals(false);
            connect(showVa, &QCheckBox::toggled, this, [=](bool checked) {
                vaRow->setVisible(checked);
            });
        }
    }
    // IC7610_V23_SET_SIGNAL_HELPERS_END
// IC7610_EXT_SIGNALS_END

    // Slider tooltip
    connect(ui->txPowerSlider, &QSlider::sliderMoved,
        [&](int value) {
          QToolTip::showText(QCursor::pos(), QString("%1").arg(value*100/255), nullptr);
        });

    // Meter widgets
    connect(ui->meterSPoWidget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter1Type = meterTypeRequested;
        setupui->updateIfPref(if_meter1Type);
        // Change the meter locally:
        qInfo() << "New meter type" << meterTypeRequested;
        changeMeterType(meterTypeRequested, 1);
    });

    connect(ui->meter2Widget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter2Type = meterTypeRequested;
        setupui->updateIfPref(if_meter2Type);
        // Change the meter locally:
        changeMeterType(meterTypeRequested, 2);
        if (meterTypeRequested == meterComp)
            ui->meter2Widget->setCompReverse(prefs.compMeterReverse);
        // Block duplicate meter selection in the other meter:
        ui->meter3Widget->blockMeterType(meterTypeRequested);
    });

    connect(ui->meter3Widget, &meter::configureMeterSignal, this,
            [=](const meter_t &meterTypeRequested) {
        // Change the preferences and update settings widget to reflect new meter selection:
        prefs.meter3Type = meterTypeRequested;
        setupui->updateIfPref(if_meter3Type);
        // Change the meter locally:
        changeMeterType(meterTypeRequested, 3);
        if (meterTypeRequested == meterComp)
            ui->meter3Widget->setCompReverse(prefs.compMeterReverse);
        // Block duplicate meter selection in the other meter:
        ui->meter2Widget->blockMeterType(meterTypeRequested);
    });


}


void wfmain::enableControls(bool en)
{
    // This should contain all of the controls within the main window (other than power buttons)
    // So should be updated if any are added/removed/renamed.

    ui->freqDial->setEnabled(en);
    ui->tuningStepCombo->setEnabled(en);
    ui->tuneLockChk->setEnabled(en);
    ui->ritEnableChk->setEnabled(en);
    ui->ritTuneDial->setEnabled(en);
    ui->afGainSlider->setEnabled(en);
    ui->micGainSlider->setEnabled(en);
    ui->monitorSlider->setEnabled(en);
    ui->rfGainSlider->setEnabled(en);
    ui->sqlSlider->setEnabled(en);
    ui->txPowerSlider->setEnabled(en);

    ui->OtherControlsGrp->setEnabled(en);

    ui->cwButton->setEnabled(en);
    ui->memoriesBtn->setEnabled(en);
    ui->rptSetupBtn->setEnabled(en);
    ui->transmitBtn->setEnabled(en);
    ui->tuneEnableChk->setEnabled(en);
    ui->tuneNowBtn->setEnabled(en);

    ui->scopeSettingsGroup->setEnabled(en);
    ui->preampAttGroup->setEnabled(en);
    ui->antennaGroup->setEnabled(en);

    ui->meterSPoWidget->setEnabled(en);
    ui->meter2Widget->setEnabled(en);
    ui->meter3Widget->setEnabled(en);

    ui->showBandsBtn->setEnabled(en);
    ui->showFreqBtn->setEnabled(en);
}

void wfmain::updatedQueueInterval(qint64 interval)
{
    if (interval == -1)
        enableControls(false);
    else if (rigCaps != Q_NULLPTR)
        enableControls(true);
}


/* USB Hotplug support added at the end of the file for convenience */
#ifdef USB_HOTPLUG

#ifdef Q_OS_WINDOWS
bool wfmain::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    if (QDateTime::currentMSecsSinceEpoch() > lastUsbNotify + 10)
    {
        static bool created = false;

        MSG * msg = static_cast< MSG * > (message);
        switch (msg->message)
        {
        case WM_PAINT:
        {
            if (!created) {
                    GUID InterfaceClassGuid = {0x745a17a0, 0x74d3, 0x11d0,{ 0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda}};
                    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;
                    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
                    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
                    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
                    NotificationFilter.dbcc_classguid = InterfaceClassGuid;
                    HWND hw = (HWND) this->effectiveWinId();   //Main window handle
                    RegisterDeviceNotification(hw,&NotificationFilter, DEVICE_NOTIFY_ALL_INTERFACE_CLASSES );
                    created = true;
            }
            break;
        }
        case WM_DEVICECHANGE:
        {
            switch (msg->wParam) {
            case DBT_DEVICEARRIVAL:
            case DBT_DEVICEREMOVECOMPLETE:
                    emit usbHotplug();
                    lastUsbNotify = QDateTime::currentMSecsSinceEpoch();
                    break;
            case DBT_DEVNODES_CHANGED:
                    break;
            default:
                    break;
            }

            return true;
            break;
        }
        default:
            break;
        }
    }
    return false; // Process native events as normal
}

#elif defined(Q_OS_LINUX)

void wfmain::uDevEvent()
{
    udev_device *dev = udev_monitor_receive_device(uDevMonitor);
    if (dev)
    {
        const char* action = udev_device_get_action(dev);
        if (action && strcmp(action, "add") == 0 && QDateTime::currentMSecsSinceEpoch() > lastUsbNotify + 10)
        {
            emit usbHotplug();
            lastUsbNotify = QDateTime::currentMSecsSinceEpoch();
        }
        udev_device_unref(dev);
    }
}
#endif
#endif

