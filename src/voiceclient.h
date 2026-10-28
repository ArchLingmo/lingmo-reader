/*
 * SPDX-FileCopyrightText: 2026 LingmoOS Team
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

// Client of lingmo-voice-engine (lingmo-voicestudio): JSON lines over
// $XDG_RUNTIME_DIR/lingmo-voice/engine.sock. Starts the engine on demand.
//
// The same file lives in lingmo-voicestudio/src/common/voiceclient.{h,cpp}
// and lingmo-reader/src/voiceclient.{h,cpp}: change both together.

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>

#include <functional>

class QLocalSocket;
class QTimer;

class VoiceClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    // the engine is installed (lingmo-voicestudio); without it "Ouvir" explains how to get it
    Q_PROPERTY(bool installed READ installed CONSTANT)

public:
    using Reply = std::function<void(const QJsonObject &)>;

    explicit VoiceClient(QObject *parent = nullptr);
    ~VoiceClient() override;

    bool connected() const;
    bool installed() const;
    static QString socketPath();

    // Sends a command; `done` gets the final reply ({"ok": ...}), `event` the
    // intermediate ones of the same request (e.g. "audio" before alignment).
    // Returns the request id.
    int call(const QString &cmd, QJsonObject fields, Reply done = {}, Reply event = {});
    void cancelReplies(int id);

Q_SIGNALS:
    void connectedChanged();
    // events not tied to a request: "job", "install", "engines", "log"
    void broadcast(const QJsonObject &event);
    void failed(const QString &message);

private:
    void ensureConnected();
    void onReadyRead();
    void flushQueue();
    void failAll(const QString &message);

    QLocalSocket *m_socket;
    QTimer *m_retry;
    int m_nextId = 1;
    int m_attempts = 0;
    bool m_started = false;
    QByteArray m_buffer;
    QList<QByteArray> m_queue;
    struct Pending { Reply done; Reply event; };
    QHash<int, Pending> m_pending;
};
