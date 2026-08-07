/* Copyright (c) 2026 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "base/notreached.h"
#include "base/win/windows_version.h"
#include "chrome/install_static/install_util.h"
#include "chrome/installer/setup/brand_behaviors.h"
#include "chrome/installer/util/google_update_settings.h"
#include "chrome/installer/util/install_util.h"

namespace installer {

// AI Automation Browser has no uninstall-survey infrastructure of its own
// (unlike brave_behaviors.cc, which posts to brave.com/uninstall-survey), so
// this is a no-op rather than sending uninstall telemetry to a URL that
// isn't ours.
void UpdateInstallStatus(installer::ArchiveType archive_type,
                         installer::InstallStatus install_status) {
  GoogleUpdateSettings::UpdateInstallStatus(
      install_static::IsSystemInstall(), archive_type,
      InstallUtil::GetInstallReturnCode(install_status));
}

void DoPostUninstallOperations(const base::Version& version,
                               const base::FilePath& local_data_path,
                               const std::wstring& distribution_data) {}

class GoogleUpdateSettings_UNUSED {
 public:
  static void UpdateInstallStatus() { NOTREACHED(); }
};

}  // namespace installer

#define GoogleUpdateSettings GoogleUpdateSettings_UNUSED
#define DoPostUninstallOperations DoPostUninstallOperations_UNUSED
#include <chrome/installer/setup/google_chrome_behaviors.cc>
#undef DoPostUninstallOperations
#undef GoogleUpdateSettings
