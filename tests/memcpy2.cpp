#ifdef EXTREME_KERNEL
#include <tasks/tasks.h>
// Each independent copy has a source and destination single-port bank.
static constexpr unsigned bytes=EC_BUS_BYTES, offset=1024*bytes;
__attribute__((noinline)) void copy_a(){
    __builtin_memcpy(reinterpret_cast<void*>(EC_BANK_WORDS*bytes+offset),
                     reinterpret_cast<const void*>(offset),EC_COPY_WORDS*bytes);
    extreme::tasks::finish(0xa0);
}
__attribute__((noinline)) void copy_b(){
    __builtin_memcpy(reinterpret_cast<void*>(3*EC_BANK_WORDS*bytes+offset),
                     reinterpret_cast<const void*>(2*EC_BANK_WORDS*bytes+offset),EC_COPY_WORDS*bytes);
    extreme::tasks::finish(0xb0);
}
extern "C" unsigned kernel(){
    auto first=extreme::tasks::issue(0,copy_a);
    auto second=extreme::tasks::issue(1,copy_b);
    return unsigned(first)|unsigned(second);
}
#else
#include "../rtl/System.h"
System top;
#ifndef SYNTHESIS
#define TEST_TOP System
#include "SystemHarness.h"
#include <fstream>
#include <array>
static_assert(EC_CORES==3 && EC_BANKS==4 && EC_DEPTH>=8);
struct Memcpy2Test : Harness {
    static constexpr unsigned bytes=EC_BITS/8, offset=1024*bytes;
    struct Timing {unsigned total, stream, paired;};
    Word access(unsigned addr,bool write,Word data=0){
        host_address_in=addr;host_write_in=write;host_data_in=data;host_request_in=true;
        settle();require(OUT(host_ready_out),"host request rejected");tick();host_request_in=false;settle();
        require(OUT(host_response_out),"host response missing");Word value=read_word(OUT(host_result_out));tick();return value;
    }
    Timing run(const std::string& image,unsigned count,bool stalls=false){
        std::ifstream file(image,std::ios::binary);require(bool(file),"cannot open memcpy2 image");
        auto field=[&](){uint32_t v=0;for(unsigned i=0;i<4;++i){int c=file.get();require(c!=EOF,"truncated image");v|=uint32_t(uint8_t(c))<<(8*i);}return v;};
        require(field()==0x58434345 && field()==1,"image format");
        require(field()==EC_BITS && field()==EC_REGS && field()==EC_BANKS && field()==EC_BANK_WORDS,"image configuration");
        unsigned entry=field(),idle=field(),result=field(),size=field();
        require(size==EC_BANKS*EC_BANK_WORDS*bytes,"image payload size");
        run_in=false;host_mode_in=true;host_request_in=false;
        for(unsigned b=0;b<EC_BANKS;++b)memory_enable_in[b]=true;
        tick(true);tick(true);
        for(unsigned addr=0;addr<size;addr+=bytes){Word w=0;for(unsigned j=0;j<bytes;++j){int c=file.get();require(c!=EOF,"truncated payload");w.bits(j*8+7,j*8)=uint8_t(c);}access(addr,true,w);}
        require(file.get()==EOF,"trailing image bytes");
        for(unsigned copy=0;copy<2;++copy){
            unsigned src=2*copy*EC_BANK_WORDS*bytes+offset,dst=src+EC_BANK_WORDS*bytes;
            for(unsigned i=0;i<count;++i){access(src+i*bytes,true,pattern(10000*copy+i+19));access(dst+i*bytes,true,Word(0));}
            access(dst-bytes,true,pattern(0xfeed));access(dst+count*bytes,true,pattern(0xbeef));
        }
        for(unsigned c=0;c<EC_CORES;++c)entry_in[c]=c?idle:entry;
        host_mode_in=false;run_in=true;
        std::array<unsigned,EC_CORES> copied{};std::array<int,2> owner{-1,-1};
        unsigned cycles=0,first=0,last=0,paired=0;
        for(;cycles<100000;++cycles){
            for(unsigned b=0;b<EC_BANKS;++b)memory_enable_in[b]=!stalls || (cycles+3*b)%13>3;
            settle();unsigned transfers=0;
            for(unsigned c=0;c<EC_CORES;++c){
                if(OUT(task_launch_out[c])){unsigned id=OUT(task_launch_id_out[c]);require(id<2 && owner[id]<0,"duplicate/unexpected launch");owner[id]=int(c);}
                if(OUT(copy_word_out[c])){++copied[c];++transfers;if(!first)first=cycles+1;last=cycles+1;}
            }
            if(transfers==2)++paired;
            require(transfers<=2,"more copy streams than tasks");
            tick();settle();bool done=OUT(tasks_idle_out);
            for(unsigned c=0;c<EC_CORES;++c){require(!OUT(fault_out[c]),"memcpy2 CPU fault");done &= bool(OUT(halted_out[c]));}
            if(done){++cycles;break;}
        }
        require(cycles<100000,"memcpy2 timeout");
        require(owner[0]>0 && owner[1]>0 && owner[0]!=owner[1],"CPU0 must issue tasks to two other cores");
        require(copied[0]==0 && copied[owner[0]]==count && copied[owner[1]]==count,"COPY word counts");
        for(unsigned t=0;t<2;++t){task_debug_id_in=t;settle();require(OUT(task_debug_state_out)==4 && OUT(task_debug_result_out)==(t?0xb0:0xa0),"task completion/result");}
        Timing timing{cycles,last-first+1,paired};
        if(!stalls){
            // Includes dispatch, code fetch, scalar setup and completion. The
            // separately checked size delta must cost exactly one clock/word.
            require(cycles>=count && cycles<=count+256,"parallel memcpy end-to-end cycle bound: "+std::to_string(cycles));
            require(timing.stream>=count && timing.stream<=count+96,"parallel streaming cycle bound: "+std::to_string(timing.stream));
            require(paired>=count-96,"insufficient simultaneous COPY throughput: "+std::to_string(paired));
        }
        run_in=false;host_mode_in=true;for(unsigned b=0;b<EC_BANKS;++b)memory_enable_in[b]=true;tick();
        Word returned=access(result&~(bytes-1),false);require(uint32_t(returned>>((result%bytes)*8))==0,"CPU0 task issue status");
        for(unsigned copy=0;copy<2;++copy){
            unsigned src=2*copy*EC_BANK_WORDS*bytes+offset,dst=src+EC_BANK_WORDS*bytes;
            for(unsigned i=0;i<count;++i){Word expected=pattern(10000*copy+i+19);require(access(dst+i*bytes,false)==expected,"destination mismatch");require(access(src+i*bytes,false)==expected,"source modified");}
            require(access(dst-bytes,false)==pattern(0xfeed) && access(dst+count*bytes,false)==pattern(0xbeef),"copy boundary overwrite");
        }
        std::cout<<"memcpy2 PASS words="<<count<<" total="<<cycles<<" stream="<<timing.stream<<" simultaneous="<<paired<<" stalls="<<stalls<<'\n';return timing;
    }
};
int main(int argc,char** argv){try{
    require(argc==3,"usage: memcpy2 image1024.ecx image2048.ecx");Memcpy2Test t;
    auto small=t.run(argv[1],1024),large=t.run(argv[2],2048);
    require(large.total-small.total==1024 && large.stream-small.stream==1024,"doubling both buffers must add exactly 1024 clocks");
    t.run(argv[1],1024,true);
    std::cout<<"memcpy2 bandwidth PASS: two additional 1024-word buffers take exactly 1024 additional clocks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
#endif
