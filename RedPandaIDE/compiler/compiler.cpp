#include "compiler.h"
#include "utils.h"

#include <QFileInfo>
#include <QProcess>
#include <QString>
#include <QTextCodec>
#include <QDebug>
#include <QTime>

Compiler::Compiler(bool silent,bool onlyCheckSyntax):
    QThread(),
    mSilent(silent),
    mOnlyCheckSyntax(onlyCheckSyntax)
{

}

void Compiler::run()
{
    emit compileStarted();
    if (prepareForCompile()){
        mErrorCount = 0;
        mWarningCount = 0;
        QElapsedTimer timer;
        timer.start();
        runCommand(mCompiler, mArguments, QFileInfo(mCompiler).absolutePath());

        log("");
        log(tr("Compile Result:"));
        log("------------------");
        log(tr("- Errors: %1").arg(mErrorCount));
        log(tr("- Warnings: %1").arg(mWarningCount));
        if (!mOutputFile.isEmpty()) {
            log(tr("- Output Filename: %1").arg(mOutputFile));
            QLocale locale = QLocale::system();
            log(tr("- Output Size: %1").arg(locale.formattedDataSize(QFileInfo(mOutputFile).size())));
        }
        log(tr("- Compilation Time: %1 secs").arg(timer.elapsed() / 1000.0));
    }
    emit compileFinished();
}

QString Compiler::getFileNameFromOutputLine(QString &line) {
    QString temp;
    line = line.trimmed();
    while (true) {
        int pos;
        if (line.length() > 2 && line[1]==':') { // full file path at start, ignore this ':'
            pos = line.indexOf(':',2);
        } else {
            pos = line.indexOf(':');
        }
        if ( pos < 0) {
            break;
        }
        temp = line.mid(0,pos);
        line.remove(0,pos+1);
        if (temp.compare("<stdin>", Qt::CaseInsensitive)!=0 && !QFile(temp).exists()) {
            continue;
        }

        if (QFile(temp).fileName() == QLatin1String("ld.exe")) { // skip ld.exe
            continue;
        } else {
            break;
        }
    }
    return temp;
}

int Compiler::getLineNumberFromOutputLine(QString &line)
{
    line = line.trimmed();
    int pos = line.indexOf(':');
    int result=0;
    if (pos < 0) {
        pos = line.indexOf(',');
    }
    if (pos>=0) {
        result = line.mid(0,pos).toInt();
        line.remove(0,pos+1);
    }
    return result;
}

int Compiler::getColunmnFromOutputLine(QString &line)
{
    line = line.trimmed();
    int pos = line.indexOf(':');
    int result=0;
    if (pos < 0) {
        pos = line.indexOf(',');
    }
    if (pos>=0) {
        result = line.mid(0,pos).toInt();
        line.remove(0,pos+1);
    }
    return result;
}

CompileIssueType Compiler::getIssueTypeFromOutputLine(QString &line)
{
    CompileIssueType result = CompileIssueType::Other;
    line = line.trimmed();
    int pos = line.indexOf(':');
    if (pos>=0) {
        QString s=line.mid(0,pos);
        if (s == "error" || s == "fatal error") {
            mErrorCount += 1;
            line = tr("[Error] ")+line.mid(pos+1);
            result = CompileIssueType::Error;
        } else if (s == "warning") {
            mWarningCount += 1;
            line = tr("[Warning] ")+line.mid(pos+1);
            result = CompileIssueType::Warning;
        } else if (s == "info") {
            mWarningCount += 1;
            line = tr("[Info] ")+line.mid(pos+1);
            result = CompileIssueType::Info;
        } else if (s == "note") {
            mWarningCount += 1;
            line = tr("[Note] ")+line.mid(pos+1);
            result = CompileIssueType::Note;
        }
    }
    return result;
}

void Compiler::processOutput(QString &line)
{
    QString inFilePrefix = QString("In file included from ");
    QString fromPrefix = QString("from ");
    PCompileIssue issue = std::make_shared<CompileIssue>();
    QString description;
    issue->type = CompileIssueType::Other;
    if (line.startsWith(inFilePrefix)) {
        line.remove(0,inFilePrefix.length());
        issue->filename = getFileNameFromOutputLine(line);
        issue->line = getLineNumberFromOutputLine(line);
        issue->column = getColunmnFromOutputLine(line);
        issue->type = getIssueTypeFromOutputLine(line);
        issue->description = inFilePrefix + issue->filename;
        emit compileIssue(issue);
        return;
    } else if(line.startsWith(fromPrefix)) {
        line.remove(0,fromPrefix.length());
        issue->filename = getFileNameFromOutputLine(line);
        issue->line = getLineNumberFromOutputLine(line);
        issue->column = getColunmnFromOutputLine(line);
        issue->type = getIssueTypeFromOutputLine(line);
        issue->description = "                 from " + issue->filename;
        emit compileIssue(issue);
        return;
    }

    // Ignore code snippets that GCC produces
    // they always start with a space
    if (line.length()>0 && line[0] == ' ') {
        return;
    }
    // assume regular main.cpp:line:col: message
    issue->filename = getFileNameFromOutputLine(line);
    issue->line = getLineNumberFromOutputLine(line);
    issue->column = getColunmnFromOutputLine(line);
    issue->type = getIssueTypeFromOutputLine(line);
    issue->description = line.trimmed();
    emit compileIssue(issue);
}

void Compiler::stopCompile()
{
    mStop = true;
}

QString Compiler::getCharsetArgument(const QByteArray& encoding)
{
    QString result;
    if (compilerSet()->autoAddCharsetParams() && encoding != ENCODING_ASCII) {
        QString encodingName;
        QString systemEncodingName=QTextCodec::codecForLocale()->name();
        if (encoding == ENCODING_SYSTEM_DEFAULT) {
            encodingName = systemEncodingName;
        } else if (encoding == ENCODING_UTF8_BOM) {
            encodingName = "UTF-8";
        } else {
            encodingName = encoding;
        }
        result += QString(" -finput-charset=%1 -fexec-charset=%2")
                .arg(encodingName)
                .arg(systemEncodingName);
    }
    return result;
}

QString Compiler::getCCompileArguments(bool checkSyntax)
{
    QString result;
    if (checkSyntax) {
        result += " -fsyntax-only";
    }

    for (PCompilerOption pOption: compilerSet()->options()) {
        if (pOption->value > 0 && pOption->isC) {
            if (pOption->choices.isEmpty()) {
                result += " " + pOption->setting;
            } else if (pOption->value < pOption->choices.size()) {
                QStringList nameValue=pOption->choices[pOption->value].split('=');
                if (nameValue.count()==2) {
                    result += " " + pOption->setting + nameValue[1];
                }
            }
        }
    }

    if (compilerSet()->useCustomCompileParams() && !compilerSet()->customCompileParams().isEmpty()) {
        result += " "+compilerSet()->customCompileParams();
    }
    return result;
}

QString Compiler::getCppCompileArguments(bool checkSyntax)
{
    QString result;
    if (checkSyntax) {
        result += " -fsyntax-only";
    }

    for (PCompilerOption pOption: compilerSet()->options()) {
        if (pOption->value > 0 && pOption->isCpp) {
            if (pOption->choices.isEmpty()) {
                result += " "+pOption->setting;
            } else if (pOption->value < pOption->choices.size()) {
                QStringList nameValue=pOption->choices[pOption->value].split('=');
                if (nameValue.count()==2) {
                    result += " "+pOption->setting + nameValue[1];
                }
            }
        }
    }

    if (compilerSet()->useCustomCompileParams() && !compilerSet()->customCompileParams().isEmpty()) {
        result += " "+compilerSet()->customCompileParams();
    }
    return result;
}


QString Compiler::getCIncludeArguments()
{
    QString result;
    for (const QString& folder:compilerSet()->CIncludeDirs()) {
        result += QString(" -I\"%1\"").arg(folder);
    }
    return result;
}

QString Compiler::getCppIncludeArguments()
{
    QString result;
    for (const QString& folder:compilerSet()->CppIncludeDirs()) {
        result += QString(" -I\"%1\"").arg(folder);
    }
    return result;
}

QString Compiler::getLibraryArguments()
{
    QString result;

    for (const QString& folder:compilerSet()->libDirs()) {
        result += QString(" -L\"%1\"").arg(folder);
    }

    // Add global compiler linker extras
    if (compilerSet()->useCustomLinkParams() && !compilerSet()->customLinkParams().isEmpty()) {
       result += " "+compilerSet()->customCompileParams();
    }

    //options like "-static" must be added after "-lxxx"
    for (PCompilerOption pOption: compilerSet()->options()) {
        if (pOption->value > 0 && pOption->isLinker) {
            if (pOption->choices.isEmpty()) {
                result += " " + pOption->setting;
            } else if (pOption->value < pOption->choices.size()) {
                QStringList nameValue=pOption->choices[pOption->value].split('=');
                if (nameValue.count()==2) {
                    result += " " + pOption->setting + nameValue[1];
                }
            }
        }
    }
    return result;
}

void Compiler::runCommand(const QString &cmd, const QString  &arguments, const QString &workingDir, const QString& inputText)
{
    QProcess process;
    mStop = false;
    process.setProgram(cmd);
    process.setArguments(QProcess::splitCommand(arguments));
    process.setWorkingDirectory(workingDir);

    process.connect(&process, &QProcess::readyReadStandardError,[&process,this](){
        this->error(process.readAllStandardError());
    });
    process.connect(&process, &QProcess::readyReadStandardOutput,[&process,this](){
        this->log(process.readAllStandardOutput());
    });
    process.start();
    if (!inputText.isEmpty())
        process.write(inputText.toUtf8());
    process.closeWriteChannel();
    process.waitForStarted(5000);
    while (true) {
        process.waitForFinished(1000);
        if (process.state()!=QProcess::Running) {
            break;
        }
        if (mStop) {
            process.kill();
            break;
        }
    }
}

void Compiler::log(const QString &msg)
{
    emit compileOutput(msg);
}

void Compiler::error(const QString &msg)
{
    emit compileOutput(msg);
    for (QString& s:msg.split("\n")) {
        if (!s.isEmpty())
            processOutput(s);
    }
}

