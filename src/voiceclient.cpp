/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "voiceclient.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace {

QString engineExecutable()
{
    const QString env = qEnvironmentVariable("LINGMO_VOICE_ENGINE");
    if (!env.isEmpty())
        return env;
    return QStandardPaths::findExecutable(QStringLiteral("lingmo-voice-engine"));
}

} // namespace

VoiceClient::VoiceClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_retry(new QTimer(this))
{
    m_retry->setSingleShot(true);
    connect(m_retry, &QTimer::timeout, this, &VoiceClient::ensureConnected);
    connect(m_socket, &QLocalSocket::connected, this, [this] {
        m_attempts = 0;
        Q_EMIT connectedChanged();
        flushQueue();
    });
    connect(m_socket, &QLocalSocket::disconnected, this, [this] {
        m_buffer.clear();
        Q_EMIT connectedChanged();
        failAll(tr("The voice engine stopped."));
    });
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError) {
        if (m_socket->state() == QLocalSocket::ConnectedState)
            return;
        // not running yet: start it once, then retry for a few seconds
        if (!m_started) {
            m_started = true;
            const QString exe = engineExecutable();
            if (exe.isEmpty()) {
                failAll(tr("The voice engine is not installed (lingmo-voicestudio)."));
                return;
            }
            QProcess::startDetached(exe, {});
        }
        if (++m_attempts < 60)
            m_retry->start(m_attempts < 20 ? 100 : 250);
        else
            failAll(tr("The voice engine did not start."));
    });
    connect(m_socket, &QLocalSocket::readyRead, this, &VoiceClient::onReadyRead);
}

VoiceClient::~VoiceClient() = default;

QString VoiceClient::socketPath()
{
    const QString env = qEnvironmentVariable("LINGMO_VOICE_SOCKET");
    if (!env.isEmpty())
        return env;
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QStringLiteral("/tmp");
    return runtime + QStringLiteral("/lingmo-voice/engine.sock");
}

bool VoiceClient::connected() const
{
    return m_socket->state() == QLocalSocket::ConnectedState;
}

bool VoiceClient::installed() const
{
    return !engineExecutable().isEmpty() || QFileInfo::exists(socketPath());
}

void VoiceClient::ensureConnected()
{
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_socket->connectToServer(socketPath());
}

int VoiceClient::call(const QString &cmd, QJsonObject fields, Reply done, Reply event)
{
    const int id = m_nextId++;
    fields.insert(QStringLiteral("cmd"), cmd);
    fields.insert(QStringLiteral("id"), id);
    m_pending.insert(id, {std::move(done), std::move(event)});
    m_queue << QJsonDocument(fields).toJson(QJsonDocument::Compact) + '\n';
    if (connected()) {
        flushQueue();
    } else {
        if (m_socket->state() == QLocalSocket::UnconnectedState && !m_retry->isActive()) {
            m_attempts = 0;
            m_started = false;
        }
        ensureConnected();
    }
    return id;
}

void VoiceClient::cancelReplies(int id)
{
    m_pending.remove(id);
}

void VoiceClient::flushQueue()
{
    for (const QByteArray &line : std::as_const(m_queue))
        m_socket->write(line);
    m_queue.clear();
    m_socket->flush();
}

void VoiceClient::onReadyRead()
{
    m_buffer += m_socket->readAll();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        const QJsonObject msg = QJsonDocument::fromJson(line).object();
        if (msg.isEmpty())
            continue;
        const int id = msg.value(QStringLiteral("id")).toInt(0);
        if (id && m_pending.contains(id)) {
            if (msg.contains(QStringLiteral("ok"))) {
                const Pending p = m_pending.take(id);
                if (p.done)
                    p.done(msg);
            } else if (m_pending[id].event) {
                m_pending[id].event(msg);
            }
            continue;
        }
        if (!id)
            Q_EMIT broadcast(msg);
    }
}

void VoiceClient::failAll(const QString &message)
{
    const auto pending = m_pending;
    m_pending.clear();
    m_queue.clear();
    for (const Pending &p : pending) {
        if (p.done)
            p.done(QJsonObject{{QStringLiteral("ok"), false}, {QStringLiteral("error"), message}, {QStringLiteral("code"), QStringLiteral("engine")}});
    }
    if (!pending.isEmpty())
        Q_EMIT failed(message);
}
