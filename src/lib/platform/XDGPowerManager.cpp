/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "XDGPowerManager.h"
#include "base/Event.h"
#include "base/IEventQueue.h"
#include "common/Constants.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDebug>

namespace {

bool sleepInhibitCall(bool state, XDGPowerManager::InhibitScreenServices serviceID)
{
  if (std::string error; !XDGPowerManager::inhibitScreenCall(serviceID, state, error)) {
    qDebug("dbus inhibit error %s", error.c_str());
    return false;
  }

  return true;
}

} // namespace

XDGPowerManager::XDGPowerManager(QObject *parent) : QObject(parent)
{
}

void XDGPowerManager::watchScreenLock(IEventQueue *events, void *eventTarget)
{
  m_events = events;
  m_eventTarget = eventTarget;
  auto sessionBus = QDBusConnection::sessionBus();
  const auto freedesktopConnected = sessionBus.connect(
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
      QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("ActiveChanged"), this,
      SLOT(handleScreenSaverActiveChanged(bool))
  );
  if (freedesktopConnected)
    qInfo("watching org.freedesktop.ScreenSaver lock state");
  else
    qWarning("org.freedesktop.ScreenSaver lock signal is not available");

  const auto gnomeConnected = sessionBus.connect(
      QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("/org/gnome/ScreenSaver"),
      QStringLiteral("org.gnome.ScreenSaver"), QStringLiteral("ActiveChanged"), this,
      SLOT(handleScreenSaverActiveChanged(bool))
  );
  if (gnomeConnected)
    qInfo("watching org.gnome.ScreenSaver lock state");
  else
    qDebug("org.gnome.ScreenSaver lock signal is not available");

  auto systemBus = QDBusConnection::systemBus();
  QDBusInterface loginManager(
      QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
      QStringLiteral("org.freedesktop.login1.Manager"), systemBus
  );
  const auto sessionId = qEnvironmentVariable("XDG_SESSION_ID", QStringLiteral("auto"));
  const auto sessionReply = loginManager.call(QStringLiteral("GetSession"), sessionId);
  if (sessionReply.type() != QDBusMessage::ErrorMessage) {
    const auto sessionPath = qvariant_cast<QDBusObjectPath>(sessionReply.arguments().value(0)).path();
    const auto lockFailed =
        sessionPath.isEmpty() ||
        !systemBus.connect(
            QStringLiteral("org.freedesktop.login1"), sessionPath,
            QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Lock"), this,
            SLOT(handleSessionLocked())
        );
    const auto unlockFailed =
        sessionPath.isEmpty() ||
        !systemBus.connect(
            QStringLiteral("org.freedesktop.login1"), sessionPath,
            QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Unlock"), this,
            SLOT(handleSessionUnlocked())
        );
    const auto propertiesFailed =
        sessionPath.isEmpty() ||
        !systemBus.connect(
            QStringLiteral("org.freedesktop.login1"), sessionPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), this,
            SLOT(handleSessionPropertiesChanged(QString,QVariantMap,QStringList))
        );
    if (!lockFailed && !unlockFailed && !propertiesFailed)
      qInfo("watching systemd-logind session lock state");
    else
      qWarning("failed to watch one or more systemd-logind lock signals");
  } else {
    qDebug("systemd-logind session lock signal is not available: %s", qPrintable(sessionReply.errorMessage()));
  }
}

void XDGPowerManager::handleScreenSaverActiveChanged(bool active)
{
  updateLockState(active);
}

void XDGPowerManager::handleSessionLocked()
{
  updateLockState(true);
}

void XDGPowerManager::handleSessionUnlocked()
{
  updateLockState(false);
}

void XDGPowerManager::handleSessionPropertiesChanged(
    const QString &interface, const QVariantMap &properties, const QStringList &
)
{
  if (interface != QStringLiteral("org.freedesktop.login1.Session"))
    return;

  if (const auto lockedHint = properties.constFind(QStringLiteral("LockedHint")); lockedHint != properties.cend())
    updateLockState(lockedHint->toBool());
}

void XDGPowerManager::updateLockState(bool locked)
{
  if (m_screenLocked == locked)
    return;

  m_screenLocked = locked;
  Q_EMIT screenLockChanged(locked);
  if (m_events && m_eventTarget) {
    const auto type = locked ? EventTypes::PrimaryScreenSaverActivated : EventTypes::PrimaryScreenSaverDeactivated;
    m_events->addEvent(Event(type, m_eventTarget));
  }
}

XDGPowerManager::~XDGPowerManager()
{
  enableSleep();
}

void XDGPowerManager::disableSleep() const
{
  if (!sleepInhibitCall(true, XDGPowerManager::InhibitScreenServices::kScreenSaver) &&
      !sleepInhibitCall(true, XDGPowerManager::InhibitScreenServices::kSessionManager)) {
    qWarning("failed to prevent system from going to sleep");
  }
}

void XDGPowerManager::enableSleep() const
{
  if (!sleepInhibitCall(false, XDGPowerManager::InhibitScreenServices::kScreenSaver) &&
      !sleepInhibitCall(false, XDGPowerManager::InhibitScreenServices::kSessionManager)) {
    qWarning("failed to enable system idle sleep");
  }
}

bool XDGPowerManager::inhibitScreenCall(InhibitScreenServices serviceID, bool state, std::string &error)
{
  error = "";
  static const std::array<QString, 2> services = {"org.freedesktop.ScreenSaver", "org.gnome.SessionManager"};
  static const std::array<QString, 2> paths = {"/org/freedesktop/ScreenSaver", "/org/gnome/SessionManager"};
  static std::array<uint, 2> cookies;

  auto serviceNum = static_cast<uint8_t>(serviceID);

  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    error = "bus failed to connect";
    return false;
  }

  QDBusInterface screenSaverInterface(services[serviceNum], paths[serviceNum], services[serviceNum], bus);

  if (!screenSaverInterface.isValid()) {
    error = "screen saver interface failed to initialize";
    return false;
  }

  QDBusReply<uint> reply;
  if (state) {
    if (cookies[serviceNum]) {
      error = "cookies are not empty";
      return false;
    }

    QString msg = "Sleep is manually prevented by the %1 preferences";
    reply = screenSaverInterface.call("Inhibit", kAppName, msg.arg(kAppName));
    if (reply.isValid())
      cookies[serviceNum] = reply.value();
  } else {
    if (!cookies[serviceNum]) {
      // Nothing was ever inhibited for this service
      return true;
    }
    reply = screenSaverInterface.call("UnInhibit", cookies[serviceNum]);
    cookies[serviceNum] = 0;
  }

  if (!reply.isValid()) {
    QDBusError qerror = reply.error();
    error = qerror.name().toStdString() + " : " + qerror.message().toStdString();
    return false;
  }

  return true;
}
