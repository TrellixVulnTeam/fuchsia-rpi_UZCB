#!/usr/bin/env python

# Copyright 2020 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""
  Prints "true" if the input |file_name| exists.
"""

import argparse
import os

parser = argparse.ArgumentParser()
parser.add_argument(
    "-file_name", type=str, help="File name for which to check existence for.")
args = parser.parse_args()
if os.path.exists(args.file_name):
    print("true")
