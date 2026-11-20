/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/CommandRunner.h"

#include "base/Log.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>

#ifdef Q_OS_WIN
#include "arch/win32/ArchMiscWindows.h"
#include "platform/MSWindowsProcess.h"
#endif

namespace deskflow {
namespace {

QString quotedPath(const QString &file)
{
  return QStringLiteral("\"%1\"").arg(QDir::toNativeSeparators(QFileInfo(file).absoluteFilePath()));
}

} // namespace

bool runDetachedCommand(const QString &commandLine)
{
#ifdef Q_OS_WIN
  using deskflow::platform::MSWindowsProcess;
  if (ArchMiscWindows::isProcessElevated()) {
    LOG_DEBUG("current process is elevated, starting detached process as session user");
    return MSWindowsProcess::startDetachedAsSessionUser(commandLine.toStdWString());
  }
#endif

  auto args = QProcess::splitCommand(commandLine);
  if (args.isEmpty())
    return false;

  const auto program = args.takeFirst();
  return QProcess::startDetached(program, args);
}

QString scriptCommand(const QString &scriptFile)
{
  const auto path = QFileInfo(scriptFile).absoluteFilePath();
  const auto suffix = QFileInfo(path).suffix().toLower();
  const auto quoted = quotedPath(path);

#ifdef Q_OS_WIN
  if (suffix == QStringLiteral("bat") || suffix == QStringLiteral("cmd"))
    return QStringLiteral("cmd.exe /c %1").arg(quoted);
  if (suffix == QStringLiteral("ps1"))
    return QStringLiteral("powershell.exe -NoProfile -File %1").arg(quoted);
  if (suffix == QStringLiteral("py"))
    return QStringLiteral("python.exe %1").arg(quoted);
#else
  if (suffix == QStringLiteral("sh") || suffix == QStringLiteral("command"))
    return QStringLiteral("/bin/sh %1").arg(quoted);
  if (suffix == QStringLiteral("bash"))
    return QStringLiteral("/bin/bash %1").arg(quoted);
  if (suffix == QStringLiteral("zsh"))
    return QStringLiteral("/bin/zsh %1").arg(quoted);
  if (suffix == QStringLiteral("py"))
    return QStringLiteral("python3 %1").arg(quoted);
#endif

  return quoted;
}

} // namespace deskflow
