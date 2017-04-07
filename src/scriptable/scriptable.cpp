/*
    Copyright (c) 2017, Lukas Holecek <hluk@email.cz>

    This file is part of CopyQ.

    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "scriptable.h"

#include "common/arguments.h"
#include "common/commandstatus.h"
#include "common/log.h"
#include "common/mimetypes.h"
#include "common/textdata.h"
#include "scriptable/commandhelp.h"
#include "scriptable/scriptablebytearray.h"
#include "scriptable/scriptabledir.h"
#include "scriptable/scriptablefile.h"
#include "scriptable/scriptableproxy.h"
#include "scriptable/scriptabletemporaryfile.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QJSEngine>
#include <QJSValueIterator>
#include <QMetaType>
#include <Qt>

namespace {

const char *const programName = "CopyQ Clipboard Manager";

const int setClipboardMaxRetries = 3;

QString helpHead()
{
    return Scriptable::tr("Usage: copyq [%1]").arg(Scriptable::tr("COMMAND")) + "\n\n"
        + Scriptable::tr("Starts server if no command is specified.") + "\n"
        + Scriptable::tr("  COMMANDs:");
}

QString helpTail()
{
    return Scriptable::tr("NOTES:") + "\n"
        + Scriptable::tr("  - Use dash argument (-) to read data from stdandard input.") + "\n"
        + Scriptable::tr("  - Use double-dash argument (--) to read all following arguments without\n"
                      "    expanding escape sequences (i.e. \\n, \\t and others).") + "\n"
        + Scriptable::tr("  - Use ? for MIME to print available MIME types (default is \"text/plain\").");
}

QJSValue newByteArray(const QByteArray &bytes, const Scriptable *scriptable)
{
    return scriptable->engine()->newQObject( new ScriptableByteArray(bytes) );
}

const QByteArray *getByteArray(const QJSValue &value, const Scriptable *scriptable)
{
    const auto byteArray = scriptable->engine()->fromScriptValue<ScriptableByteArray*>(value);
    return byteArray ? byteArray->data() : nullptr;
}

QString toString(const QJSValue &value, const Scriptable *scriptable)
{
    const auto bytes = getByteArray(value, scriptable);
    return (bytes == nullptr) ? value.toString() : getTextData(*bytes);
}

QByteArray serializeScriptValue(const QJSValue &value, const Scriptable *scriptable)
{
    QByteArray data;

    if ( value.isArray() ) {
        const quint32 len = value.property("length").toUInt();
        for (quint32 i = 0; i < len; ++i)
            data += serializeScriptValue(value.property(i), scriptable);
    } else {
        const auto bytes = getByteArray(value, scriptable);
        if (bytes)
            data = *bytes;
        else if ( !value.isUndefined() )
            data = value.toString().toUtf8() + '\n';
    }

    return data;
}

QString createScriptErrorMessage(const QString &text)
{
    return "ScriptError: " + text;
}

void logScriptError(const QString &text)
{
    log( createScriptErrorMessage(text), LogNote );
}

QString messageCodeToString(int code)
{
    switch (code) {
    case CommandArguments:
        return "CommandArguments";
    case CommandReadInputReply:
        return "CommandReadInputReply";
    default:
        return QString("Unknown(%1)").arg(code);
    }
}

} // namespace

Scriptable::Scriptable(QJSEngine *engine, ScriptableProxy *proxy)
    : m_engine(engine)
    , m_proxy(proxy)
    , m_self( m_engine->newQObject(this) )
{
    QJSValue globalObject = m_engine->globalObject();

    QJSValueIterator it(m_self);
    while (it.hasNext()) {
        it.next();
        globalObject.setProperty( it.name(), it.value() );
    }

    globalObject.setProperty("mimeText", mimeText);
    globalObject.setProperty("mimeHtml", mimeHtml);
    globalObject.setProperty("mimeUriList", mimeUriList);
    globalObject.setProperty("mimeWindowTitle", mimeWindowTitle);
    globalObject.setProperty("mimeItems", mimeItems);
    globalObject.setProperty("mimeItemNotes", mimeItemNotes);
    globalObject.setProperty("mimeOwner", mimeOwner);
    globalObject.setProperty("mimeClipboardMode", mimeClipboardMode);
    globalObject.setProperty("mimeCurrentTab", mimeCurrentTab);
    globalObject.setProperty("mimeSelectedItems", mimeSelectedItems);
    globalObject.setProperty("mimeCurrentItem", mimeCurrentItem);
    globalObject.setProperty("mimeHidden", mimeHidden);
    globalObject.setProperty("mimeShortcut", mimeShortcut);
    globalObject.setProperty("mimeColor", mimeColor);
    globalObject.setProperty("mimeOutputTab", mimeOutputTab);
    globalObject.setProperty("mimeSyncToClipboard", mimeSyncToClipboard);
    globalObject.setProperty("mimeSyncToSelection", mimeSyncToSelection);

    globalObject.setProperty("inputSeparator", "\n");

    const auto byteArrayMetaObject = m_engine->newQMetaObject<ScriptableByteArray>();
    globalObject.setProperty("ByteArray", byteArrayMetaObject);

    auto dirMetaObject = m_engine->newQMetaObject<ScriptableDir>();
    dirMetaObject.setProperty("homePath", QDir::homePath());
    globalObject.setProperty("Dir", dirMetaObject);

    const auto fileMetaObject = m_engine->newQMetaObject<ScriptableFile>();
    globalObject.setProperty("File", fileMetaObject);

    const auto temporaryFileMetaObject = m_engine->newQMetaObject<ScriptableTemporaryFile>();
    globalObject.setProperty("TemporaryFile", temporaryFileMetaObject);
}

QByteArray Scriptable::fromString(const QJSValue &value)
{
    Q_UNUSED(value);
    return QByteArray();
}

bool Scriptable::toInt(const QJSValue &value, int *number) const
{
    bool ok;
    *number = toString(value, this).toInt(&ok);
    return ok;
}

QJSValue Scriptable::arguments() const
{
    return m_engine->evaluate("arguments");
}

int Scriptable::argumentCount() const
{
    return arguments().property("length").toInt();
}

QJSValue Scriptable::argument(int index) const
{
    return arguments().property( static_cast<quint32>(index) );
}

QString Scriptable::arg(int i, const QString &defaultValue)
{
    return i < argumentCount() ? toString(argument(i), this) : defaultValue;
}

QJSValue Scriptable::throwError(const QString &errorMessage)
{
    m_engine->globalObject().setProperty("_copyqExceptionText", errorMessage);
    return m_engine->evaluate("throw _copyqExceptionText");
}

QJSValue Scriptable::version()
{
    m_skipArguments = 0;
    return tr(programName) + " " COPYQ_VERSION " (hluk@email.cz)\n"
            + tr("Built with: ")
            + "Qt " + QT_VERSION_STR +
            + '\n';
}

QJSValue Scriptable::help()
{
    m_skipArguments = -1;

    QString helpString;

    if ( argumentCount() == 0 ) {
        helpString.append(helpHead() + "\n");

        for (const auto &hlp : commandHelp())
            helpString.append(hlp.toString());

        helpString.append("\n" + helpTail() + "\n\n" + tr(programName)
            + " " + COPYQ_VERSION + " (hluk@email.cz)\n");
    } else {
        for (int i = 0; i < argumentCount(); ++i) {
            const QString &cmd = toString(argument(i), this);
            for (const auto &helpItem : commandHelp()) {
                if ( helpItem.cmd.contains(cmd) )
                    helpString.append(helpItem.toString());
            }
        }

        if ( helpString.isEmpty() )
            return throwError( tr("Command not found!") );
    }

    return helpString;
}

void Scriptable::show()
{
    m_skipArguments = 1;

    if ( argumentCount() == 0 )
        m_proxy->showWindow();
    else
        m_proxy->showBrowser( toString(argument(0), this) );
}

void Scriptable::showAt()
{
    QRect rect(-1, -1, 0, 0);
    int n;
    int i = 0;
    if ( toInt(argument(i), &n) ) {
        rect.setX(n);
        ++i;

        if ( toInt(argument(i), &n) ) {
            rect.setY(n);
            ++i;

            if ( toInt(argument(i), &n) ) {
                rect.setWidth(n);
                ++i;

                if ( toInt(argument(i), &n) ) {
                    rect.setHeight(n);
                    ++i;
                }
            }
        }
    }

    m_skipArguments = i;

    const auto tabName = arg(i++);
    if ( tabName.isEmpty() )
        m_proxy->showWindowAt(rect);
    else
        m_proxy->showBrowserAt(tabName, rect);
}

void Scriptable::hide()
{
    m_skipArguments = 0;
    m_proxy->close();
}

QJSValue Scriptable::toggle()
{
    m_skipArguments = 0;
    return m_proxy->toggleVisible();
}

QJSValue Scriptable::menu()
{
    m_skipArguments = 4;

    if (argumentCount() == 0) {
        m_proxy->toggleMenu();
    } else {
        const auto tabName = toString(argument(0), this);

        int maxItemCount = -1;
        if (argumentCount() >= 2) {
            const auto value = argument(1);
            if ( !toInt(value, &maxItemCount) || maxItemCount <= 0 )
                return throwError("Argument maxItemCount must be positive number");
        }

        int x = -1;
        int y = -1;
        if (argumentCount() >= 3) {
            const auto xValue = argument(2);
            const auto yValue = argument(3);
            if ( !toInt(xValue, &x) || x < 0 || !toInt(yValue, &y) || y < 0 )
                return throwError("Coordinates must be positive numbers");
        }

        m_proxy->toggleMenu( tabName, maxItemCount, QPoint(x, y) );
    }

    return QJSValue();
}

void Scriptable::exit()
{
    m_skipArguments = 0;
    QByteArray message = fromString( tr("Terminating server.\n") );
    emit sendMessage(message, CommandPrint);
    m_proxy->exit();
}

void Scriptable::disable()
{
    m_skipArguments = 0;
    m_proxy->disableMonitoring(true);
}

void Scriptable::enable()
{
    m_skipArguments = 0;
    m_proxy->disableMonitoring(false);
}

QJSValue Scriptable::monitoring()
{
    m_skipArguments = 0;
    return m_proxy->isMonitoringEnabled();
}

QJSValue Scriptable::visible()
{
    m_skipArguments = 0;
    return m_proxy->isMainWindowVisible();
}

QJSValue Scriptable::focused()
{
    m_skipArguments = 0;
    return m_proxy->isMainWindowFocused();
}

QJSValue Scriptable::eval()
{
    const auto script = arg(0);
    const auto result = eval(script);
    m_skipArguments = -1;
    return result;
}

QJSValue Scriptable::input()
{
    m_skipArguments = 0;

    /// TODO
    getByteArray(m_input, this);
    /*
    if ( !getByteArray(m_input, this) ) {
        emit sendMessage(QByteArray(), CommandReadInput);
        while ( m_connected && !getByteArray(m_input, this) )
            QCoreApplication::processEvents();
    }
    */

    return m_input;
}

void Scriptable::onMessageReceived(const QByteArray &bytes, int messageCode)
{
    COPYQ_LOG( "Message received: " + messageCodeToString(messageCode) );

    if (messageCode == CommandArguments)
        executeArguments(bytes);
    else if (messageCode == CommandReadInputReply)
        m_input = newByteArray(bytes, this);
    else
        log("Incorrect message code from client", LogError);
}

void Scriptable::onDisconnected()
{
    m_connected = false;
}

void Scriptable::executeArguments(const QByteArray &bytes)
{
    Arguments args;
    QDataStream stream(bytes);
    stream >> args;
    if ( stream.status() != QDataStream::Ok ) {
        log("Failed to read client arguments", LogError);
        return;
    }

    if ( hasLogLevel(LogDebug) ) {
        const bool isEval = args.length() == Arguments::Rest + 3
                && args.at(Arguments::Rest) == "eval"
                && args.at(Arguments::Rest + 1) == "--";

        const int skipArgs = isEval ? 2 : 0;
        auto msg = QString("Client-%1:").arg( getTextData(args.at(Arguments::ProcessId)) );
        for (int i = Arguments::Rest + skipArgs; i < args.length(); ++i) {
            const QString indent = isEval
                    ? QString()
                    : (QString::number(i - Arguments::Rest + 1) + " ");
            msg.append( "\n" + indent + getTextData(args.at(i)) );
        }
        COPYQ_LOG(msg);
    }

    const auto currentPath = getTextData(args.at(Arguments::CurrentPath));
    m_engine->globalObject().setProperty("currentPath", currentPath);

    bool hasData;
    const int id = args.at(Arguments::ActionId).toInt(&hasData);
    if (hasData)
        m_data = m_proxy->getActionData(id);
    const auto oldData = m_data;

    m_actionName = getTextData( args.at(Arguments::ActionName) );

    QByteArray response;
    int exitCode;

    if ( args.isEmpty() ) {
        logScriptError("Bad command syntax");
        exitCode = CommandBadSyntax;
    } else {
        /* Special arguments:
             * "-"  read this argument from stdin
             * "--" read all following arguments without control sequences
             */
        QJSValueList fnArgs;
        bool readRaw = false;
        for ( int i = Arguments::Rest; i < args.length(); ++i ) {
            const QByteArray &arg = args.at(i);
            if (!readRaw && arg == "--") {
                readRaw = true;
            } else {
                const QJSValue value = readRaw || arg != "-"
                        ? newByteArray(arg, this)
                        : input();
                fnArgs.append(value);
            }
        }

        QString cmd;
        QJSValue result;

        int skipArguments = 0;
        while ( skipArguments < fnArgs.size() && !result.isError() ) {
            if ( result.isCallable() ) {
                m_skipArguments = -1;
                result = result.call( fnArgs.mid(skipArguments) );
                if (m_skipArguments == -1)
                    break;
                skipArguments += m_skipArguments;
            } else {
                cmd = toString(fnArgs[skipArguments], this);
                ++skipArguments;

                // Set "arguments" array for script.
                const auto argumentCount = fnArgs.length() - skipArguments;
                auto arguments = m_engine->newArray( argumentCount < 0 ? 0 : static_cast<uint>(argumentCount) );
                for ( int i = skipArguments; i < fnArgs.length(); ++i )
                    arguments.setProperty( static_cast<quint32>(i - skipArguments), fnArgs[i] );
                m_engine->globalObject().setProperty("arguments", arguments);

                result = eval(cmd);
            }
        }

        if ( result.isCallable() )
            result = result.call( fnArgs.mid(skipArguments) );

        if ( result.isError() ) {
            const auto exceptionText = result.toString();
            response = createScriptErrorMessage(exceptionText).toUtf8();
            exitCode = CommandException;
        } else {
            response = serializeScriptValue(result, this);
            exitCode = CommandFinished;
        }
    }

    if (exitCode == CommandFinished && hasData && oldData != m_data)
        m_proxy->setActionData(id, m_data);

    // Destroy objects so destructors are run before script finishes
    // (e.g. file writes are flushed or temporary files are automatically removed).
    m_engine->collectGarbage();

    emit sendMessage(response, exitCode);

    COPYQ_LOG("DONE");
}

QJSValue Scriptable::eval(const QString &script, const QString &fileName)
{
    // TODO: Check syntax.
    return m_engine->evaluate(script, fileName);
}

QJSValue Scriptable::eval(const QString &script)
{
    const int i = script.indexOf('\n');
    const QString name = quoteString( i == -1 ? script : script.mid(0, i) + "..." );
    return eval(script, name);
}
