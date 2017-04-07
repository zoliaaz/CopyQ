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

#ifndef SCRIPTABLE_H
#define SCRIPTABLE_H

#include <QJSValue>
#include <QObject>

class QByteArray;
class QJSEngine;
class ScriptableProxy;

class Scriptable : public QObject
{
    Q_OBJECT

public:
    Scriptable(QJSEngine *engine, ScriptableProxy *proxy);

    QByteArray fromString(const QJSValue &value);

    bool isConnected() const { return m_connected; }

public slots:
    void onMessageReceived(const QByteArray &bytes, int messageCode);
    void onDisconnected();

signals:
    void sendMessage(const QByteArray &message, int messageCode);

private:
    void executeArguments(const QByteArray &bytes);

    QJSValue m_input;
    bool m_connected = true;
};

#endif // SCRIPTABLE_H
