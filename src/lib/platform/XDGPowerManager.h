/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <string>

class IEventQueue;

class XDGPowerManager : public QObject
{
  Q_OBJECT

public:
  explicit XDGPowerManager(QObject *parent = nullptr);
  ~XDGPowerManager();

  /**
   * @brief Prevent the system from sleep
   */
  void disableSleep() const;

  /**
   * @brief Enables automatical sleep
   */
  void enableSleep() const;

  /**
   * @brief Forward desktop screen-lock signals to the event queue.
   */
  void watchScreenLock(IEventQueue *events = nullptr, void *eventTarget = nullptr);

  XDGPowerManager(const XDGPowerManager &) = delete;
  XDGPowerManager &operator=(const XDGPowerManager &) = delete;

  enum class InhibitScreenServices
  {
    kScreenSaver,
    kSessionManager
  };
  static bool inhibitScreenCall(InhibitScreenServices serviceID, bool state, std::string &error);

Q_SIGNALS:
  void screenLockChanged(bool locked);

private Q_SLOTS:
  void handleScreenSaverActiveChanged(bool active);
  void handleSessionLocked();
  void handleSessionUnlocked();
  void handleSessionPropertiesChanged(const QString &interface, const QVariantMap &properties, const QStringList &);

private:
  void updateLockState(bool locked);
  IEventQueue *m_events = nullptr;
  void *m_eventTarget = nullptr;
  bool m_screenLocked = false;
};
