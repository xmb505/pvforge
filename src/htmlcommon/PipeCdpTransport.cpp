#include "htmlcommon/PipeCdpTransport.h"

#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pvf {
namespace html {

namespace {

// A screenshot reply for an 8192x4320 frame can approach 200 MiB once base64
// encoded (high-entropy PNG). Anything past this is a protocol failure, not a
// valid CDP message.
constexpr qsizetype kMaxIncomingMessage = 512 * 1024 * 1024;

#ifdef Q_OS_UNIX
bool setNonBlocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

} // namespace

PipeCdpTransport::PipeCdpTransport(int readFd, int writeFd, QObject* parent)
    : CdpTransport(parent)
    , m_readFd(readFd)
    , m_writeFd(writeFd)
{
}

PipeCdpTransport::~PipeCdpTransport()
{
    close();
}

bool PipeCdpTransport::connectTo(const QUrl& endpoint, int timeoutMs, QString* error)
{
    Q_UNUSED(endpoint);
    Q_UNUSED(timeoutMs);

    if (m_connected)
        return true;
#ifndef Q_OS_UNIX
    if (error)
        *error = QStringLiteral("the CDP pipe transport is only implemented on Unix");
    return false;
#else
    if (m_readFd < 0 || m_writeFd < 0)
    {
        if (error)
            *error = QStringLiteral("no CDP pipe descriptors were handed off");
        return false;
    }
    if (!setNonBlocking(m_readFd) || !setNonBlocking(m_writeFd))
    {
        if (error)
            *error = QStringLiteral("cannot make the CDP pipe non-blocking: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    m_readNotifier = new QSocketNotifier(m_readFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { onReadable(); });

    m_writeNotifier = new QSocketNotifier(m_writeFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this,
            [this](QSocketDescriptor, QSocketNotifier::Type) { onWritable(); });

    m_connected = true;
    m_closing = false;
    m_failed = false;
    return true;
#endif
}

void PipeCdpTransport::close()
{
    m_closing = true;
    m_connected = false;
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
#ifdef Q_OS_UNIX
    if (m_readFd >= 0)
        ::close(m_readFd);
    if (m_writeFd >= 0)
        ::close(m_writeFd);
#endif
    m_readFd = -1;
    m_writeFd = -1;
    m_rx.clear();
    m_tx.clear();
}

void PipeCdpTransport::sendMessage(const QString& message)
{
    if (!m_connected || m_writeFd < 0)
    {
        emit errorOccurred(QStringLiteral("cannot send a CDP message: the DevTools pipe is closed"));
        return;
    }

    QByteArray payload = message.toUtf8();
    payload.append('\0');
    m_tx.append(payload);
    flushWrites();
}

void PipeCdpTransport::flushWrites()
{
#ifdef Q_OS_UNIX
    while (m_connected && !m_tx.isEmpty())
    {
        const ssize_t written = ::write(m_writeFd, m_tx.constData(), size_t(m_tx.size()));
        if (written > 0)
        {
            m_tx.remove(0, int(written));
            continue;
        }
        if (written < 0 && errno == EINTR)
            continue;
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        fail(QStringLiteral("cannot write to the DevTools pipe: %1")
                 .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
    if (m_writeNotifier)
        m_writeNotifier->setEnabled(m_connected && !m_tx.isEmpty());
#endif
}

void PipeCdpTransport::onWritable()
{
    flushWrites();
}

void PipeCdpTransport::onReadable()
{
#ifdef Q_OS_UNIX
    char buffer[65536];
    for (;;)
    {
        const ssize_t readBytes = ::read(m_readFd, buffer, sizeof(buffer));
        if (readBytes > 0)
        {
            m_rx.append(buffer, int(readBytes));
            if (m_rx.size() > kMaxIncomingMessage)
            {
                fail(QStringLiteral("the DevTools pipe sent a message larger than %1 MiB")
                         .arg(kMaxIncomingMessage / (1024 * 1024)));
                return;
            }
            continue;
        }
        if (readBytes == 0)
        {
            fail(QStringLiteral("Chrome closed the DevTools pipe"));
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        fail(QStringLiteral("cannot read from the DevTools pipe: %1")
                 .arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    int terminator = m_rx.indexOf('\0');
    while (m_connected && terminator >= 0)
    {
        const QByteArray message = m_rx.left(terminator);
        m_rx.remove(0, terminator + 1);
        if (!message.isEmpty())
            emit messageReceived(QString::fromUtf8(message));
        terminator = m_rx.indexOf('\0');
    }
#endif
}

void PipeCdpTransport::fail(const QString& reason)
{
    if (m_failed)
        return;
    m_failed = true;
    m_connected = false;
    delete m_readNotifier;
    m_readNotifier = nullptr;
    delete m_writeNotifier;
    m_writeNotifier = nullptr;
#ifdef Q_OS_UNIX
    if (m_readFd >= 0)
        ::close(m_readFd);
    if (m_writeFd >= 0)
        ::close(m_writeFd);
#endif
    m_readFd = -1;
    m_writeFd = -1;
    m_rx.clear();
    m_tx.clear();
    if (!m_closing)
        emit disconnected(reason);
}

bool cdpPipeTransportSupported()
{
#ifdef Q_OS_UNIX
    return true;
#else
    return false;
#endif
}

CdpTransport* createPipeCdpTransport(int readFd, int writeFd, QObject* parent)
{
    return new PipeCdpTransport(readFd, writeFd, parent);
}

} // namespace html
} // namespace pvf
