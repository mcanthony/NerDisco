#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "ImageOperations.h"
#include "QtSpinBoxAction.h"
#include "ParameterQtConnect.h"

#include <QPainter>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMessageBox>


MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, ui(new Ui::MainWindow)
	, m_midiInterface(MIDIInterface::getInstance())
	, previewInterval("previewInterval", 33, 20, 100)
	, previewWidth("previewWidth", 128, 32, 256)
	, previewHeight("previewHeight", 72, 18, 148)
	, displayInterval("displayInterval", 50, 20, 100)
	, displayWidth("displayWidth", 32, 16, 64)
	, displayHeight("displayHeight", 16, 8, 32)
	, displayBrightness("displayBrightness", 0.0f, -1.0f, 1.0f)
	, displayContrast("displayContrast", 0.0f, -1.0f, 1.0f)
	, displayGamma("displayGamma", 2.2f, 1.5f, 3.5f)
	, crossFadeValue("displayGamma", 0.0f, 0.0f, 1.0f)
{
	ui->setupUi(this);
	ui->widgetDeckA->setDeckName("DeckA");
	ui->widgetDeckB->setDeckName("DeckB");
	//connect preview gamma/brightness/contrast/crossfade slider to parameter and register for MIDI interaction
	connectParameter(crossFadeValue, ui->horizontalSliderCrossfade);
	m_midiInterface->getParameterMapping()->registerMIDIParameter(crossFadeValue.GetSharedParameter());
	connectParameter(displayBrightness, ui->horizontalSliderBrightness);
	connectParameter(displayContrast, ui->horizontalSliderContrast);
	connectParameter(displayGamma, ui->horizontalSliderGamma);
	m_midiInterface->getParameterMapping()->registerMIDIParameter(displayBrightness.GetSharedParameter());
	m_midiInterface->getParameterMapping()->registerMIDIParameter(displayContrast.GetSharedParameter());
	m_midiInterface->getParameterMapping()->registerMIDIParameter(displayGamma.GetSharedParameter());
	//update audio devices
	connect(m_audioInterface.captureDevice.GetSharedParameter().get(), SIGNAL(valueChanged(const QString &)), this, SLOT(audioInputDeviceChanged(const QString &)));
	connect(ui->actionAudioRecord, SIGNAL(triggered(bool)), this, SLOT(audioRecordTriggered(bool)));
	connect(ui->actionAudioStop, SIGNAL(triggered()), this, SLOT(audioStopTriggered()));
	connect(m_audioInterface.capturing.GetSharedParameter().get(), SIGNAL(valueChanged(bool)), this, SLOT(audioCaptureStateChanged(bool)));
	connect(&m_audioInterface, SIGNAL(levelData(const QVector<float>&, float)), this, SLOT(audioUpdateLevels(const QVector<float>&, float)));
	updateAudioDevices();
	//update midi devices
	connect(m_midiInterface->getDeviceInterface()->captureDevice.GetSharedParameter().get(), SIGNAL(valueChanged(const QString &)), this, SLOT(midiInputDeviceChanged(const QString &)));
	connect(ui->actionMidiStart, SIGNAL(triggered(bool)), this, SLOT(midiStartTriggered(bool)));
	connect(ui->actionMidiStop, SIGNAL(triggered()), this, SLOT(midiStopTriggered()));
	connect(m_midiInterface->getDeviceInterface()->capturing.GetSharedParameter().get(), SIGNAL(valueChanged(bool)), this, SLOT(midiCaptureStateChanged(bool)));
	updateMidiDevices();
	//connect slot to start the midi mapping process
	connect(ui->actionMidiLearnMapping, SIGNAL(triggered()), this, SLOT(midiLearnMappingToggled()));
	connect(ui->actionStoreLearnedConnection, SIGNAL(triggered()), this, SLOT(midiStoreLearnedConnection()));
	connect(m_midiInterface->getParameterMapping(), SIGNAL(learnedConnectionStateChanged(bool)), this, SLOT(midiLearnedConnectionStateChanged(bool)));
	//connect menu actions
	connect(ui->actionSaveDeckA, SIGNAL(triggered()), this, SLOT(saveDeckA()));
	connect(ui->actionSaveAsDeckA, SIGNAL(triggered()), this, SLOT(saveAsDeckA()));
	connect(ui->actionSaveDeckB, SIGNAL(triggered()), this, SLOT(saveDeckB()));
	connect(ui->actionSaveAsDeckB, SIGNAL(triggered()), this, SLOT(saveAsDeckB()));
	connect(ui->actionExit, SIGNAL(triggered()), this, SLOT(exitApplication()));
	//update the menu showing the effect files
	updateEffectMenu();
	//connect the parameters in the decks to parameters here
	connectParameter(ui->widgetDeckA->updateInterval, previewInterval);
	connectParameter(ui->widgetDeckA->previewWidth, previewWidth);
	connectParameter(ui->widgetDeckA->previewHeight, previewHeight);
	connectParameter(ui->widgetDeckA->updateInterval, previewInterval);
	connectParameter(ui->widgetDeckB->previewWidth, previewWidth);
	connectParameter(ui->widgetDeckB->previewHeight, previewHeight);
	updateDeckMenu();
	//connect parameters for preview resolution here
	connect(previewWidth.GetSharedParameter().get(), SIGNAL(valueChanged(int)), this, SLOT(setFramebufferWidth(int)));
	connect(previewHeight.GetSharedParameter().get(), SIGNAL(valueChanged(int)), this, SLOT(setFramebufferHeight(int)));
	//connect parameters in the image converter to parameters here
	connectParameter(m_displayImageConverter.crossfadeValue, crossFadeValue);
	connectParameter(m_displayImageConverter.displayBrightness, displayBrightness);
	connectParameter(m_displayImageConverter.displayContrast, displayContrast);
	connectParameter(m_displayImageConverter.displayGamma, displayGamma);
	connectParameter(m_displayImageConverter.displayWidth, displayWidth);
	connectParameter(m_displayImageConverter.displayHeight, displayHeight);
	connect(&m_displayImageConverter, SIGNAL(previewImageChanged(const QImage &)), this, SLOT(updatePreview(const QImage &)));
	connect(&m_displayImageConverter, SIGNAL(displayImageChanged(const QImage &)), this, SLOT(updateDisplay(const QImage &)));
	//set up serial display sending thread
	updateDisplayMenu();
	connectParameter(m_displayThread.displayWidth, displayWidth);
	connectParameter(m_displayThread.displayHeight, displayHeight);
	connectParameter(m_displayThread.displayInterval, displayInterval);
	connectParameter(m_displayThread.flipHorizontal, ui->actionDisplayFlipHorizontal);
	connectParameter(m_displayThread.flipVertical, ui->actionDisplayFlipVertical);
	connectParameter(m_displayThread.sending, ui->actionDisplayStart);	
	connect(ui->actionDisplayStart, SIGNAL(triggered(bool)), this, SLOT(displayStartSending(bool)));
	connect(ui->actionDisplayStop, SIGNAL(triggered()), this, SLOT(displayStopSending()));
	connect(&m_displayThread, SIGNAL(portOpened(bool)), this, SLOT(displayPortStatusChanged(bool)));
	connect(m_displayThread.sending.GetSharedParameter().get(), SIGNAL(valueChanged(bool)), this, SLOT(displaySendStatusChanged(bool)));
	m_displayThread.start();
	//retrieve settings from XML for all components
	loadSettings();
	//set up signal joiner that waits for both render windows being finished with rendering to grab their framebuffers
	m_signalJoiner.addObjectToJoin(ui->widgetDeckA);
	m_signalJoiner.addObjectToJoin(ui->widgetDeckB);
	connect(ui->widgetDeckA, SIGNAL(renderingFinished()), &m_signalJoiner, SLOT(notify()));
	connect(ui->widgetDeckB, SIGNAL(renderingFinished()), &m_signalJoiner, SLOT(notify()));
	connect(&m_signalJoiner, SIGNAL(joined()), this, SLOT(grabDeckImages()));
	//set up timer for grabbing the composite image
	connect(&m_displayTimer, SIGNAL(timeout()), this, SLOT(updateDeckImages()));
	m_displayTimer.start(displayInterval);
}

MainWindow::~MainWindow()
{
	//stop display refresh and audio capturing
	m_displayTimer.stop();
	m_audioInterface.capturing = false;
	//save settings to XML
	saveSettings();
	delete ui;
}

void MainWindow::toXML(QDomElement & parent) const
{
	//try to find element in document
	QDomElement element = parent.firstChildElement("General");
	if (element.isNull())
	{
		//add the new element
		element = parent.ownerDocument().createElement("General");
		parent.appendChild(element);
	}
	previewWidth.toXML(element);
	previewHeight.toXML(element);
	displayInterval.toXML(element);
	displayWidth.toXML(element);
	displayHeight.toXML(element);
	displayBrightness.toXML(element);
	displayContrast.toXML(element);
	displayGamma.toXML(element);
	crossFadeValue.toXML(element);
}

MainWindow & MainWindow::fromXML(const QDomElement & parent)
{
	//try to find element in document
	QDomElement element = parent.firstChildElement("General");
	if (element.isNull())
	{
		throw std::runtime_error("No general settings found!");
	}
	//read settings from element
	previewWidth.fromXML(element);
	previewHeight.fromXML(element);
	displayInterval.fromXML(element);
	displayWidth.fromXML(element);
	displayHeight.fromXML(element);
	displayBrightness.fromXML(element);
	displayContrast.fromXML(element);
	displayGamma.fromXML(element);
	crossFadeValue.fromXML(element);
	return *this;
}

void MainWindow::loadSettings(const QString & fileName)
{
	QDomDocument doc("NerDisco");
	QFile file(fileName);
	if (file.open(QIODevice::ReadOnly))
	{
		QString errorMessage;
		if (doc.setContent(&file, &errorMessage))
		{
			QDomElement root = doc.documentElement();
			try
			{
				m_displayImageConverter.fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				m_displayThread.fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				m_audioInterface.fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				m_midiInterface->getDeviceInterface()->fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				m_midiInterface->getParameterMapping()->fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				ui->widgetDeckA->fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				ui->widgetDeckB->fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
			try
			{
				fromXML(root);
			}
			catch (std::runtime_error e)
			{
				QMessageBox::information(this, tr("Failed to read settings"), tr("Error while reading settings from \"%1\". %2").arg(fileName).arg(e.what()));
			}
		}
		else
		{
			QMessageBox::information(this, tr("Failed to read settings"), tr("Error reading \"%1\": %2. Using default settings.").arg(fileName).arg(errorMessage));
		}
		file.close();
	}
	else
	{
		QMessageBox::information(this, tr("Failed to read settings"), tr("Failed to open \"%1\". Using default settings.").arg(fileName));
	}
}

void MainWindow::saveSettings(const QString & fileName)
{
	QDomDocument doc("NerDisco");
	QFile file(fileName);
	if (file.open(QIODevice::ReadWrite))
	{
		//try reading the file. if this fails, we don't care, we'll fill it now...
		QDomElement root;
		if (doc.setContent(&file))
		{
			root = doc.documentElement();
		}
		if (root.isNull())
		{
			//failed reading the file. create and add root node.
			root = doc.createElement("NerDisco");
			doc.appendChild(root);
		}
		//now store everything in root element
		try
		{
			m_displayImageConverter.toXML(root);
			m_displayThread.toXML(root);
			m_audioInterface.toXML(root);
			m_midiInterface->getDeviceInterface()->toXML(root);
			m_midiInterface->getParameterMapping()->toXML(root);
			ui->widgetDeckA->toXML(root);
			ui->widgetDeckB->toXML(root);
			toXML(root);
			file.resize(0);
			file.reset();
			file.write(doc.toByteArray());
		}
		catch (std::runtime_error e)
		{
			QMessageBox::information(this, tr("Failed to save settings"), tr("Error while saving settings to \"%1\". %2").arg(fileName).arg(e.what()));
		}
		file.close();
	}
	else
	{
		QMessageBox::information(this, tr("Failed to save settings"), tr("Failed to open \"%1\". Using default settings.").arg(fileName));
	}
}

void MainWindow::exitApplication()
{
	close();
}

//-------------------------------------------------------------------------------------------------

void MainWindow::setFramebufferWidth(int width)
{
	if (previewWidth != width)
	{
		previewWidth = width;
		ui->labelFinalImage->setFixedSize(previewWidth, previewHeight);
		ui->labelRealImage->setFixedSize(previewWidth, previewHeight);
	}
}

void MainWindow::setFramebufferHeight(int height)
{
	if (previewHeight != height)
	{
		previewHeight = height;
		ui->labelFinalImage->setFixedSize(previewWidth, previewHeight);
		ui->labelRealImage->setFixedSize(previewWidth, previewHeight);
	}
}

//-------------------------------------------------------------------------------------------------

void MainWindow::updateAudioDevices()
{
	//clear old menu
	QMenu * oldMenu = ui->actionAudioDevices->menu();
	if (oldMenu)
	{
        oldMenu->setParent(NULL);
		delete oldMenu;
        ui->actionAudioDevices->setMenu(NULL);
	}
	//add default device
	QMenu * deviceMenu = new QMenu(this);
	QAction * action = deviceMenu->addAction(tr("None"));
	action->setCheckable(true);
	deviceMenu->addAction(action);
	connect(action, SIGNAL(triggered()), this, SLOT(audioInputDeviceSelected()));
	//add actual devices
	QStringList inputDevices = AudioInterface::inputDeviceNames();
	for (int i = 0; i < inputDevices.size(); ++i)
	{
		action = deviceMenu->addAction(inputDevices.at(i));
		action->setCheckable(true);
		deviceMenu->addAction(action);
		connect(action, SIGNAL(triggered()), this, SLOT(audioInputDeviceSelected()));
		//if this is the active audio device, select it
		action->setChecked(inputDevices.at(i) == m_audioInterface.captureDevice);
	}
	//add menu to UI
	ui->actionAudioDevices->setMenu(deviceMenu);
}

void MainWindow::audioInputDeviceSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		m_audioInterface.capturing = false;
		if (action->text() == tr("None"))
		{
			m_audioInterface.captureDevice = "";
		}
		else
		{
			m_audioInterface.captureDevice = action->text();
		}
	}
}

void MainWindow::audioInputDeviceChanged(const QString & name)
{
	//disable buttons if not audio device selected
	ui->actionAudioRecord->setEnabled(name != "");
	ui->actionAudioStop->setEnabled(name != "");
	//check which action to select
	QMenu * menu = ui->actionAudioDevices->menu();
	if (menu && menu->actions().size() > 0)
	{
        foreach (QAction * action, menu->actions())
		{
			action->setChecked(action->text() == name || (action->text() == tr("None") && name == ""));
		}
	}
}

void MainWindow::audioRecordTriggered(bool checked)
{
	m_audioInterface.capturing = checked;
}

void MainWindow::audioStopTriggered()
{
	m_audioInterface.capturing = false;
}

void MainWindow::audioCaptureStateChanged(bool capturing)
{
	ui->actionAudioRecord->setChecked(capturing);
}

void MainWindow::audioUpdateLevels(const QVector<float> & data, float /*timeus*/)
{
    //qDebug() << "Audio data arrived" << timeus / 1000;
	QImage image(ui->labelSpectrumImage->size(), QImage::Format_ARGB32);
	QPainter painter(&image);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(image.rect(), Qt::black);
	painter.fillRect(QRect(0, 0, image.width() / 2, image.height() * data.at(0)), Qt::green);
	//painter.fillRect(QRect(image.width() / 2, 0, image.width() / 2, image.height() * data.at(1)), Qt::green);
	/*const int sampleCount = data.size();
	const int samplesPerPixel = (float)sampleCount / (float)image.width() < 0 ? 1 : (float)sampleCount / (float)image.width();
	for (int x = 0; x < image.width(); ++x)
	{
		float value = 0.0f;
		float dataStart = ((float)x * (float)sampleCount) / (float)image.width();
		for (int i = 0; i < samplesPerPixel; ++i)
		{
			value += data[dataStart + i];
		}
		value /= (float)samplesPerPixel;
		image.setPixel(x, value * image.height(), 0xFF0000FF);
	}*/
	ui->labelSpectrumImage->setPixmap(QPixmap::fromImage(image));
	ui->labelSpectrumImage->update();
}

//-------------------------------------------------------------------------------------------------

void MainWindow::updateMidiDevices()
{
	//clear old menu
	QMenu * oldMenu = ui->actionMidiDevices->menu();
	if (oldMenu)
	{
		oldMenu->setParent(NULL);
		delete oldMenu;
		ui->actionMidiDevices->setMenu(NULL);
	}
	//add default device
	QMenu * deviceMenu = new QMenu(this);
	QAction * action = deviceMenu->addAction(tr("None"));
	action->setCheckable(true);
	deviceMenu->addAction(action);
	connect(action, SIGNAL(triggered()), this, SLOT(midiInputDeviceSelected()));
	//add actual devices
	QStringList midiDevices = m_midiInterface->getDeviceInterface()->inputDeviceNames();
	for (int i = 0; i < midiDevices.size(); ++i)
	{
		action = deviceMenu->addAction(midiDevices.at(i));
		action->setCheckable(true);
		deviceMenu->addAction(action);
		connect(action, SIGNAL(triggered()), this, SLOT(midiInputDeviceSelected()));
		//if this is the active midi device, select it
		action->setChecked(midiDevices.at(i) == m_midiInterface->getDeviceInterface()->captureDevice);
	}
	//add menu to UI
	ui->actionMidiDevices->setMenu(deviceMenu);
}

void MainWindow::midiInputDeviceSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		m_midiInterface->getDeviceInterface()->capturing = false;
		if (action->text() == tr("None"))
		{
			m_midiInterface->getDeviceInterface()->captureDevice = "";
		}
		else
		{
			m_midiInterface->getDeviceInterface()->captureDevice = action->text();
		}
	}
}

void MainWindow::midiInputDeviceChanged(const QString & name)
{
	//disable buttons if not audio device selected
	ui->actionMidiStart->setEnabled(name != "");
	ui->actionMidiStop->setEnabled(name != "");
	//check which action to select
	QMenu * menu = ui->actionMidiDevices->menu();
	if (menu && menu->actions().size() > 0)
	{
		foreach(QAction * action, menu->actions())
		{
			action->setChecked(action->text() == name || (action->text() == tr("None") && name == ""));
		}
	}
}

void MainWindow::midiStartTriggered(bool checked)
{
	m_midiInterface->getDeviceInterface()->capturing = checked;
}

void MainWindow::midiStopTriggered()
{
	m_midiInterface->getDeviceInterface()->capturing = false;
}

void MainWindow::midiCaptureStateChanged(bool capturing)
{
	ui->actionMidiStart->setChecked(capturing);
	ui->actionMidiLearnMapping->setEnabled(capturing);
}

//-------------------------------------------------------------------------------------------------

void MainWindow::midiLearnMappingToggled()
{
	if (m_midiInterface->getParameterMapping()->learnMode)
	{
		//stop midi mapping mode
		m_midiInterface->getParameterMapping()->learnMode = false;
		ui->actionMidiLearnMapping->setChecked(false);
	}
	else
	{
		//start midi mapping mode
		if (m_midiInterface->getDeviceInterface()->capturing)
		{
			//connect slots to detect value changes in decks
			m_midiInterface->getParameterMapping()->learnMode = true;
		}
		ui->actionMidiLearnMapping->setChecked(m_midiInterface->getDeviceInterface()->capturing);
	}
}

void MainWindow::midiLearnedConnectionStateChanged(bool valid)
{
	ui->actionStoreLearnedConnection->setEnabled(m_midiInterface->getParameterMapping()->learnMode && valid);
}

void MainWindow::midiStoreLearnedConnection()
{
	m_midiInterface->getParameterMapping()->storeLearnedConnection();
}

//-------------------------------------------------------------------------------------------------

void MainWindow::updateDisplayMenu()
{
	//clear old menu
	QMenu * oldMenu = ui->actionDisplaySerialPort->menu();
	if (oldMenu)
	{
		oldMenu->setParent(NULL);
		delete oldMenu;
		ui->actionDisplaySerialPort->setMenu(NULL);
	}
	//add default device
	QMenu * deviceMenu = new QMenu(this);
	QAction * action = deviceMenu->addAction(tr("None"));
	action->setCheckable(true);
	deviceMenu->addAction(action);
	connect(action, SIGNAL(triggered()), this, SLOT(displaySerialPortSelected()));
	//add actual devices
	QStringList serialPorts = m_displayThread.getAvailablePortNames();
	for (int i = 0; i < serialPorts.size(); ++i)
	{
		action = deviceMenu->addAction(serialPorts.at(i));
		action->setCheckable(true);
		deviceMenu->addAction(action);
		connect(action, SIGNAL(triggered()), this, SLOT(displaySerialPortSelected()));
		//if this is the active port, select it
		action->setChecked(serialPorts.at(i) == m_displayThread.portName);
	}
	//add menu to UI
	ui->actionDisplaySerialPort->setMenu(deviceMenu);
	//baud rates to menu
	const int rates[7] = { 57600, 115200, 230400, 250000, 300000, 400000, 500000 };
	for (int i = 0; i < 7; ++i)
	{
		action = ui->menuDisplayBaudrate->addAction(QString::number(rates[i]) + " Baud");
		action->setCheckable(true);
		action->setData(rates[i]);
		connect(action, SIGNAL(triggered()), this, SLOT(displayBaudrateSelected()));
	}
	//add scanline directions data to menu
	ui->actionConstantLeftRight->setData(ScanlineDirection::ConstantLeftToRight);
	connect(ui->actionConstantLeftRight, SIGNAL(triggered()), this, SLOT(displayScanlineDirectionSelected()));
	ui->actionConstantRightLeft->setData(ScanlineDirection::ConstantRightToLeft);
	connect(ui->actionConstantRightLeft, SIGNAL(triggered()), this, SLOT(displayScanlineDirectionSelected()));
	ui->actionAlternatingStartLeft->setData(ScanlineDirection::AlternatingStartLeft);
	connect(ui->actionAlternatingStartLeft, SIGNAL(triggered()), this, SLOT(displayScanlineDirectionSelected()));
	ui->actionAlternatingStartRight->setData(ScanlineDirection::AlternatingStartRight);
	connect(ui->actionAlternatingStartRight, SIGNAL(triggered()), this, SLOT(displayScanlineDirectionSelected()));
	//add spinbox actions to menu
	QtSpinBoxAction * intervalAction = new QtSpinBoxAction("Update interval", "ms");
	intervalAction->setObjectName("displayInterval");
	ui->menuDisplaySettings->insertAction(ui->actionDisplayStart, intervalAction);
	connectParameter(displayInterval, intervalAction->control());
	QtSpinBoxAction * widthAction = new QtSpinBoxAction("Display width");
	widthAction->setObjectName("displayWidth");
	ui->menuDisplaySettings->insertAction(ui->actionDisplayStart, widthAction);
	connectParameter(displayWidth, widthAction->control());
	QtSpinBoxAction * heightAction = new QtSpinBoxAction("Display height");
	heightAction->setObjectName("displayHeight");
	ui->menuDisplaySettings->insertAction(ui->actionDisplayStart, heightAction);
	connectParameter(displayHeight, heightAction->control());
}

void MainWindow::displaySerialPortSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		m_displayThread.sending = false;
		if (action->text() == tr("None"))
		{
			m_displayThread.portName = "";
		}
		else
		{
			m_displayThread.portName = action->text();
		}
	}
}

void MainWindow::displayBaudrateSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		m_displayThread.baudrate = action->data().toInt();
	}
}

void MainWindow::displayScanlineDirectionSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		m_displayThread.scanlineDirection = (ScanlineDirection)action->data().toInt();
	}
}

void MainWindow::displayStartSending(bool checked)
{
	m_displayThread.sending = checked;
}

void MainWindow::displayStopSending()
{
	m_displayThread.sending = false;
}

void MainWindow::displayPortStatusChanged(bool opened)
{
	ui->actionDisplayStart->setEnabled(opened);
	ui->actionDisplayStop->setEnabled(opened);
}

void MainWindow::displaySendStatusChanged(bool sending)
{
	ui->actionDisplayStart->setChecked(sending);
}

void MainWindow::displayFlipChanged(bool horizontal, bool vertical)
{
	ui->actionDisplayFlipHorizontal->setChecked(horizontal);
	ui->actionDisplayFlipVertical->setChecked(vertical);
}

//-------------------------------------------------------------------------------------------------

void MainWindow::updateDeckImages()
{
	//check if we're still waiting for one or both views to finish rendering
	if (!m_signalJoiner.isJoining())
	{
		ui->widgetDeckA->grabFramebufferAfterSwap();
		ui->widgetDeckB->grabFramebufferAfterSwap();
		m_signalJoiner.start();
		ui->widgetDeckA->render();
		ui->widgetDeckB->render();
	}
}

void MainWindow::grabDeckImages()
{
	m_signalJoiner.stop();
	//grab images from the decks and convert for display
	m_displayImageConverter.convertImages(ui->widgetDeckA->getGrabbedFramebuffer(), ui->widgetDeckB->getGrabbedFramebuffer());
}

void MainWindow::updatePreview(const QImage & image)
{
	ui->labelFinalImage->setPixmap(QPixmap::fromImage(image));
}

void MainWindow::updateDisplay(const QImage & image)
{
	m_displayThread.sendImage(image);
	ui->labelRealImage->setPixmap(QPixmap::fromImage(image.scaled(ui->labelFinalImage->size())));
}

QStringList buildFileList(const QString & path)
{
	QStringList list;
	QDirIterator it(path, QStringList() << "*.fs", QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		if (it.fileInfo().isDir())
		{
			list.append(buildFileList(it.path()));
		}
		else
		{
			list << it.next();
		}
	}
	return list;
}

void MainWindow::updateEffectMenu()
{
	//clear entries from deck a and b
	if (ui->actionLoadDeckA->menu())
	{
		ui->actionLoadDeckA->menu()->clear();
	}
	if (ui->actionLoadDeckA->menu())
	{
		ui->actionLoadDeckB->menu()->clear();
	}
	//find all fs files in the effects folder
	QStringList list = buildFileList("./effects");
	if (list.size() > 0)
	{
		//sort list first
		list.sort();
		//build new menus
		QMenu * menuA = new QMenu;
		QMenu * menuB = new QMenu;
		//add file actions to menu
		foreach (const QString & entry, list)
		{
			//remove base directory from string
			QString cleanName = entry;
			cleanName = cleanName.remove("./effects");
			QFileInfo info(cleanName);
			//check if this is a fs file
			if (info.suffix().toLower() == "fs")
			{
				//build menu actions
				QAction * actionA = menuA->addAction(info.baseName());
				actionA->setData(entry);
				connect(actionA, SIGNAL(triggered()), this, SLOT(loadDeckA()));
				QAction * actionB = menuB->addAction(info.baseName());
				actionB->setData(entry);
				connect(actionB, SIGNAL(triggered()), this, SLOT(loadDeckB()));
			}
		}
		//add new menus
		ui->actionLoadDeckA->setMenu(menuA);
		ui->actionLoadDeckB->setMenu(menuB);
	}
}

void MainWindow::updateDeckMenu()
{
	//add spinbox actions to menu
	QtSpinBoxAction * intervalActionA = new QtSpinBoxAction("Update interval", "ms");
	intervalActionA->setObjectName("updateInterval");
	ui->menuSettingsDeckA->addAction(intervalActionA);
	connectParameter(previewInterval, intervalActionA->control());
	QtSpinBoxAction * widthActionA = new QtSpinBoxAction("Preview width");
	widthActionA->setObjectName("previewWidth");
	ui->menuSettingsDeckA->addAction(widthActionA);
	connectParameter(previewWidth, widthActionA->control());
	QtSpinBoxAction * heightActionA = new QtSpinBoxAction("Preview height");
	heightActionA->setObjectName("previewHeight");
	ui->menuSettingsDeckA->addAction(heightActionA);
	connectParameter(previewHeight, heightActionA->control());
	//deck B
	QtSpinBoxAction * intervalActionB = new QtSpinBoxAction("Update interval", "ms");
	intervalActionB->setObjectName("updateInterval");
	ui->menuSettingsDeckB->addAction(intervalActionB);
	connectParameter(previewInterval, intervalActionB->control());
	QtSpinBoxAction * widthActionB = new QtSpinBoxAction("Preview width");
	widthActionB->setObjectName("previewWidth");
	ui->menuSettingsDeckB->addAction(widthActionB);
	connectParameter(previewWidth, widthActionB->control());
	QtSpinBoxAction * heightActionB = new QtSpinBoxAction("Preview height");
	heightActionB->setObjectName("previewHeight");
	ui->menuSettingsDeckB->addAction(heightActionB);
	connectParameter(previewHeight, heightActionB->control());
}

void MainWindow::loadDeckA(bool /*checked*/)
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		ui->widgetDeckA->loadScript(action->data().toString());
	}
}

void MainWindow::saveDeckA(bool /*checked*/)
{
	if (ui->widgetDeckA->saveScript())
	{
		updateEffectMenu();
	}
}

void MainWindow::saveAsDeckA(bool /*checked*/)
{
	if (ui->widgetDeckA->saveAsScript())
	{
		updateEffectMenu();
	}
}

void MainWindow::loadDeckB(bool /*checked*/)
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		ui->widgetDeckB->loadScript(action->data().toString());
	}
}

void MainWindow::saveDeckB(bool /*checked*/)
{
	if (ui->widgetDeckB->saveScript())
	{
		updateEffectMenu();
	}
}

void MainWindow::saveAsDeckB(bool /*checked*/)
{
	if (ui->widgetDeckB->saveAsScript())
	{
		updateEffectMenu();
	}
}

void MainWindow::showResponse(const QString &s)
{
	ui->statusbar->showMessage(s);
}

void MainWindow::processError(const QString &s)
{
	ui->statusbar->showMessage("Serial port error:" + s);
}

void MainWindow::processTimeout(const QString &s)
{
	ui->statusbar->showMessage("Serial port timeout:" +s);
}
