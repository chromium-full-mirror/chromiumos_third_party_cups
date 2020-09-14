// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gtest/gtest.h"

extern "C" {
#include "cupsd.h"
}

// Note: This program requires that cups is the current working directory
// and not cups/scheduler. Invoke it via ./scheduler/googletests from bash.
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  base::ScopedTempDir ppd;  // ppd files directory
  EXPECT_TRUE(ppd.Set(base::FilePath("conf/ppd")));
  cupsdSetString(&ConfigurationFile, "conf/cupsd.conf");
  cupsdSetString(&CupsFilesFile, "conf/cups-files.conf");
  EXPECT_TRUE(ConfigurationFile);
  EXPECT_TRUE(CupsFilesFile);
  EXPECT_TRUE(cupsdReadConfiguration());
  // Do not use /var/spool/cups/tmp or whatever TempDir in cups-files.conf
  // is set to as the temp dir.
  EXPECT_TRUE(base::Environment::Create()->UnSetVar("TMPDIR"));
  return RUN_ALL_TESTS();
}
