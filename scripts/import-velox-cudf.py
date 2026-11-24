#!/usr/bin/env python3
# Copyright (c) ByteDance Ltd. and/or its affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Imports Velox's cuDF integration (velox/experimental/cudf) into bolt/cudf.

The import is mechanical: include paths, namespaces, macros and exception
names are renamed from Velox to Bolt. Bolt-specific adaptations are kept as
separate commits on top of the import, so a newer Velox commit is picked up by
re-running this script and rebasing those commits.

Usage: scripts/import-velox-cudf.py <velox checkout> [--ref <commit>]
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

BOLT_ROOT = pathlib.Path(__file__).resolve().parent.parent
DEST = BOLT_ROOT / "bolt" / "cudf"
SOURCE = "velox/experimental/cudf"

# Paths under velox/experimental/cudf to import. Iceberg, the UCX exchange,
# the device-side exception shadows and the unit tests depend on Velox
# components Bolt does not have.
INCLUDE = [
    "CudfConfig.h",
    "CudfDefaultStreamOverload.h",
    "CudfNoDefaults.h",
    "connectors/hive/",
    "exec/",
    "expression/",
    "vector/",
    "tests/utils/",
    "benchmarks/CudfTpchBenchmark.cpp",
    "benchmarks/CudfTpchBenchmark.h",
]
EXCLUDE = [
    "connectors/hive/iceberg/",
    "CMakeLists.txt",
    "tests/utils/CudfTpcdsQueryBuilder.cpp",
    "tests/utils/CudfTpcdsQueryBuilder.h",
]
SOURCE_SUFFIXES = {".h", ".cpp", ".cu", ".cuh", ".hpp", ".inl"}

# Bolt keeps its own CMake files and these hand-written sources.
KEEP = ["CMakeLists.txt", "CudfEnable.h"]
# Bolt's stand-ins for Velox headers it does not have.
KEEP_DIRS = ["compat"]

REWRITES = [
    (re.compile(r'#include "velox/experimental/cudf/'), '#include "bolt/cudf/'),
    (
        re.compile(
            r'#include "velox/(?:type|connectors)/(TypeUtil|ConnectorRegistry)\.h"'
        ),
        r'#include "bolt/cudf/compat/\1.h"',
    ),
    (
        re.compile(r'#include "velox/common/caching/FileProperties\.h"'),
        '#include "bolt/connectors/hive/FileProperties.h"',
    ),
    (re.compile(r"#include <experimental/cudf/([^>]+)>"), r'#include "bolt/cudf/\1"'),
    (re.compile(r'#include "velox/'), '#include "bolt/'),
    (re.compile(r"\bfacebook::velox\b"), "bytedance::bolt"),
    (re.compile(r"\bvelox::"), "bolt::"),
    (re.compile(r"\bVELOX_"), "BOLT_"),
    (re.compile(r"\bVelox(Exception|UserError|RuntimeError)\b"), r"Bolt\1"),
]


def export_tree(velox: pathlib.Path, ref: str, out: pathlib.Path) -> str:
    sha = subprocess.check_output(
        ["git", "-C", str(velox), "rev-parse", ref], text=True
    ).strip()
    archive = subprocess.run(
        ["git", "-C", str(velox), "archive", sha, SOURCE],
        check=True,
        capture_output=True,
    ).stdout
    tar_path = out / "cudf.tar"
    tar_path.write_bytes(archive)
    with tarfile.open(tar_path) as tar:
        tar.extractall(out, filter="data")
    return sha


def selected(rel: str) -> bool:
    if any(rel == e or (e.endswith("/") and rel.startswith(e)) for e in EXCLUDE):
        return False
    if rel.endswith("/CMakeLists.txt"):
        return False
    return any(rel == i or (i.endswith("/") and rel.startswith(i)) for i in INCLUDE)


def rewrite(text: str) -> str:
    for pattern, replacement in REWRITES:
        text = pattern.sub(replacement, text)
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("velox", type=pathlib.Path, help="Velox git checkout")
    parser.add_argument("--ref", default="HEAD", help="Velox commit to import")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = pathlib.Path(tmp)
        sha = export_tree(args.velox, args.ref, tmp_path)
        src = tmp_path / SOURCE

        for path in sorted(DEST.rglob("*")):
            rel = path.relative_to(DEST).parts
            if rel[0] in KEEP_DIRS:
                continue
            if path.is_file() and path.name not in KEEP:
                path.unlink()
        for path in sorted(DEST.rglob("*"), reverse=True):
            if path.is_dir() and not any(path.iterdir()):
                path.rmdir()

        count = 0
        for path in sorted(src.rglob("*")):
            rel = path.relative_to(src).as_posix()
            if not path.is_file() or not selected(rel):
                continue
            target = DEST / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            if path.suffix in SOURCE_SUFFIXES:
                target.write_text(rewrite(path.read_text()))
            else:
                shutil.copy2(path, target)
            count += 1

    (DEST / "VELOX_COMMIT").write_text(sha + "\n")
    print(f"Imported {count} files from Velox {sha} into {DEST.relative_to(BOLT_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
