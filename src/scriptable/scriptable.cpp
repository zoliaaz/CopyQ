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

#include "common/commandstatus.h"
#include "common/log.h"

#include <QByteArray>
#include <Qt>

namespace {

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

QJSValue newByteArray(const QByteArray &bytes)
{
    Q_UNUSED(bytes);
    return QJSValue();
}

} // namespace

Scriptable::Scriptable(QJSEngine *engine, ScriptableProxy *proxy)
{
    Q_UNUSED(engine);
    Q_UNUSED(proxy);
}

QByteArray Scriptable::fromString(const QJSValue &value)
{
    Q_UNUSED(value);
    return QByteArray();
}

void Scriptable::onMessageReceived(const QByteArray &bytes, int messageCode)
{
    COPYQ_LOG( "Message received: " + messageCodeToString(messageCode) );

    if (messageCode == CommandArguments)
        executeArguments(bytes);
    else if (messageCode == CommandReadInputReply)
        m_input = newByteArray(bytes);
    else
        log("Incorrect message code from client", LogError);
}

void Scriptable::onDisconnected()
{
    m_connected = false;
}

void Scriptable::executeArguments(const QByteArray &bytes)
{
    Q_UNUSED(bytes);
    emit sendMessage(QByteArray(), CommandError);
}
