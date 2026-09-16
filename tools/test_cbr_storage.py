#!/usr/bin/env python3
"""Run after applying the workflow patches. Requires g++, Boost serialization/iostreams.
Use --boost-root <unpacked-prefix>/usr for dependencies outside the system paths.
Only disposable test copies receive the two Linux portability adjustments; the
production class layouts, version tags, and serialization functions are unchanged.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--boost-root', type=Path)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]

def old_header(name):
    # These headers are unchanged by all patches preceding SteamID persistence.
    return subprocess.check_output(['git', 'show', f'HEAD:src/CBR/{name}'], cwd=repo, text=True)

with tempfile.TemporaryDirectory(prefix='cbr-storage-') as folder:
    work = Path(folder)
    fixtures = work / 'fixtures'
    fixtures.mkdir()
    for version in (0, 3, 4):
        headers = work / str(version)
        headers.mkdir()
        for source in (repo / 'src/CBR').glob('*.h'):
            shutil.copyfile(source, headers / source.name)
        if version < 4:
            for name in ('CbrData.h', 'AnnotatedReplay.h'):
                text = old_header(name)
                if name == 'CbrData.h' and version == 0:
                    text = text.replace('BOOST_CLASS_VERSION(CbrData, 3)', 'BOOST_CLASS_VERSION(CbrData, 0)')
                (headers / name).write_text(text)
            old = old_header('CbrUtils.h')
            metadata = old[old.index('struct FileMetadata {'):old.index('template <typename T')]
            (headers / 'CbrFileMetadata.h').write_text('#pragma once\n' + metadata)
        binary = work / (f'test-v{version}' + ('.exe' if os.name == 'nt' else ''))
        if os.name == 'nt':
            if not args.boost_root:
                parser.error('Windows requires --boost-root pointing to the vcpkg x86-windows-static prefix')
            command = ['cl', '/nologo', '/std:c++17', '/EHsc', '/MT', '/O1',
                       '/I' + str(headers), '/I' + str(args.boost_root / 'include')]
            if version < 4:
                command += [f'/DCBR_LEGACY={version}']
            command += [str(repo / 'tools/cbr_storage_test.cpp'), '/Fe:' + str(binary),
                        '/Fo:' + str(headers / 'test.obj'), '/link',
                        '/LIBPATH:' + str(args.boost_root / 'lib'), 'zlib.lib']
        else:
            # MSVC accepts this legacy non-const-reference forwarding; GCC does not.
            comparison = headers / 'ComparisonFunction.h'
            comparison.write_text(comparison.read_text().replace('std::forward<Args>(args)...', 'args...'))
            (headers / 'cbrData.h').symlink_to('CbrData.h')
            command = ['g++', '-std=c++17', '-O1', '-fpermissive', '-w', '-I', str(headers)]
            if version < 4:
                command += [f'-DCBR_LEGACY={version}']
            if args.boost_root:
                lib = args.boost_root / 'lib/x86_64-linux-gnu'
                command += ['-I', str(args.boost_root / 'include'), '-L', str(lib), '-Wl,-rpath,' + str(lib)]
                libraries = ['-l:libboost_serialization.so.1.83.0', '-l:libboost_iostreams.so.1.83.0']
            else:
                libraries = ['-lboost_serialization', '-lboost_iostreams']
            command += [str(repo / 'tools/cbr_storage_test.cpp'), '-o', str(binary), *libraries, '-pthread']
        subprocess.run(command, check=True)
        subprocess.run([str(binary), str(fixtures)], check=True)
