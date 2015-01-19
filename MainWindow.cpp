#include "MainWindow.h"
#include "ui_MainWindow.h"
#include "SettingsDialog.h"
#include "Settings.h"
#include "ColorOperations.h"

#include <QPainter>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>


MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	//showFullScreen();
	//retrieve settings
	Settings & settings = Settings::getInstance();
	//update audio devices
	connect(&m_audioInterface, SIGNAL(inputDeviceChanged(const QString &)), this, SLOT(audioInputDeviceChanged(const QString &)));
	connect(ui->actionAudioRecord, SIGNAL(triggered(bool)), this, SLOT(audioRecordTriggered(bool)));
	connect(ui->actionAudioStop, SIGNAL(triggered()), this, SLOT(audioStopTriggered()));
	connect(&m_audioInterface, SIGNAL(captureStateChanged(bool)), this, SLOT(audioCaptureStateChanged(bool)));
	connect(&m_audioInterface, SIGNAL(audioDataCaptured(const QVector<float>&, float)), this, SLOT(audioUpdateData(const QVector<float>&, float)));
	updateAudioDevices();
	//set up the final preview
	ui->labelFinalImage->setFixedSize(settings.frameBufferWidth(), settings.frameBufferHeight());
	ui->labelRealImage->setFixedSize(settings.frameBufferWidth(), settings.frameBufferHeight());
	//setup preview gamma/brighness/contrast
	ui->horizontalSliderGamma->setValue(settings.displayGamma() * 100.0f);
	ui->horizontalSliderBrightness->setValue(settings.displayBrightness() * 50.0f);
	ui->horizontalSliderContrast->setValue((settings.displayContrast() - 1.0f) * 50.0f);
	//connect preview gamma/brighness/contrast slider to update slot
	connect(ui->horizontalSliderGamma, SIGNAL(valueChanged(int)), this, SLOT(updateSettingsFromUi()));
	connect(ui->horizontalSliderBrightness, SIGNAL(valueChanged(int)), this, SLOT(updateSettingsFromUi()));
	connect(ui->horizontalSliderContrast, SIGNAL(valueChanged(int)), this, SLOT(updateSettingsFromUi()));
	//allocate images
	m_currentImage = QImage(settings.frameBufferWidth(), settings.frameBufferHeight(), QImage::Format_ARGB32);
	m_realImage = QImage(settings.displayWidth(), settings.displayHeight(), QImage::Format_ARGB32);
	//connect menu actions
	connect(ui->actionSaveDeckA, SIGNAL(triggered()), this, SLOT(saveDeckA()));
	connect(ui->actionSaveAsDeckA, SIGNAL(triggered()), this, SLOT(saveAsDeckA()));
	connect(ui->actionSaveDeckB, SIGNAL(triggered()), this, SLOT(saveDeckB()));
	connect(ui->actionSaveAsDeckB, SIGNAL(triggered()), this, SLOT(saveAsDeckB()));
	connect(ui->actionOptions, SIGNAL(triggered()), this, SLOT(showSettings()));
	connect(ui->actionBeenden, SIGNAL(triggered()), this, SLOT(exitApplication()));
	//update the menu showing the effect files
	updateMenu();
	//set up serial display sending thread
	m_displayThread.setPortName(settings.portName());
	m_displayThread.start();
	//set up timer for grabbing the composite image
	connect(&m_displayTimer, SIGNAL(timeout()), this, SLOT(updateCurrentImage()));
	m_displayTimer.start(settings.displayInterval());
}

MainWindow::~MainWindow()
{
	//stop display refresh and audio capturing
	m_displayTimer.stop();
	m_audioInterface.setCaptureState(false);
	//save settings
	Settings::getInstance().save();
	delete ui;
}

void MainWindow::showSettings()
{
	SettingsDialog dialog;
	dialog.exec();
}

void MainWindow::updateSettingsFromUi()
{
	//update settings from ui
	Settings & settings = Settings::getInstance();
	settings.setDisplayGamma(ui->horizontalSliderGamma->value() / 100.0f);
	settings.setDisplayBrightness(ui->horizontalSliderBrightness->value() / 50.0f);
	settings.setDisplayContrast(ui->horizontalSliderContrast->value() / 50.0f + 1.0f);
}

void MainWindow::exitApplication()
{
	close();
}

void MainWindow::updateAudioDevices()
{
	Settings & settings = Settings::getInstance();
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
    QAction * selected = NULL;
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
		if (inputDevices.at(i) == settings.inputAudioDeviceName())
		{
			selected = action;
		}
	}
	//add menu to UI
	ui->actionAudioDevices->setMenu(deviceMenu);
	//now select action
	if (selected)
	{
		action->trigger();
	}
}

void MainWindow::audioInputDeviceSelected()
{
	QAction * action = qobject_cast<QAction*>(sender());
	if (action)
	{
		qDebug() << "Device selected" << action->text();
		m_audioInterface.setCaptureState(false);
		if (action->text() == tr("None"))
		{
			m_audioInterface.setCurrentInputDevice("");
		}
		else
		{
			m_audioInterface.setCurrentInputDevice(action->text());
		}
		Settings & settings = Settings::getInstance();
		settings.setInputAudioDeviceName(action->text());
	}
}

void MainWindow::audioInputDeviceChanged(const QString & name)
{
	qDebug() << "Device changed" << name;
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
	qDebug() << "Record triggered";
	m_audioInterface.setCaptureState(checked);
}

void MainWindow::audioStopTriggered()
{
	qDebug() << "Stop triggered";
	m_audioInterface.setCaptureState(false);
}

void MainWindow::audioCaptureStateChanged(bool capturing)
{
	qDebug() << "Capture state changed" << capturing;
	ui->actionAudioRecord->setChecked(capturing);
}

void MainWindow::audioUpdateData(const QVector<float> & data, float timeus)
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
}

QImage MainWindow::currentImage() const
{
	return m_currentImage;
}

QImage  MainWindow::realImage() const
{
	return m_realImage;
}

void MainWindow::updateCurrentImage()
{
	//grab images from the decks
	QImage deckImageA = ui->widgetDeckA->grabFramebuffer();
	QImage deckImageB = ui->widgetDeckB->grabFramebuffer();
	//composite deck framebuffers into final image
	QPainter painter(&m_currentImage);
	painter.setCompositionMode(QPainter::CompositionMode_Source);
	painter.fillRect(m_currentImage.rect(), Qt::black);
	qreal alphaB = ui->horizontalSliderCrossfade->value() / 100.0;
	qreal alphaA = 1.0 - alphaB;
	if (alphaA <= alphaB)
	{
		painter.drawImage(0, 0, deckImageA);
		painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter.setOpacity(alphaB);
		painter.drawImage(0, 0, deckImageB);
	}
	else
	{
		painter.drawImage(0, 0, deckImageB);
		painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
		painter.setOpacity(alphaA);
		painter.drawImage(0, 0, deckImageA);
	}
	//scale image down to real size
	m_realImage = m_currentImage.scaled(m_realImage.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	//do image correction
	Settings & settings = Settings::getInstance();
	const float gamma = settings.displayGamma();
	const float brightness = settings.displayBrightness();
	const float contrast = settings.displayContrast();
	for (int y = 0; y < m_realImage.height(); ++y)
	{
		unsigned char * scanLine = m_realImage.scanLine(y);
		for (int x = 0; x < m_realImage.width()*4; ++x)
		{
			scanLine[x] = change(scanLine[x], brightness, contrast, gamma);
		}
	}
	//send image data to display
	m_displayThread.sendData(m_realImage);
	//draw images in UI
	ui->labelFinalImage->setPixmap(QPixmap::fromImage(m_currentImage));
	ui->labelRealImage->setPixmap(QPixmap::fromImage(m_realImage.scaled(m_currentImage.size())));
}

QStringList buildFileList(const QString & path)
{
	QStringList list;
	QDirIterator it(path, QStringList() << "*.qml", QDir::Files, QDirIterator::Subdirectories);
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

void MainWindow::updateMenu()
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
	//find all qml files in the effects folder
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
			//check if this is a QML file
			if (info.suffix().toLower() == "qml")
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
		updateMenu();
	}
}

void MainWindow::saveAsDeckA(bool /*checked*/)
{
	if (ui->widgetDeckA->saveAsScript())
	{
		updateMenu();
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
		updateMenu();
	}
}

void MainWindow::saveAsDeckB(bool /*checked*/)
{
	if (ui->widgetDeckB->saveAsScript())
	{
		updateMenu();
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