#!/usr/bin/env python3
"""Synthesis smoke check of generated SystemVerilog with Yosys + slang."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser()
p.add_argument('--bits',type=int,default=64)
p.add_argument('--depth',type=int,default=2)
p.add_argument('--cores',type=int,default=1)
p.add_argument('--banks',type=int,default=1)
p.add_argument('--words',type=int,default=32)
p.add_argument('--yosys')
a=p.parse_args()
cpphdl=Path(os.environ.get('CPPHDL_HOME',str(Path.home()/'cpphdl')))
local=cpphdl/'build/tools/oss-cad-suite/bin/yosys'
yosys=a.yosys or os.environ.get('YOSYS') or (str(local) if local.exists() else shutil.which('yosys'))
if not yosys: p.error('Yosys with slang is required')
work=ROOT/'build'/f'synthesis-b{a.bits}-d{a.depth}-c{a.cores}-m{a.banks}-w{a.words}'
work.mkdir(parents=True,exist_ok=True)
gen=work/'generated'
flags=[f'-I{cpphdl}/include',f'-DEC_BITS={a.bits}',f'-DEC_DEPTH={a.depth}',f'-DEC_CORES={a.cores}',f'-DEC_BANKS={a.banks}',f'-DEC_BANK_WORDS={a.words}']
subprocess.run([str(cpphdl/'build/cpphdl'),'--generated-dir',str(gen),str(ROOT/'tests/memcpy.cpp'),'--',*flags],check=True)
sv=sorted(gen.glob('*_pkg.sv'))+sorted(x for x in gen.glob('*.sv') if not x.name.endswith('_pkg.sv'))
script=work/'synth.ys'
script.write_text('read_slang --top System '+ ' '.join(str(f.relative_to(work)) for f in sv)+'\n'
                  'hierarchy -check -top System\nsynth -top System -noabc\ncheck -assert\n'
                  f'write_verilog -noattr "{work}/gates.v"\nstat\n')
with (work/'synthesis.log').open('w') as out:
    result=subprocess.run([yosys,'-m','slang','-s',str(script)],stdout=out,stderr=subprocess.STDOUT,cwd=work)
if result.returncode:
    print((work/'synthesis.log').read_text()[-12000:]);raise SystemExit(result.returncode)
print('Synthesis and check -assert passed:',work/'gates.v')
