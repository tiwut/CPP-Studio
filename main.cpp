#include <QApplication>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QTreeView>
#include <QFileSystemModel>
#include <QSplitter>
#include <QVBoxLayout>
#include <QToolBar>
#include <QAction>
#include <QFileDialog>
#include <QTextStream>
#include <QProcess>
#include <QSyntaxHighlighter>
#include <QRegularExpression>
#include <QComboBox>
#include <QLabel>
#include <QHeaderView>
#include <QDirIterator>
#include <QCoreApplication>
#include <QScrollBar>
#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>
#include <QKeySequence>

class CppHighlighter : public QSyntaxHighlighter {
public:
    CppHighlighter(QTextDocument *parent = nullptr) : QSyntaxHighlighter(parent) {
        HighlightingRule rule;
        keywordFormat.setForeground(QColor("#569cd6"));
        keywordFormat.setFontWeight(QFont::Bold);
        QStringList keywordPatterns = {
            "\\bchar\\b", "\\bclass\\b", "\\bconst\\b", "\\bdouble\\b", "\\benum\\b", "\\bexplicit\\b",
            "\\bfriend\\b", "\\binline\\b", "\\bint\\b", "\\blong\\b", "\\bnamespace\\b", "\\boperator\\b",
            "\\bprivate\\b", "\\bprotected\\b", "\\bpublic\\b", "\\bshort\\b", "\\bsignals\\b", "\\bsigned\\b",
            "\\bslots\\b", "\\bstatic\\b", "\\bstruct\\b", "\\btemplate\\b", "\\btypedef\\b", "\\btypename\\b",
            "\\bunion\\b", "\\bunsigned\\b", "\\bvirtual\\b", "\\bvoid\\b", "\\bvolatile\\b", "\\bbool\\b",
            "\\bauto\\b", "\\breturn\\b", "\\bif\\b", "\\belse\\b", "\\bfor\\b", "\\bwhile\\b", "\\busing\\b"
        };
        for (const QString &pattern : keywordPatterns) {
            rule.pattern = QRegularExpression(pattern);
            rule.format = keywordFormat;
            highlightingRules.append(rule);
        }

        classFormat.setForeground(QColor("#4ec9b0"));
        rule.pattern = QRegularExpression("\\b[A-Z][A-Za-z0-9_]+\\b");
        rule.format = classFormat;
        highlightingRules.append(rule);

        singleLineCommentFormat.setForeground(QColor("#6A9955"));
        rule.pattern = QRegularExpression("//[^\n]*");
        rule.format = singleLineCommentFormat;
        highlightingRules.append(rule);

        quotationFormat.setForeground(QColor("#ce9178"));
        rule.pattern = QRegularExpression("\".*\"");
        rule.format = quotationFormat;
        highlightingRules.append(rule);

        includeFormat.setForeground(QColor("#C586C0"));
        rule.pattern = QRegularExpression("#include\\s*[<\"].*[>\"]");
        rule.format = includeFormat;
        highlightingRules.append(rule);
    }

protected:
    void highlightBlock(const QString &text) override {
        for (const HighlightingRule &rule : std::as_const(highlightingRules)) {
            QRegularExpressionMatchIterator matchIterator = rule.pattern.globalMatch(text);
            while (matchIterator.hasNext()) {
                QRegularExpressionMatch match = matchIterator.next();
                setFormat(match.capturedStart(), match.capturedLength(), rule.format);
            }
        }
    }

private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    QList<HighlightingRule> highlightingRules;
    QTextCharFormat keywordFormat;
    QTextCharFormat classFormat;
    QTextCharFormat singleLineCommentFormat;
    QTextCharFormat quotationFormat;
    QTextCharFormat includeFormat;
};

class CppStudio : public QMainWindow {
    Q_OBJECT

public:
    CppStudio() {
        setWindowTitle("Cpp Studio");
        resize(1200, 800);

        setupUI();
        setupConnections();
        applyModernTheme();

        currentFile = "";
        lastCompiledExe = "";
        lastCompiledDir = "";
        isModified = false;
        
        clipOp = ClipNone;
        clipPath = "";

        compilerProcess = new QProcess(this);
        appProcess = new QProcess(this);

        connect(compilerProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            consoleOutput->insertPlainText(QString::fromUtf8(compilerProcess->readAllStandardOutput()));
            consoleOutput->verticalScrollBar()->setValue(consoleOutput->verticalScrollBar()->maximum());
        });
        connect(compilerProcess, &QProcess::readyReadStandardError, this, [this]() {
            consoleOutput->insertPlainText(QString::fromUtf8(compilerProcess->readAllStandardError()));
            consoleOutput->verticalScrollBar()->setValue(consoleOutput->verticalScrollBar()->maximum());
        });
        connect(appProcess, &QProcess::readyReadStandardOutput, this, [this]() {
            consoleOutput->insertPlainText(QString::fromUtf8(appProcess->readAllStandardOutput()));
            consoleOutput->verticalScrollBar()->setValue(consoleOutput->verticalScrollBar()->maximum());
        });
        connect(appProcess, &QProcess::readyReadStandardError, this, [this]() {
            consoleOutput->insertPlainText(QString::fromUtf8(appProcess->readAllStandardError()));
            consoleOutput->verticalScrollBar()->setValue(consoleOutput->verticalScrollBar()->maximum());
        });
        connect(appProcess, &QProcess::finished, this, [this](int exitCode) {
            consoleOutput->appendPlainText(QString("\n--- Program finished with exit code %1 ---").arg(exitCode));
            consoleOutput->verticalScrollBar()->setValue(consoleOutput->verticalScrollBar()->maximum());
        });
    }

private:
    QPlainTextEdit *editor;
    QPlainTextEdit *consoleOutput;
    QTreeView *fileTree;
    QFileSystemModel *fileModel;
    
    QProcess *compilerProcess;
    QProcess *appProcess;

    QComboBox *stdCombo;
    QComboBox *modeCombo;
    QLabel *compileStatusLabel;
    QAction *actAutoSave;
    QTimer *autoSaveTimer;

    QString currentFile;
    QString lastCompiledExe;
    QString lastCompiledDir;
    bool isModified;

    enum ClipOperation { ClipNone, ClipCopy, ClipCut };
    ClipOperation clipOp;
    QString clipPath;

    void setupUI() {
        QToolBar *toolbar = addToolBar("Main");
        toolbar->setMovable(false);
        toolbar->setIconSize(QSize(18, 18));
        QAction *actNew = toolbar->addAction("📄 New", this, &CppStudio::newFile);
        actNew->setShortcut(QKeySequence::New);
        actNew->setToolTip("New File (Ctrl+N)");
        QAction *actOpen = toolbar->addAction("📂 Open", this, &CppStudio::openFile);
        actOpen->setShortcut(QKeySequence::Open);
        actOpen->setToolTip("Open File (Ctrl+O)");
        QAction *actFolder = toolbar->addAction("📁 Folder", this, &CppStudio::openFolder);
        actFolder->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
        actFolder->setToolTip("Open Folder (Ctrl+Shift+O)");
        QAction *actSave = toolbar->addAction("💾 Save", this, &CppStudio::saveFile);
        actSave->setShortcut(QKeySequence::Save);
        actSave->setToolTip("Save File (Ctrl+S)");
        toolbar->addSeparator();
        actAutoSave = toolbar->addAction("⏱ Auto-Save");
        actAutoSave->setCheckable(true);
        actAutoSave->setChecked(true);
        actAutoSave->setToolTip("Automatically save after you stop typing");
        toolbar->addSeparator();
        toolbar->addWidget(new QLabel(" Standard: "));
        stdCombo = new QComboBox(this);
        stdCombo->addItems({"c++11", "c++14", "c++17", "c++20", "c++23"});
        stdCombo->setCurrentText("c++17");
        toolbar->addWidget(stdCombo);
        toolbar->addWidget(new QLabel(" Mode: "));
        modeCombo = new QComboBox(this);
        modeCombo->addItems({"Debug (-g)", "Release (-O3)"});
        toolbar->addWidget(modeCombo);
        toolbar->addSeparator();
        QAction *actCompile = toolbar->addAction("🔨 Compile", this, &CppStudio::compileProject);
        actCompile->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)); // Ctrl+Shift+B
        actCompile->setToolTip("Compile / Build Project (Ctrl+Shift+B)");
        QAction *actRun = toolbar->addAction("▶ Run", this, &CppStudio::runProject);
        actRun->setShortcut(QKeySequence(Qt::Key_F5));
        actRun->setToolTip("Run Executable (F5)");
        compileStatusLabel = new QLabel(this);
        toolbar->addWidget(compileStatusLabel);
        setModifiedState(false);
        toolbar->addSeparator();
        toolbar->addAction("🗑 Clear", this, [this](){ consoleOutput->clear(); });
        fileModel = new QFileSystemModel(this);
        fileModel->setRootPath(QDir::currentPath());
        fileTree = new QTreeView(this);
        fileTree->setModel(fileModel);
        fileTree->setRootIndex(fileModel->index(QDir::currentPath()));
        fileTree->setColumnHidden(1, true); 
        fileTree->setColumnHidden(2, true); 
        fileTree->setColumnHidden(3, true); 
        fileTree->setHeaderHidden(true);
        fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
        editor = new QPlainTextEdit(this);
        QFont font("Monospace", 11);
        font.setStyleHint(QFont::Monospace);
        editor->setFont(font);
        editor->setTabStopDistance(4 * QFontMetrics(font).horizontalAdvance(' ')); 
        new CppHighlighter(editor->document());
        consoleOutput = new QPlainTextEdit(this);
        consoleOutput->setReadOnly(true);
        consoleOutput->setFont(font);
        QSplitter *mainSplitter = new QSplitter(Qt::Horizontal, this);
        QSplitter *editorSplitter = new QSplitter(Qt::Vertical, this);
        editorSplitter->addWidget(editor);
        editorSplitter->addWidget(consoleOutput);
        editorSplitter->setSizes({600, 200});
        mainSplitter->addWidget(fileTree);
        mainSplitter->addWidget(editorSplitter);
        mainSplitter->setSizes({250, 950});
        setCentralWidget(mainSplitter);
        autoSaveTimer = new QTimer(this);
        autoSaveTimer->setInterval(1000);
        autoSaveTimer->setSingleShot(true);
    }

    void applyModernTheme() {
        QString styleSheet = R"(
            QMainWindow, QDialog { background-color: #1e1e1e; }
            QToolBar { background-color: #333333; border: none; padding: 4px; spacing: 8px; }
            QToolButton { background-color: transparent; color: #cccccc; border-radius: 4px; padding: 6px; font-weight: bold; }
            QToolButton:hover { background-color: #444444; color: #ffffff; }
            QToolButton:checked { background-color: #04395e; color: #ffffff; border: 1px solid #055084; }
            QLabel { color: #cccccc; }
            QComboBox { background-color: #2d2d2d; color: #cccccc; border: 1px solid #3c3c3c; border-radius: 4px; padding: 4px 8px; }
            QComboBox::drop-down { border: none; }
            QComboBox QAbstractItemView { background-color: #2d2d2d; color: #cccccc; selection-background-color: #04395e; }
            QTreeView { background-color: #252526; color: #cccccc; border: none; }
            QTreeView::item:selected { background-color: #37373d; color: #ffffff; }
            QTreeView::item:hover { background-color: #2a2d2e; }
            QPlainTextEdit { background-color: #1e1e1e; color: #d4d4d4; border: none; }
            QSplitter::handle { background-color: #333333; width: 2px; height: 2px; }
            QScrollBar:vertical { border: none; background: #1e1e1e; width: 10px; }
            QScrollBar::handle:vertical { background: #424242; border-radius: 5px; }
            QScrollBar::handle:vertical:hover { background: #4f4f4f; }
            QMenu { background-color: #252526; color: #cccccc; border: 1px solid #454545; }
            QMenu::item { padding: 4px 20px; }
            QMenu::item:selected { background-color: #04395e; }
            QInputDialog, QMessageBox { background-color: #1e1e1e; color: #d4d4d4; }
            QLineEdit { background-color: #3c3c3c; color: white; border: 1px solid #555; padding: 4px; }
            QPushButton { background-color: #04395e; color: white; border: none; padding: 6px 12px; border-radius: 4px; }
            QPushButton:hover { background-color: #055084; }
        )";
        qApp->setStyleSheet(styleSheet);
    }

    void setupConnections() {
        connect(fileTree, &QTreeView::clicked, this, [this](const QModelIndex &index) {
            if (!fileModel->isDir(index)) {
                loadFile(fileModel->filePath(index));
            }
        });

        connect(editor->document(), &QTextDocument::contentsChanged, this, [this]() {
            if (!isModified && !currentFile.isEmpty()) {
                setModifiedState(true);
            }
            
            if (actAutoSave->isChecked() && !currentFile.isEmpty()) {
                autoSaveTimer->start();
            }
        });

        connect(autoSaveTimer, &QTimer::timeout, this, [this]() {
            if (isModified && !currentFile.isEmpty()) {
                saveFile();
            }
        });

        connect(fileTree, &QTreeView::customContextMenuRequested, this, &CppStudio::showContextMenu);
    }

    void setModifiedState(bool modified) {
        isModified = modified;
        if (modified) {
            compileStatusLabel->setText("  ⚠️ Needs Compile  ");
            compileStatusLabel->setStyleSheet("color: #d7ba7d; font-weight: bold;"); 
        } else {
            compileStatusLabel->setText("  ✅ Up to date  ");
            compileStatusLabel->setStyleSheet("color: #6A9955; font-weight: bold;"); 
        }
    }

    void showContextMenu(const QPoint &pos) {
        QModelIndex index = fileTree->indexAt(pos);
        QString path = fileModel->filePath(index);
        if (!index.isValid()) path = fileModel->rootPath();

        QString parentDir = QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath();

        QMenu menu(this);
        QAction *newFileAct = menu.addAction("📄 New File");
        QAction *newDirAct  = menu.addAction("📁 New Folder");
        menu.addSeparator();
        QAction *renameAct  = menu.addAction("✏️ Rename");
        QAction *deleteAct  = menu.addAction("🗑 Delete");
        menu.addSeparator();
        QAction *cutAct   = menu.addAction("✂️ Cut");
        QAction *copyAct  = menu.addAction("📋 Copy");
        QAction *pasteAct = menu.addAction("📋 Paste");

        if (clipPath.isEmpty()) pasteAct->setEnabled(false);
        if (!index.isValid()) { renameAct->setEnabled(false); deleteAct->setEnabled(false); cutAct->setEnabled(false); copyAct->setEnabled(false); }

        QAction *selectedItem = menu.exec(fileTree->viewport()->mapToGlobal(pos));
        if (!selectedItem) return;

        if (selectedItem == newFileAct) createNewFilePrompt(parentDir);
        else if (selectedItem == newDirAct) {
            bool ok; QString name = QInputDialog::getText(this, "New Folder", "Enter folder name:", QLineEdit::Normal, "", &ok);
            if (ok && !name.isEmpty()) QDir(parentDir).mkdir(name);
        }
        else if (selectedItem == renameAct) {
            bool ok; QString oldName = QFileInfo(path).fileName();
            QString newName = QInputDialog::getText(this, "Rename", "Enter new name:", QLineEdit::Normal, oldName, &ok);
            if (ok && !newName.isEmpty() && newName != oldName) {
                QString newPath = parentDir + "/" + newName;
                QFile::rename(path, newPath);
                if (currentFile == path) {
                    currentFile = newPath;
                    setWindowTitle("Cpp Studio - " + currentFile);
                }
            }
        }
        else if (selectedItem == deleteAct) {
            if (QMessageBox::question(this, "Confirm", "Delete " + QFileInfo(path).fileName() + "?", QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                if (QFileInfo(path).isDir()) QDir(path).removeRecursively(); else QFile(path).remove();
                if (currentFile == path) { editor->clear(); currentFile = ""; setWindowTitle("Cpp Studio"); }
            }
        }
        else if (selectedItem == copyAct) { clipPath = path; clipOp = ClipCopy; }
        else if (selectedItem == cutAct) { clipPath = path; clipOp = ClipCut; }
        else if (selectedItem == pasteAct) {
            QString destPath = parentDir + "/" + QFileInfo(clipPath).fileName();
            if (clipOp == ClipCopy) { if (QFileInfo(clipPath).isDir()) copyDirRecursively(clipPath, destPath); else QFile::copy(clipPath, destPath); }
            else if (clipOp == ClipCut) { QFile::rename(clipPath, destPath); clipPath = ""; clipOp = ClipNone; }
        }
    }

    bool copyDirRecursively(const QString &src, const QString &dst) {
        QDir dir(src); if (!dir.exists()) return false;
        QDir().mkpath(dst);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot)) {
            if (info.isDir()) copyDirRecursively(info.absoluteFilePath(), dst + "/" + info.fileName());
            else QFile::copy(info.absoluteFilePath(), dst + "/" + info.fileName());
        }
        return true;
    }

    void openFolder() {
        QString dir = QFileDialog::getExistingDirectory(this, "Open Workspace Folder", QDir::currentPath());
        if (!dir.isEmpty()) { fileModel->setRootPath(dir); fileTree->setRootIndex(fileModel->index(dir)); }
    }

    void createNewFilePrompt(const QString &dirPath) {
        bool ok; QString name = QInputDialog::getText(this, "New File", "Enter file name:", QLineEdit::Normal, "new_file.cpp", &ok);
        if (ok && !name.isEmpty()) {
            QString fullPath = dirPath + "/" + name;
            QFile file(fullPath); if (file.open(QIODevice::WriteOnly)) file.close(); 
            loadFile(fullPath);
        }
    }

    void newFile() { createNewFilePrompt(fileModel->rootPath()); }

    void openFile() {
        QString fileName = QFileDialog::getOpenFileName(this, "Open", "", "C++ Files (*.cpp *.h);;All Files (*)");
        if (!fileName.isEmpty()) loadFile(fileName);
    }

    void loadFile(const QString &fileName) {
        QFile file(fileName);
        if (file.open(QFile::ReadOnly | QFile::Text)) {
            editor->setPlainText(file.readAll());
            currentFile = fileName;
            setWindowTitle("Cpp Studio - " + fileName);
            setModifiedState(false);
        }
    }

    void saveFile() {
        if (currentFile.isEmpty()) {
            currentFile = QFileDialog::getSaveFileName(this, "Save As", fileModel->rootPath() + "/main.cpp", "C++ Files (*.cpp)");
            if (currentFile.isEmpty()) return;
        }
        QFile file(currentFile);
        if (file.open(QFile::WriteOnly | QFile::Text)) {
            QTextStream out(&file);
            out << editor->toPlainText();
            setWindowTitle("Cpp Studio - " + currentFile);
            setModifiedState(false);
        }
    }

    void compileProject() {
        if (currentFile.isEmpty()) { saveFile(); if (currentFile.isEmpty()) return; } else saveFile(); 

        consoleOutput->clear();
        QString fileDir = QFileInfo(currentFile).absolutePath();
        QString exePath = QFile::exists(fileDir + "/CMakeLists.txt") ? buildWithCMake(fileDir) : buildSingleFile(QFileInfo(currentFile));

        if (!exePath.isEmpty()) {
            lastCompiledExe = exePath; lastCompiledDir = fileDir; setModifiedState(false); 
            consoleOutput->appendPlainText("\n✅ Compile Complete! Ready to Run.");
        }
    }

    void runProject() {
        if (currentFile.isEmpty()) return;
        if(appProcess->state() == QProcess::Running) { appProcess->kill(); appProcess->waitForFinished(); }

        QString exeToRun = lastCompiledExe;
        QString workDir = lastCompiledDir;
        QString fileDir = QFileInfo(currentFile).absolutePath();

        if (exeToRun.isEmpty()) {
            if (QFile::exists(fileDir + "/CMakeLists.txt")) {
                QDirIterator it(fileDir + "/build", QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
                while (it.hasNext()) {
                    it.next(); QString name = it.fileName();
                    if (!it.filePath().contains("CMakeFiles") && !name.endsWith(".so") && !name.endsWith(".a") && name != "cmake" && name != "make") 
                    { exeToRun = it.filePath(); workDir = fileDir; break; }
                }
            } else { exeToRun = fileDir + "/" + QFileInfo(currentFile).baseName() + ".out"; workDir = fileDir; }
        }

        if (exeToRun.isEmpty() || !QFile::exists(exeToRun)) { consoleOutput->appendPlainText("\n❌ Executable not found. Please click '🔨 Compile' first."); return; }
        if (isModified) consoleOutput->appendPlainText("\n⚠️ Note: Code has been modified since last compile. Running previous version...\n");
        
        consoleOutput->appendPlainText(">>> 🚀 Running: " + exeToRun + "\n");
        appProcess->setWorkingDirectory(workDir); appProcess->start(exeToRun);
    }

    QString buildWithCMake(const QString& projectDir) {
        consoleOutput->appendPlainText(">>> 🛠️ CMake Project Detected.\n>>> Generating build files...\n");
        QString buildDir = projectDir + "/build"; QDir().mkpath(buildDir); compilerProcess->setWorkingDirectory(buildDir);

        compilerProcess->start("cmake", {".."});
        while(compilerProcess->state() == QProcess::Running) QCoreApplication::processEvents(); 
        if (compilerProcess->exitCode() != 0) return "";

        consoleOutput->appendPlainText("\n>>> 🔨 Building Project...\n");
        compilerProcess->start("cmake", {"--build", "."});
        while(compilerProcess->state() == QProcess::Running) QCoreApplication::processEvents(); 
        if (compilerProcess->exitCode() != 0) return "";

        QDirIterator it(buildDir, QDir::Files | QDir::Executable, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next(); QString name = it.fileName();
            if (!it.filePath().contains("CMakeFiles") && !name.endsWith(".so") && !name.endsWith(".a") && name != "cmake" && name != "make") return it.filePath();
        }
        return "";
    }

    QString buildSingleFile(const QFileInfo& fileInfo) {
        consoleOutput->appendPlainText(">>> 📄 Single file compile...\n");
        compilerProcess->setWorkingDirectory(fileInfo.absolutePath());
        
        QStringList args;
        args << "-Wall" << ("-std=" + stdCombo->currentText()) << (modeCombo->currentText().contains("Debug") ? "-g" : "-O3") 
             << fileInfo.absoluteFilePath() << "-o" << (fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".out");

        compilerProcess->start("g++", args);
        while(compilerProcess->state() == QProcess::Running) QCoreApplication::processEvents(); 
        
        if (compilerProcess->exitCode() != 0) return "";
        return fileInfo.absolutePath() + "/" + fileInfo.baseName() + ".out";
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setStyle("Fusion"); 
    CppStudio studio;
    studio.show();
    return app.exec();
}

#include "main.moc"