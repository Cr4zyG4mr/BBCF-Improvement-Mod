#!/usr/bin/env python3
"""Test the actual converter after applying the workflow patches."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--boost-root', type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
sources = [repo / 'tools/cbr_conversion_test.cpp'] + [repo / 'src/CBR' / name for name in (
    'CbrReplayFile.cpp', 'AnnotatedReplay.cpp', 'Metadata.cpp', 'CbrCase.cpp', 'Helper.cpp')]
with tempfile.TemporaryDirectory(prefix='cbr-conversion-') as folder:
    binary = Path(folder) / ('test.exe' if os.name == 'nt' else 'test')
    if os.name == 'nt':
        command = ['cl', '/nologo', '/std:c++17', '/EHsc', '/MT', '/Od',
                   '/I' + str(repo / 'src/CBR'), '/I' + str(args.boost_root / 'include'),
                   *map(str, sources), '/Fe:' + str(binary), '/link',
                   '/LIBPATH:' + str(args.boost_root / 'lib'),
                   *map(str, sorted((args.boost_root / 'lib').glob('*.lib')))]
    else:
        # MSVC permits this legacy temporary-to-nonconst-reference binding.
        # Give only that temporary a name in the disposable GCC test copy.
        portable = Path(folder) / 'CbrReplayFile.cpp'
        portable.write_text(sources[1].read_text().replace(
            'dollResolve = MakeInputArray(curStateDoll, FetchNirvanaCommandActions(), "none");',
            'auto nirvanaCommands = FetchNirvanaCommandActions();\n'
            'dollResolve = MakeInputArray(curStateDoll, nirvanaCommands, "none");'))
        sources[1] = portable
        lib = args.boost_root / 'lib/x86_64-linux-gnu'
        command = ['g++', '-std=c++17', '-fpermissive', '-w', '-O0', '-g',
                   '-fsanitize=address,undefined', '-D_GLIBCXX_ASSERTIONS',
                   '-I', str(repo / 'src/CBR'), '-I', str(args.boost_root / 'include'),
                   *map(str, sources), '-o', str(binary), '-L', str(lib),
                   '-Wl,-rpath,' + str(lib), '-l:libboost_serialization.so.1.83.0',
                   '-l:libboost_iostreams.so.1.83.0', '-l:libboost_filesystem.so.1.83.0']
    subprocess.run(command, cwd=folder, check=True)
    subprocess.run([str(binary)], cwd=folder, check=True)
