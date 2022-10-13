#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QMessageBox>
#include <QString>
#include <QThread>
#include <QStyleFactory>

#define PIPE_NAME "\\\\.\\pipe\\OmegaBotPipe"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , pipe(PIPE_NAME)
    , ui(new Ui::MainWindow)
{
    LoadTheme("#333343", "#191921", "#15151B", "#2A82DA");

    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    ui->setupUi(this);

    titleBar = new TitleBar(this);
    ui->titleBarLayout->addWidget(titleBar);
    ui->titleBarLayout->addWidget(ui->tabWidget);
    ui->titleBarLayout->addWidget(ui->injectButton);

    errorParent = this;

    Attach();
}

MainWindow::~MainWindow()
{
    errorParent = nullptr;
    if (injected) Uninject();
    delete ui;
}

void MainWindow::LoadTheme(const QColor& grey, const QColor& darkGrey, const QColor& black, const QColor& blue)
{
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, darkGrey);
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, black);
    darkPalette.setColor(QPalette::AlternateBase, darkGrey);
    darkPalette.setColor(QPalette::ToolTipBase, blue);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, darkGrey);
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Link, blue);
    darkPalette.setColor(QPalette::Highlight, blue);
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);

    darkPalette.setColor(QPalette::Active, QPalette::Button, grey.darker());
    darkPalette.setColor(QPalette::Disabled, QPalette::ButtonText, black);
    darkPalette.setColor(QPalette::Disabled, QPalette::WindowText, black);
    darkPalette.setColor(QPalette::Disabled, QPalette::Text, black);
    darkPalette.setColor(QPalette::Disabled, QPalette::Light, grey.darker());

    qApp->setPalette(darkPalette);

    qApp->setStyleSheet(R"(
QToolTip {
    color: #ffffff;
    background-color: #2a82da;
    border: 1px solid white;
}

QLabel#icon {
    max-width: 17px;
    min-height: 17px;
    margin-top: 1px;
    border-image: url(:/resources/logo.jpg) 0 0 0 0 stretch stretch;
    border-radius: 5px;
}

QLabel#title {
    font-size: 12px;
    font: bold;
    padding: 0px;
    margin: 0;
    position: absolute;
    top: 0px;
    left: 20px;
}

QPushButton#minimiseButton, QPushButton#closeButton {
    border: none;
    border-radius: none;
    padding: 0;
    margin-top: 1px;
    max-width: 17px;
    min-height: 17px;
    max-height: 17px;
}

QPushButton#minimiseButton {
    /*background-image: url(:/resources/minimise.png);
    background-position: center;
    background-repeat: non-repeat;*/
    //border-image: url(:/resources/minimise.png) 0 0 0 0 stretch stretch;
}

QPushButton#closeButton {
    //border-image: url(:/resources/close.png) 0 0 0 0 stretch stretch;
}

QPushButton#refreshButton {
    padding: 5px 0;
    margin: 3px;
}
)");
}

void MainWindow::Attach() {
    messageQueue.clear();
    memoryHandler = MemoryHandler("Geometry Dash");
    if (!memoryHandler.IsInitialised()) {
        Error("Failed to attach to Geometry Dash");
    } else {
        injected = memoryHandler.GetModuleBase(L"OmegaBot.dll") != 0;
        if (!injected) {
            Inject();
            if (injected) {
                pipe.Create();
                errorForwarder = new ErrorForwarder;
                QThread* thread = QThread::create([&] {
                    while (injected && pipe.Exists()) {
                        QString error;
                        if (SendMessages(&error)) {
                            errorForwarder->ForwardError(error);
                        }
                        QThread::msleep(5);
                    }
                });

                connect(errorForwarder, &ErrorForwarder::Error, this, &MainWindow::Error);

                thread->start();
            }
        } else {
            Error("OmegaBot.dll already injected");
        }
    }
    ui->injectButton->setText(injected ? "Uninject" : "Inject");
    EnableAll(injected);
}

void MainWindow::EnableAll(bool enable) {
    ui->tabWidget->setEnabled(enable);
}

void MainWindow::Inject() {
    QString dllPath = QDir::currentPath().replace("/", "\\") + "\\OmegaBot.dll";
    injected = memoryHandler.Inject(dllPath.toStdWString().c_str());
    if (!injected)
        Error("Failed to inject dll, press Refresh to retry");
}

void MainWindow::Uninject() {
    EnableAll(false);
    injected = false;
    pipe.SendMSG(Pipe::Exit);
    pipe.Disconnect();
    ui->injectButton->setText("Inject");
}

bool MainWindow::SendMessages(QString* error) {
    if (injected && pipe.Exists()) {
        if (messageQueue.isEmpty()) {
            pipe.SendMSG(Pipe::Ping);
            QString msg = pipe.ReceiveMSG();
            while (msg.at(0).cell() != Pipe::Ping)
            {
                ParseMessage(msg);
                msg = pipe.ReceiveMSG();
            }
        }
        else {
            while (!messageQueue.isEmpty()) {
                if (!messageQueue.dequeue()(error)) {
                    return true;
                }
            }
        }
    }
    return false;
}

template<typename T>
void MainWindow::QueueMessage(T&& msg) {
    CHECK_INJECTED;
    messageQueue.enqueue(std::move(msg));
}

void MainWindow::ParseMessage(const QString& msg) {
    switch (msg.at(0).cell()) {
    case Pipe::Ping:
        break;
    case Pipe::Error: {
        QString newmsg = msg;
        if (errorForwarder) errorForwarder->ForwardError(newmsg.remove(0, 1));
    } break;
    case Pipe::ChangeFPS: {
        wchar_t* newmsg = (wchar_t*)malloc(sizeof(wchar_t) * (msg.length() + 1));
        msg.toWCharArray(newmsg);
        float fps = 0.f;
        memcpy_s(&fps, sizeof(float), newmsg + 1, sizeof(float));
        ui->fpsSpinBox->setValue(fps);
        free(newmsg);
        pipe.SendMSG(Pipe::Received);
    } break;
    default:
        if (errorForwarder) errorForwarder->ForwardError("Uknkown pipe message");
        break;
    }
}

void MainWindow::Error(const QString& errorMessage) {
    if (errorParent) {
        QMessageBox::warning(errorParent, "Error", errorMessage);
    }
}

void MainWindow::on_injectButton_clicked()
{
    if (!injected) {
        Attach();
    } else {
        Uninject();
    }
}

void MainWindow::on_recordButton_clicked()
{
    CHECK_INJECTED;

    if (playing) {
        QueueMessage([=] (QString* error) {
            if (pipe.SendMSG(Pipe::Append, true, error)) {
                recording = true;
                ui->recordButton->setText(tr("Appending..."));
                return true;
            } else {
                return false;
            }
        });
        return;
    }

    QString replayName = ui->replayNameLineEdit->text();
    if (replayName.isEmpty() || replayName == "")
    {
        Error("Please enter a replay name");
        return;
    }

    if (!recording) {
        if (!QDir("replays").exists()) QDir().mkdir("replays");
        QueueMessage([=] (QString* error) {
            if (pipe.SendMSG(Pipe::StartRecording, true, error)) {
                recording = true;
                ui->recordButton->setText(tr("Recording..."));
                return true;
            } else {
                return false;
            }
        });
    } else {
        if (!QDir("replays").exists()) QDir().mkdir("replays");
        QString replayName = ui->replayNameLineEdit->text().replace("/", "\\");
        QString path = QDir::currentPath().replace("/", "\\") + "\\replays\\" + replayName + ".replay";

        QueueMessage([=] (QString* error) {
            if (pipe.SendMSG(Pipe::StopRecording, true, error)) {
                recording = false;
                ui->recordButton->setText(tr("Record"));
                QueueMessage([=] (QString* error) {
                    if (pipe.SendMSG(Pipe::SaveReplay, path, true, error)) {
                        // TODO: LoadReplays();
                        // TODO: ui->play_replayNameCombo->setCurrentText(replayName);
                        return true;
                    } else {
                        return false;
                    }
                });
                return true;
            } else {
                return false;
            }
        });
    }
}

void MainWindow::on_playButton_clicked()
{
    CHECK_INJECTED;

    if (recording) {
        Error("Already recording!");
        return;
    }

    QString replayName = ui->replayNameLineEdit->text();
    if (replayName.isEmpty() || replayName == "")
    {
        Error("Please enter a replay name");
        return;
    }

    if (playing) {
        QueueMessage([=] (QString* error) {
            if (pipe.SendMSG(Pipe::StopPlayback, true, error)) {
                playing = false;
                ui->playButton->setText(tr("Play"));
                ui->recordButton->setText(tr("Record"));
                return true;
            } else {
                return false;
            }
        });
    } else {
        QString replayName = ui->replayNameLineEdit->text().replace("/", "\\");
        QString path = QDir::currentPath().replace("/", "\\") + "\\replays\\" + replayName + ".replay";

        QueueMessage([=] (QString* error) {
            if (pipe.SendMSG(Pipe::LoadReplay, path, true, error)) {
                QueueMessage([=] (QString* error) {
                    if (pipe.SendMSG(Pipe::StartPlayback, true, error)) {
                        playing = true;
                        ui->recordButton->setText(tr("Append"));
                        ui->playButton->setText(tr("Playing..."));
                        return true;
                    } else {
                        return false;
                    }
                });
                return true;
            } else {
                return false;
            }
        });
    }
}

void MainWindow::on_replayTypeComboBox_currentIndexChanged(int index)
{
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::SetReplayType, QString("%1").arg((char)(index + 1)), true, error);
    });
}

void MainWindow::on_frameAdvanceCheckBox_stateChanged(int state)
{
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::FrameAdvance, QString("%1").arg((char)(state > 0 ? 1 : 0)), true, error);
    });
}

void MainWindow::on_setFPSButton_clicked()
{
    float fps = ui->fpsSpinBox->value();
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::ChangeFPS, QString::fromWCharArray(ReCa<const wchar_t*>(&fps), sizeof(float) / sizeof(wchar_t)), true, error);
    });
}

void MainWindow::on_accuracyFixCheckBox_stateChanged(int state)
{
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::AccuracyFix, QString("%1").arg((char)(state > 0 ? 1 : 0)), true, error);
    });
}

void MainWindow::on_practiceFixCheckBox_stateChanged(int state)
{
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::PracticeFix, QString("%1").arg((char)(state > 0 ? 1 : 0)), true, error);
    });
}

void MainWindow::on_setRespawnTimeButton_clicked()
{
    float respawnTime = ui->respawnTimeSpinBox->value();
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::RespawnTime, QString::fromWCharArray(ReCa<const wchar_t*>(&respawnTime), sizeof(float) / sizeof(wchar_t)), true, error);
    });
    if (ui->speedLinkButton->isChecked()) {
        ui->speedhackSpinBox->setValue(ui->respawnTimeSpinBox->value());
        float speed = ui->speedhackSpinBox->value();
        QueueMessage([=] (QString* error) {
            return pipe.SendMSG(Pipe::Speedhack, QString::fromWCharArray(ReCa<const wchar_t*>(&speed), sizeof(float) / sizeof(wchar_t)), true, error);
        });
    }
}

void MainWindow::on_setSpeedButton_clicked()
{
    float speed = ui->speedhackSpinBox->value();
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::Speedhack, QString::fromWCharArray(ReCa<const wchar_t*>(&speed), sizeof(float) / sizeof(wchar_t)), true, error);
    });
    if (ui->speedLinkButton->isChecked()) {
        ui->respawnTimeSpinBox->setValue(ui->speedhackSpinBox->value());
        float respawnTime = ui->respawnTimeSpinBox->value();
        QueueMessage([=] (QString* error) {
            return pipe.SendMSG(Pipe::RespawnTime, QString::fromWCharArray(ReCa<const wchar_t*>(&respawnTime), sizeof(float) / sizeof(wchar_t)), true, error);
        });
    }
}

#define TOGGLE_HACK(hack_name) QueueMessage([=] (QString* error) { \
    return pipe.SendMSG(state ? Pipe::ApplyHack : Pipe::RestoreHack, QString("%1").arg((char)HackName::hack_name), true, error); \
});

void MainWindow::on_antiCheatBypassCheckBox_stateChanged(int state)
{
    TOGGLE_HACK(AntiCheatBypass);
}

void MainWindow::on_practiceMusicFixCheckBox_stateChanged(int state)
{
    TOGGLE_HACK(PracticeMusicFix);
}

void MainWindow::on_ignoreESCCheckBox_stateChanged(int state)
{
    TOGGLE_HACK(IgnoreEscape);
}

void MainWindow::on_noRespawnFlashCheckBox_stateChanged(int state)
{
    TOGGLE_HACK(NoRespawnFlash);
}

void MainWindow::on_disableDeathEffectCheckBox_stateChanged(int state)
{
    TOGGLE_HACK(DisableDeathEffect);
}

void MainWindow::on_speedLinkButton_toggled(bool checked)
{
    ui->speedLinkButton->setIcon(checked ? QIcon(":/resources/link.png") : QIcon(":/resources/broken-link.png"));
}

void MainWindow::on_noClipSpinBox_currentIndexChanged(int index)
{
    QueueMessage([=] (QString* error) {
        return pipe.SendMSG(Pipe::SetNoClip, QString("%1").arg((char)(index + 1)), true, error);
    });
}
