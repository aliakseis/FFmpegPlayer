#include "mainwindow.h"
#include "./ui_mainwindow.h"

#include <QFileDialog>

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
