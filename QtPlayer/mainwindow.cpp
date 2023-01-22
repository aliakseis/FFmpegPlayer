#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include "videowidget.h"

#include <QFileDialog>
#include <QInputDialog>

MainWindow* getMainWindow()
{
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (auto *mainWindow = qobject_cast<MainWindow*>(widget)) {
            return mainWindow;
        }
    }
    return nullptr;
}


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    auto m_player = ui->videoPlayerWidget;

    m_player->setProgressbar(ui->videoProgress);

    ui->dockWidget->installEventFilter(m_player);
    ui->dockWidget->setDisplayForFullscreen(m_player->getCurrentDisplay());

    setCentralWidget(ui->dockWidget);

    connect(m_player->videoWidget(), &VideoWidget::leaveFullScreen, ui->dockWidget, &CustomDockWidget::onLeaveFullScreen);

    connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::onFileOpen);
    connect(ui->actionOpenUrl, &QAction::triggered, this, &MainWindow::onUrlOpen);
}

MainWindow::~MainWindow()
{
    delete ui;
}

CustomDockWidget* MainWindow::dockWidget()
{
    return ui->dockWidget;
}

QWidget* MainWindow::videoControlWidget()
{
    return ui->videoControl;
}

VideoPlayerWidget* MainWindow::getPlayer()
{
    return ui->videoPlayerWidget;
}

void MainWindow::onFileOpen()
{
    auto fileName = QFileDialog::getOpenFileName(
                 this,
                 tr("Open Video File"));
    if (!fileName.isEmpty())
    {
        getPlayer()->playFile(fileName);
    }
}

void MainWindow::onUrlOpen()
{
    QInputDialog dialog(this);
    dialog.setWindowTitle(tr("Open URL"));
    dialog.setLabelText(tr("Url to open:"));
    if (dialog.exec() == QDialog::Accepted)
    {
        QString fileName = dialog.textValue();
        if (!fileName.isEmpty())
        {
            getPlayer()->playFile(fileName);
        }
    }
}
