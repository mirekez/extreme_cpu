#!/usr/bin/env python3
"""Build the same C++ testbench against native RTL and generated SystemVerilog."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--flow',choices=['cpp','verilator','all'],default='all')
p.add_argument('--bits',type=int,default=128)
p.add_argument('--depth',type=int,default=16)
p.add_argument('--cores',type=int,default=2)
p.add_argument('--banks',type=int,default=2)
p.add_argument('--test',choices=['all','System','LoadFifo','StoreFifo','Memory','MemoryMux','TasksControl','Tasks','Memcpy2','Compiled'],default='all')
p.add_argument('--image',type=Path)
p.add_argument('--image2',type=Path)
p.add_argument('--expected',default='0')
p.add_argument('--limit',default='5000000')
p.add_argument('--mode',default='none')
p.add_argument('--build-dir',type=Path)
p.add_argument('--cxx')
p.add_argument('--cpphdl',type=Path)
a=p.parse_args()
cpphdl=a.cpphdl or Path(os.environ.get('CPPHDL_HOME',str(Path.home()/'cpphdl')))
build=(a.build_dir or ROOT/'build')/f'b{a.bits}-d{a.depth}-c{a.cores}-m{a.banks}'
build.mkdir(parents=True,exist_ok=True)
def run(cmd,log=None):
    if log:
        with log.open('w') as out:
            r=subprocess.run([str(x) for x in cmd],cwd=ROOT,stdout=out,stderr=subprocess.STDOUT)
        if r.returncode:
            print(log.read_text()[-16000:],file=sys.stderr)
            raise RuntimeError(f'command failed; full log: {log}')
    else:
        subprocess.run([str(x) for x in cmd],cwd=ROOT,check=True)
cxx=a.cxx or os.environ.get('CXX','g++')
flags=['-std=c++20','-O2','-fno-strict-aliasing',f'-I{cpphdl}/include',f'-DEC_BITS={a.bits}',f'-DEC_DEPTH={a.depth}',f'-DEC_CORES={a.cores}',f'-DEC_BANKS={a.banks}']
if a.flow in ['cpp','all']:
    run([cxx,'-std=c++20',ROOT/'arch/tests/instructions.cpp','-o',build/'instructions'])
    run([build/'instructions'])
for name in (['LoadFifo','StoreFifo','Memory','MemoryMux','TasksControl','System','Tasks'] if a.test=='all' else [a.test]):
    src=ROOT/('tests/memcpy2.cpp' if name=='Memcpy2' else 'tests/tasks.cpp' if name=='Tasks' else 'tests/compiled.cpp' if name=='Compiled' else 'tests/memcpy.cpp' if name=='System' else f'rtl/tests/{name}.cpp')
    top='System' if name in ['Compiled','Tasks','Memcpy2'] else name
    runtime_args=[str(a.image),a.expected,a.limit,a.mode] if name=='Compiled' else []
    if name=='Memcpy2':
        if a.image is None or a.image2 is None:p.error('--image and --image2 are required for Memcpy2')
        runtime_args=[str(a.image),str(a.image2)]
    if name=='Compiled' and a.image is None: p.error('--image is required for Compiled')
    folder=build/name;folder.mkdir(exist_ok=True)
    if a.flow in ['cpp','all']:
        print(f'[{name}] native C++',flush=True)
        run([cxx,*flags,src,'-o',folder/'native'],folder/'native-build.log')
        run([folder/'native',*runtime_args])
    if a.flow in ['verilator','all']:
        print(f'[{name}] C++HDL -> SystemVerilog -> Verilator',flush=True)
        gen=folder/'generated'
        run([cpphdl/'build/cpphdl','--generated-dir',gen,src,'--',*flags],folder/'convert.log')
        sv=sorted(gen.glob('*_pkg.sv'))+sorted(x for x in gen.glob('*.sv') if not x.name.endswith('_pkg.sv'))
        verilator=os.environ.get('VERILATOR',shutil.which('verilator') or str(cpphdl/'.conda/bin/verilator'))
        try:
            run([verilator,'--cc','--exe','--build','-j','2','-Wno-fatal','--top-module',top,'--Mdir',folder/'obj',
                 '-MAKEFLAGS',f'CXX={cxx} LINK={cxx}', '-CFLAGS',' '.join(flags+['-DVERILATOR']),*sv,src,'-o','regression'],folder/'verilator-build.log')
        finally:
            # Recent Verilator versions create large disposable precompiled headers.
            # Keep generated RTL, object files, and executables without filling disk.
            for pch in (folder/'obj').glob('*__pch.h.*.gch'):
                pch.unlink()
        run([folder/'obj/regression',*runtime_args])
print('All requested regressions passed.',flush=True)
