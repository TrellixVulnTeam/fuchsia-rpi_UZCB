// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hwstress.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(HwStress, RunHelp) {
  const char* args[] = {"hwstress", "--help"};
  EXPECT_EQ(0, hwstress::Run(2, args));
}

TEST(HwStress, RunShort) {
  // Run with default args for a short duration.
  const char* args[] = {"hwstress", "-d", "0.1"};
  EXPECT_EQ(0, hwstress::Run(3, args));
}

}  // namespace
}  // namespace hwstress