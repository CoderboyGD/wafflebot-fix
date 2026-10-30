#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "AudioHandling.h"
#include "AudioFile.h"
#include "Macro.h"
#include "MacroLoader.h"
#include "ClickFolder.h"
#include <vector>
#include <array>
#include <filesystem>

#include <QMainWindow>
#include <QFileDialog>
#include <QCloseEvent>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

constexpr int CLICK_TYPE_COUNT{4}; // normal, soft, hard, etc
constexpr int CLICK_FOLDER_COUNT{CLICK_TYPE_COUNT * 12}; // the 12 comes from there being 2 folders per click type (click and release) * 6 buttons total for p1 and p2

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_loadMacrosButton_clicked();

    void on_unloadMacrosButton_clicked();

    void on_unloadAllMacrosButton_clicked();

    void on_loadClickpackButton_clicked();

    void on_noiseCheckBox_clicked();

    void on_microClickThresholdTextEdit_textChanged();

    void on_microClickAfterReleaseThresholdTextEdit_textChanged();

    void on_softClickThresholdTextEdit_textChanged();

    void on_softClickAfterReleaseThresholdTextEdit_textChanged();

    void on_hardClickThresholdTextEdit_textChanged();

    void on_hardClickAfterReleaseThresholdTextEdit_textChanged();

    void on_saveConfigButton_clicked();

    void on_loadConfigButton_clicked();

    void on_renderButton_clicked();

private:
    Ui::MainWindow *ui;
    QString dialogOpenPath = QDir::currentPath();
    QString clickpackPath;
    std::vector<Macro> macroVector;
    ClickFiles clickFiles;
    AudioFile noiseFile;

    std::array<std::vector<const char *>, CLICK_FOLDER_COUNT> clickFolderNames {
        {
         {"player1/clicks",                                 "clicks"},
         {"player1/releases",                               "releases"},
         {"player1/softclicks",   "player1/softClicks",     "softclicks",   "softClicks"},
         {"player1/softreleases", "player1/softReleases",   "softreleases", "softReleases"},
         {"player1/hardclicks",   "player1/hardClicks",     "hardclicks",   "hardClicks"},
         {"player1/hardreleases", "player1/hardReleases",   "hardreleases", "hardReleases"},
         {"player1/microclicks",  "player1/microClicks",    "microclicks",  "microClicks"},
         {"player1/microreleases","player1/microReleases",  "microreleases","microReleases"},

         {"left1/clicks"},
         {"left1/releases"},
         {"left1/softclicks"},
         {"left1/softreleases"},
         {"left1/hardclicks"},
         {"left1/hardreleases"},
         {"left1/microclicks"},
         {"left1/microreleases"},

         {"right1/clicks"},
         {"right1/releases"},
         {"right1/softclicks"},
         {"right1/softreleases"},
         {"right1/hardclicks"},
         {"right1/hardreleases"},
         {"right1/microclicks"},
         {"right1/microreleases"},

         {"player2/clicks"},
         {"player2/releases"},
         {"player2/softclicks",   "player2/softClicks"},
         {"player2/softreleases", "player2/softReleases"},
         {"player2/hardclicks",   "player2/hardClicks"},
         {"player2/hardreleases", "player2/hardReleases"},
         {"player2/microclicks",  "player2/microClicks"},
         {"player2/microreleases","player2/microReleases"},

         {"left2/clicks"},
         {"left2/releases"},
         {"left2/softclicks"},
         {"left2/softreleases"},
         {"left2/hardclicks"},
         {"left2/hardreleases"},
         {"left2/microclicks"},
         {"left2/microreleases"},

         {"right2/clicks"},
         {"right2/releases"},
         {"right2/softclicks"},
         {"right2/softreleases"},
         {"right2/hardclicks"},
         {"right2/hardreleases"},
         {"right2/microclicks"},
         {"right2/microreleases"}
        }
    };




    void loadClickpack(const QString& folderPath);
    QString settingsFilePath() const;
    void checkFields();
    void loadClickpackFromConfig(const QString& absoluteFilePath);
    void loadDefaultSettings();

protected:
    void closeEvent(QCloseEvent *event) override;

};
#endif // MAINWINDOW_H
