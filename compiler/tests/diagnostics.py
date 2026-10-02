import subprocess
import sys
import tempfile
from pathlib import Path
cases=[
('recursive','volatile unsigned n=4; __attribute__((noinline,optnone)) unsigned f(unsigned x){return x ? f(x-1)+x : 0;} extern "C" unsigned kernel(){return f(n);}', 'recursive'),
('double','volatile double x=2; extern "C" unsigned kernel(){return unsigned(x*3);}', 'unsupported'),
('unresolved','extern unsigned missing(unsigned); extern "C" unsigned kernel(){return missing(7);}', 'unresolved'),
('constructor','extern unsigned init(); unsigned value=init(); extern \"C\" unsigned kernel(){return value;}', 'constructors'),
('task_dynamic','#include <tasks/tasks.h>\nvoid task(){} extreme::tasks::Entry volatile entry=task; extern "C" unsigned kernel(){return extreme::tasks::issue(0,entry);}', 'constant task entry'),
('task_signature','#include <tasks/tasks.h>\nunsigned task(){return 7;} extern "C" unsigned kernel(){return extreme::tasks::issue(0,reinterpret_cast<extreme::tasks::Entry>(task));}', 'task entry must be void'),
('task_ordinary','#include <tasks/tasks.h>\nvolatile unsigned x; __attribute__((noinline,optnone)) void task(){x=x+1;} extern "C" unsigned kernel(){task();return extreme::tasks::issue(0,task);}', 'ordinary functions'),
('task_unknown','extern "C" unsigned __extreme_task_unknown(unsigned); extern "C" unsigned kernel(){return __extreme_task_unknown(0);}', 'unknown task intrinsic'),
('task_bad_abi','extern "C" unsigned __extreme_task_finish(unsigned); extern "C" unsigned kernel(){return __extreme_task_finish(0);}', 'invalid task intrinsic signature'),
('exceptions','extern "C" unsigned kernel(){throw 42;}', 'exceptions disabled'),
]
with tempfile.TemporaryDirectory(prefix='extreme-negative-') as temp:
    for name,source,diagnostic in cases:
        src=Path(temp)/(name+'.cpp');src.write_text(source)
        result=subprocess.run([sys.executable,sys.argv[1],str(src),'-o',str(Path(temp)/(name+'.ecx'))],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        if result.returncode==0 or diagnostic not in result.stdout:
            raise RuntimeError(f'{name}: expected rejection containing {diagnostic}:\n{result.stdout}')
        print(name,'correctly rejected')
