// Copyright 2019 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "gtest/gtest.h"

extern "C" {
#include "cupsd.h"
int cupsd_main(int argc, char* argv[]);
}

// Note: This program requires that cups is the current working directory
// and not cups/scheduler. Invoke it via ./scheduler/googletests from bash.
int main(int argc, char* argv[]) {
  testing::InitGoogleTest(&argc, argv);
  base::ScopedTempDir ppd;  // ppd files directory
  EXPECT_TRUE(ppd.Set(base::FilePath("conf/ppd")));
  {
    char* args[] = {strdup("googletests"), strdup("-tc"),
                    strdup("conf/cupsd.conf")};
    EXPECT_EQ(0, cupsd_main(sizeof(args) / sizeof(args[0]), args));
    for (char* arg : args)
      free(arg);
  }
  // Do not use /var/spool/cups/tmp or whatever TempDir in cups-files.conf
  // is set to as the temp dir.
  EXPECT_TRUE(base::Environment::Create()->UnSetVar("TMPDIR"));
  return RUN_ALL_TESTS();
}
