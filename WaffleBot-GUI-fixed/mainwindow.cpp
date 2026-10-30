#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QXmlStreamWriter>
#include <QCoreApplication>
#include <QDebug>
#include <QDomDocument>
#include <QMessageBox>
#include <QWidget>
#include <QApplication>
#include <QDir>
#include <filesystem>
#include <set>
#include <limits>
#include "ClickFolder.h"
#include <algorithm>

namespace fs = std::filesystem;


// Empty threshold boxes use the default shown as grey placeholder text, so the
// Render button doesn't stay disabled until every box has been typed into.
template <typename Edit>
static double readThreshold(Edit* edit, bool* ok) {
    QString text = edit->toPlainText().trimmed();
    if (text.isEmpty()) {
        text = edit->placeholderText();
    }
    return text.toDouble(ok);
}

static fs::path toPath(const QString& s) {
    QByteArray utf8 = s.toUtf8();
    return fs::path(std::u8string(utf8.begin(), utf8.end()));
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setFixedSize(QSize(712, 474));
    loadDefaultSettings();

    // set hint text for the click type threshold text fields
    ui->softClickThresholdTextEdit->setPlaceholderText(QString::number(0.15));
    ui->softClickAfterReleaseThresholdTextEdit->setPlaceholderText(QString::number(0.1));
    ui->hardClickThresholdTextEdit->setPlaceholderText(QString::number(2.0));
    ui->hardClickAfterReleaseThresholdTextEdit->setPlaceholderText(QString::number(1.5));
    ui->microClickThresholdTextEdit->setPlaceholderText(QString::number(0.05));
    ui->microClickAfterReleaseThresholdTextEdit->setPlaceholderText(QString::number(0.03));

    checkFields();
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_loadMacrosButton_clicked()
{
    QStringList fileNames = QFileDialog::getOpenFileNames(this,
                                                          tr("Select Files"),
                                                          dialogOpenPath,
                                                          tr(supportedMacroFilter()));
    if (fileNames.isEmpty()) {
        return;
    }

    // Attempt to parse macros, remembering why any of them failed
    QStringList failures;
    for (const QString& filePath : fileNames) {
        QFileInfo fileInfo{filePath};
        dialogOpenPath = fileInfo.absolutePath();
        Macro macro{filePath.toStdString()};

        if (macro.isLoaded()) {
            QString label = fileInfo.fileName();
            if (!macro.getFormat().empty()) {
                label += "  [" + QString::fromStdString(macro.getFormat()) + "]";
            }
            ui->macrosListWidget->addItem(label);
            macroVector.push_back(std::move(macro));
        } else {
            failures << fileInfo.fileName() + ": " + QString::fromStdString(macro.getError());
        }
    }
    checkFields();

    if (!failures.isEmpty()) {
        QMessageBox::warning(this, "Some macros couldn't be loaded", failures.join("\n\n"));
    }
}


void MainWindow::on_unloadMacrosButton_clicked()
{
    for (auto item : ui->macrosListWidget->selectedItems())
    {
        int row = ui->macrosListWidget->row(item);

        // Remove corresponding index for the actual macro data
        macroVector.erase(macroVector.begin() + row); // erase the matching macro

        // Then remove from list widget
        delete ui->macrosListWidget->takeItem(row);
    }
    checkFields();
}


void MainWindow::on_unloadAllMacrosButton_clicked()
{
    // Clear
    macroVector.clear();
    ui->macrosListWidget->clear();
    checkFields();
}

void MainWindow::loadClickpack(const QString& folderPath)
{
    // empty previously loaded sounds, if any
    clickFiles.fill({});
    noiseFile = AudioFile{};

    fs::path base = toPath(folderPath);
    int skipped = 0;
    size_t loadedSounds = 0;

    for (std::size_t i{0}; i < clickFolderNames.size(); i++) {
        std::vector<fs::path> seen;
        for (const char* relative : clickFolderNames[i]) {
            fs::path candidate = base / relative;
            std::error_code ec;
            if (!fs::is_directory(candidate, ec)) {
                continue;
            }
            // case-insensitive file systems (Windows) would otherwise load softclicks/softClicks twice
            bool duplicate = std::any_of(seen.begin(), seen.end(), [&](const fs::path& s) {
                std::error_code ec2;
                return fs::equivalent(s, candidate, ec2);
            });
            if (duplicate) {
                continue;
            }
            seen.push_back(candidate);

            for (auto& file : loadAudioFolder(candidate, &skipped)) {
                clickFiles[i].emplace_back(std::move(file));
                ++loadedSounds;
            }
        }
    }

    // noise.wav or whitenoise.wav in root, player1 or player2 folder
    noiseFile = loadNoiseFile(base);
    if (noiseFile.empty()) {
        noiseFile = loadNoiseFile(base / "player1");
    }
    if (noiseFile.empty()) {
        noiseFile = loadNoiseFile(base / "player2");
    }

    // Determine if clickpack loaded correctly
    QString loadedClickpackText;
    if (loadedSounds > 0) {
        QFileInfo folderInfo{folderPath};
        loadedClickpackText = folderInfo.fileName();
        if (loadedClickpackText.isEmpty()) {
            loadedClickpackText = folderPath; // e.g. a drive root
        }
        clickpackPath = folderInfo.absoluteFilePath();
    }

    ui->clickpackNameLabel->setText(loadedClickpackText);
    checkFields();

    if (loadedSounds == 0 && !folderPath.isEmpty()) {
        QMessageBox::warning(this, "No sounds found",
            "That folder doesn't contain any readable sounds.\n\n"
            "A clickpack needs folders like \"clicks\" and \"releases\" (or \"player1/clicks\", "
            "\"player1/releases\") containing .wav files." +
            (skipped > 0 ? QString("\n\n%1 file(s) were found but couldn't be decoded.").arg(skipped) : QString()));
    }
}

void MainWindow::on_loadClickpackButton_clicked()
{
    // Get clickpack folder
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("Select Folder"), dialogOpenPath);
    if (folderPath.isEmpty()) {
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    loadClickpack(folderPath);
    QApplication::restoreOverrideCursor();
    dialogOpenPath = folderPath;
}

void MainWindow::checkFields() {
    bool softclickLoaded = false;
    bool hardclickLoaded = false;
    bool microclickLoaded = false;

    ui->unloadMacrosButton->setEnabled(ui->macrosListWidget->count() > 0);
    ui->unloadAllMacrosButton->setEnabled(ui->macrosListWidget->count() > 0);

    // Determine if soft clicks/hard clicks are loaded
    std::vector<int> softclickShitVector {
        PLAYER_1_SOFTCLICKS,
        PLAYER_1_SOFTRELEASES,
        PLAYER_1_LEFTSOFTCLICKS,
        PLAYER_1_LEFTSOFTRELEASES,
        PLAYER_1_RIGHTSOFTCLICKS,
        PLAYER_1_RIGHTSOFTRELEASES,

        PLAYER_2_SOFTCLICKS,
        PLAYER_2_SOFTRELEASES,
        PLAYER_2_LEFTSOFTCLICKS,
        PLAYER_2_LEFTSOFTRELEASES,
        PLAYER_2_RIGHTSOFTCLICKS,
        PLAYER_2_RIGHTSOFTRELEASES,
    };

    std::vector<int> hardclickShitVector {
        PLAYER_1_HARDCLICKS,
        PLAYER_1_HARDRELEASES,
        PLAYER_1_LEFTHARDCLICKS,
        PLAYER_1_LEFTHARDRELEASES,
        PLAYER_1_RIGHTHARDCLICKS,
        PLAYER_1_RIGHTHARDRELEASES,

        PLAYER_2_HARDCLICKS,
        PLAYER_2_HARDRELEASES,
        PLAYER_2_LEFTHARDCLICKS,
        PLAYER_2_LEFTHARDRELEASES,
        PLAYER_2_RIGHTHARDCLICKS,
        PLAYER_2_RIGHTHARDRELEASES,
    };

    std::vector<int> microclickShitVector {
        PLAYER_1_MICROCLICKS,
        PLAYER_1_MICRORELEASES,
        PLAYER_1_LEFTMICROCLICKS,
        PLAYER_1_LEFTMICRORELEASES,
        PLAYER_1_RIGHTMICROCLICKS,
        PLAYER_1_RIGHTMICRORELEASES,

        PLAYER_2_MICROCLICKS,
        PLAYER_2_MICRORELEASES,
        PLAYER_2_LEFTMICROCLICKS,
        PLAYER_2_LEFTMICRORELEASES,
        PLAYER_2_RIGHTMICROCLICKS,
        PLAYER_2_RIGHTMICRORELEASES,
    };

    for (int i : softclickShitVector) {
        if (!clickFiles[i].empty()) {
            softclickLoaded = true;
            break;
        }
    }

    for (int i : hardclickShitVector) {
        if (!clickFiles[i].empty()) {
            hardclickLoaded = true;
            break;
        }
    }

    for (int i : microclickShitVector) {
        if (!clickFiles[i].empty()) {
            microclickLoaded = true;
            break;
        }
    }

    ui->noiseCheckBox->setCheckable(!noiseFile.empty());
    ui->softClickThresholdTextEdit->setEnabled(softclickLoaded);
    ui->hardClickThresholdTextEdit->setEnabled(hardclickLoaded);
    ui->microClickThresholdTextEdit->setEnabled(microclickLoaded);
    ui->softClickAfterReleaseThresholdTextEdit->setEnabled(softclickLoaded);
    ui->hardClickAfterReleaseThresholdTextEdit->setEnabled(hardclickLoaded);
    ui->microClickAfterReleaseThresholdTextEdit->setEnabled(microclickLoaded);

    // Determine if render button is clickable
    bool allTextEditsValid;
    bool softClickThresholdValid = true;
    bool softClickAfterReleaseThresholdValid = true;
    bool hardClickThresholdValid = true;
    bool hardClickAfterReleaseThresholdValid = true;
    bool microClickThresholdValid = true;
    bool microClickAfterReleaseThresholdValid = true;

    if (ui->softClickThresholdTextEdit->isEnabled()) {
        readThreshold(ui->softClickThresholdTextEdit, &softClickThresholdValid);
    }

    if (ui->softClickAfterReleaseThresholdTextEdit->isEnabled()) {
        readThreshold(ui->softClickAfterReleaseThresholdTextEdit, &softClickAfterReleaseThresholdValid);
    }

    if (ui->hardClickThresholdTextEdit->isEnabled()) {
        readThreshold(ui->hardClickThresholdTextEdit, &hardClickThresholdValid);
    }

    if (ui->hardClickAfterReleaseThresholdTextEdit->isEnabled()) {
        readThreshold(ui->hardClickAfterReleaseThresholdTextEdit, &hardClickAfterReleaseThresholdValid);
    }

    if (ui->microClickThresholdTextEdit->isEnabled()) {
        readThreshold(ui->microClickThresholdTextEdit, &microClickThresholdValid);
    }

    if (ui->microClickAfterReleaseThresholdTextEdit->isEnabled()) {
        readThreshold(ui->microClickAfterReleaseThresholdTextEdit, &microClickAfterReleaseThresholdValid);
    }

    allTextEditsValid = (softClickThresholdValid && softClickAfterReleaseThresholdValid && hardClickThresholdValid && hardClickAfterReleaseThresholdValid && microClickThresholdValid && microClickAfterReleaseThresholdValid);

    ui->saveConfigButton->setEnabled(allTextEditsValid && !ui->clickpackNameLabel->text().isEmpty());
    ui->renderButton->setEnabled(!ui->clickpackNameLabel->text().isEmpty() && ui->unloadAllMacrosButton->isEnabled() && allTextEditsValid);

}


void MainWindow::on_noiseCheckBox_clicked()
{
    checkFields();
}


void MainWindow::on_microClickThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::on_microClickAfterReleaseThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::on_softClickThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::on_softClickAfterReleaseThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::on_hardClickThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::on_hardClickAfterReleaseThresholdTextEdit_textChanged()
{
    checkFields();
}


void MainWindow::loadClickpackFromConfig(const QString& absoluteFilePath) {
    if (absoluteFilePath.isEmpty() || !QDir(absoluteFilePath).exists()) {
        checkFields();
        return;
    }
    loadClickpack(absoluteFilePath);
}

void MainWindow::on_saveConfigButton_clicked()
{
    QString configFileName = QFileDialog::getSaveFileName(this, tr("Save File"), dialogOpenPath, tr("XML files (*.xml)"));
    if (!configFileName.isEmpty() && !configFileName.endsWith(".xml")) {
        configFileName.append(".xml");
    }

    QFile file(configFileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return;
    }

    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);  // Makes output human-readable

    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement("Configuration");

    xmlWriter.writeTextElement("clickpackPath", clickpackPath);
    xmlWriter.writeTextElement("microclickThreshold", ui->microClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("microclickAfterReleaseThreshold", ui->microClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("softclickThreshold", ui->softClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("softclickAfterReleaseThreshold", ui->softClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("hardclickThreshold", ui->hardClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("hardclickAfterReleaseThreshold", ui->hardClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("noiseEnabled", (ui->noiseCheckBox->isChecked()) ? "true" : "false");

    xmlWriter.writeEndElement(); // Configuration
    xmlWriter.writeEndDocument();

    file.close();
    QFileInfo fileInfo{configFileName};
    dialogOpenPath = fileInfo.filePath();
}


void MainWindow::on_loadConfigButton_clicked()
{
    QString configFileName = QFileDialog::getOpenFileName(this, tr("Open File"), dialogOpenPath, tr("XML files (*.xml)"));

    QFile file(configFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file.";
        checkFields();
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        qDebug() << "Failed to parse XML.";
        checkFields();
        return;
    }
    file.close();

    // Access elements
    QDomElement root = doc.documentElement();

    clickpackPath = root.firstChildElement("clickpackPath").text();
    loadClickpackFromConfig(clickpackPath);
    ui->microClickThresholdTextEdit->setText(root.firstChildElement("microclickThreshold").text());
    ui->microClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("microclickAfterReleaseThreshold").text());
    ui->softClickThresholdTextEdit->setText(root.firstChildElement("softclickThreshold").text());
    ui->softClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("softclickAfterReleaseThreshold").text());
    ui->hardClickThresholdTextEdit->setText(root.firstChildElement("hardclickThreshold").text());
    ui->hardClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("hardclickAfterReleaseThreshold").text());
    ui->noiseCheckBox->setChecked(root.firstChildElement("noiseEnabled").text() == "true");

    QFileInfo fileInfo{configFileName};
    dialogOpenPath = fileInfo.filePath();

    checkFields();
}

void MainWindow::on_renderButton_clicked()
{
    // Pick where the generated .wav files go
    QString folderPath = QFileDialog::getExistingDirectory(this, tr("Save to..."), dialogOpenPath);
    if (folderPath.isEmpty()) {
        return;
    }
    fs::path outFolder = toPath(folderPath);

    double microClickTime = (ui->microClickThresholdTextEdit->isEnabled()) ? readThreshold(ui->microClickThresholdTextEdit, nullptr) : 0.0;
    double microClickAfterReleaseTime = (ui->microClickAfterReleaseThresholdTextEdit->isEnabled()) ? readThreshold(ui->microClickAfterReleaseThresholdTextEdit, nullptr) : 0.0;
    double softClickTime = (ui->softClickThresholdTextEdit->isEnabled()) ? readThreshold(ui->softClickThresholdTextEdit, nullptr) : 0.0;
    double softClickAfterReleaseTime = (ui->softClickAfterReleaseThresholdTextEdit->isEnabled()) ? readThreshold(ui->softClickAfterReleaseThresholdTextEdit, nullptr) : 0.0;
    double hardClickTime = (ui->hardClickThresholdTextEdit->isEnabled()) ? readThreshold(ui->hardClickThresholdTextEdit, nullptr) : std::numeric_limits<double>::max();
    double hardClickAfterReleaseTime = (ui->hardClickAfterReleaseThresholdTextEdit->isEnabled()) ? readThreshold(ui->hardClickAfterReleaseThresholdTextEdit, nullptr) : std::numeric_limits<double>::max();

    const AudioFile* noise = (ui->noiseCheckBox->isChecked() && !noiseFile.empty()) ? &noiseFile : nullptr;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    int generatedFilesCount = 0;
    QStringList problems;
    std::set<std::string> usedNames;

    for (Macro& macro : macroVector) {
        QApplication::processEvents();
        macro.determineClickTypes(microClickTime, microClickAfterReleaseTime, softClickTime, softClickAfterReleaseTime, hardClickTime, hardClickAfterReleaseTime);

        // never overwrite another macro from the same batch that has the same name
        std::string name = macro.getName();
        std::string unique = name;
        for (int n = 2; usedNames.count(unique); ++n) {
            unique = name + "_" + std::to_string(n);
        }
        usedNames.insert(unique);

        fs::path outPath = outFolder / fs::path(std::u8string(unique.begin(), unique.end()) + u8".wav");
        RenderResult result = renderMacroToWav(macro, clickFiles, noise, outPath);

        if (result.ok) {
            generatedFilesCount++;
            if (!result.message.empty()) {
                problems << QString::fromStdString(name) + ": " + QString::fromStdString(result.message);
            }
        } else {
            problems << QString::fromStdString(name) + ": FAILED - " + QString::fromStdString(result.message);
        }
    }

    QApplication::restoreOverrideCursor();

    QString generatedText{"Generated "};
    generatedText.append(QString::number(generatedFilesCount));
    generatedText.append(" of ");
    generatedText.append(QString::number(macroVector.size()));
    generatedText.append(" files!");

    if (problems.isEmpty()) {
        QMessageBox::information(this, "Info", generatedText);
    } else {
        QMessageBox::warning(this, "Info", generatedText + "\n\n" + problems.join("\n"));
    }

    dialogOpenPath = folderPath;
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // Save current settings to a default settings xml
    QString configFileName {"settings.xml"};

    QFile file(configFileName);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file for writing:" << file.errorString();
        return;
    }

    QXmlStreamWriter xmlWriter(&file);
    xmlWriter.setAutoFormatting(true);  // Makes output human-readable

    xmlWriter.writeStartDocument();

    xmlWriter.writeStartElement("Configuration");

    xmlWriter.writeTextElement("clickpackPath", clickpackPath);
    xmlWriter.writeTextElement("microclickThreshold", ui->microClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("microclickAfterReleaseThreshold", ui->microClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("softclickThreshold", ui->softClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("softclickAfterReleaseThreshold", ui->softClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("hardclickThreshold", ui->hardClickThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("hardclickAfterReleaseThreshold", ui->hardClickAfterReleaseThresholdTextEdit->document()->toPlainText());
    xmlWriter.writeTextElement("noiseEnabled", (ui->noiseCheckBox->isChecked()) ? "true" : "false");

    xmlWriter.writeEndElement(); // Configuration
    xmlWriter.writeEndDocument();

    file.close();
}

void MainWindow::loadDefaultSettings() {
    QString configFileName {settingsFilePath()};

    QFile file(configFileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Failed to open file.";
        checkFields();
        return;
    }

    QDomDocument doc;
    if (!doc.setContent(&file)) {
        qDebug() << "Failed to parse XML.";
        checkFields();
        return;
    }
    file.close();

    // Access elements
    QDomElement root = doc.documentElement();

    clickpackPath = root.firstChildElement("clickpackPath").text();
    loadClickpackFromConfig(clickpackPath);
    ui->microClickThresholdTextEdit->setText(root.firstChildElement("microclickThreshold").text());
    ui->microClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("microclickAfterReleaseThreshold").text());
    ui->softClickThresholdTextEdit->setText(root.firstChildElement("softclickThreshold").text());
    ui->softClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("softclickAfterReleaseThreshold").text());
    ui->hardClickThresholdTextEdit->setText(root.firstChildElement("hardclickThreshold").text());
    ui->hardClickAfterReleaseThresholdTextEdit->setText(root.firstChildElement("hardclickAfterReleaseThreshold").text());
    ui->noiseCheckBox->setChecked(root.firstChildElement("noiseEnabled").text() == "true");

    checkFields();
}


QString MainWindow::settingsFilePath() const {
    return QDir(QCoreApplication::applicationDirPath()).filePath("settings.xml");
}
