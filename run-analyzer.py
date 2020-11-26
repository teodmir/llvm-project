#!/usr/bin/env python
import argparse
import shutil
import os
import subprocess
import sys

script_dir = os.path.join(os.path.dirname(os.path.realpath(__file__)))
def_bin_unix = os.path.join(script_dir,
                            "build",
                            "bin",
                            "clang-tidy")
def_bin_windows = os.path.join(script_dir,
                               "debug",
                               "bin",
                               "clang-tidy")
parser = argparse.ArgumentParser(
    description='Run clang-tidy with assignment-specific options.'
)
parser.add_argument(
    '-t',
    help='Location of Clang-tidy executable',
    default=shutil.which(def_bin_unix) or shutil.which(def_bin_windows)
)
parser.add_argument(
    '-d',
    help='Location JSON file containing assignment declarations'
)
parser.add_argument(
    '-c',
    action='store_true',
    help='Use local compilation database, if available'
)
parser.add_argument('file',
                    help='File to analyze')

args = parser.parse_args()

if not args.t:
    print("No Clang-tidy executable found", file=sys.stderr)
    exit(1)

if not args.file:
    print("No file provided for analysis", file=sys.stderr)
    exit(1)

tidy_args = [args.t]
# Not sure if this glob actually works; default checkers seem to be
# included anyway..
tidy_args.append("-checks=misc-assignment-*,misc-unused-parameters,misc-no-recursion")
if (args.d):
    tidy_args.append(
        "-config={{CheckOptions: "
        "[{{key: 'misc-assignment-decl-exist.DeclFile', value: '{}'}}]}}"
        .format(args.d)
    )

tidy_args.append(args.file)
# Tells Clang-tidy no compilation database should be used.
if not args.c:
    tidy_args.append("--")

subprocess.call(tidy_args)
