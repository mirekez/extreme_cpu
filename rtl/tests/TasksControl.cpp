#include "../TasksControl.h"
TasksControl top;
#ifndef SYNTHESIS
#define TEST_TOP TasksControl
#include "../../tests/TestSupport.h"
#include <array>
struct Harness {
    Dut dut;
    bool enable=true, request[EC_CORES]{}, free[EC_CORES]{}, fault[EC_CORES]{};
    u<32> command[EC_CORES]{}, id[EC_CORES]{}, address[EC_CORES]{}, mask[EC_CORES]{}, value[EC_CORES]{}, debug{};
    std::vector<std::pair<unsigned,unsigned>> launches;
    int assigned[32]{};
    Harness() {
#ifndef VERILATOR
        dut.enable_in=_ASSIGN(enable);dut.debug_id_in=_ASSIGN(debug);
        for(unsigned i=0;i<EC_CORES;++i) {
            dut.request_in[i]=_ASSIGN_I(request[i]);dut.free_in[i]=_ASSIGN_I(free[i]);dut.fault_in[i]=_ASSIGN_I(fault[i]);
            dut.command_in[i]=_ASSIGN_I(command[i]);dut.id_in[i]=_ASSIGN_I(id[i]);dut.address_in[i]=_ASSIGN_I(address[i]);
            dut.mask_in[i]=_ASSIGN_I(mask[i]);dut.value_in[i]=_ASSIGN_I(value[i]);
        }
        dut._assign();
#endif
    }
    void settle() {
        ++_system_clock;
#ifdef VERILATOR
        dut.enable_in=enable;dut.debug_id_in=debug;
        for(unsigned c=0;c<EC_CORES;++c) {
            dut.request_in[c]=request[c];dut.free_in[c]=free[c];dut.fault_in[c]=fault[c];
            dut.command_in[c]=command[c];dut.id_in[c]=id[c];dut.address_in[c]=address[c];dut.mask_in[c]=mask[c];dut.value_in[c]=value[c];
        }
        dut.clk=0;dut.eval();
#endif
    }
    void tick(bool reset=false) {
#ifdef VERILATOR
        dut.reset=reset;
#endif
        settle();
#ifdef VERILATOR
        dut.clk=1;dut.eval();
#else
        dut._work(reset);dut._strobe();
#endif
        settle();
        if(!reset) for(unsigned c=0;c<EC_CORES;++c) if(OUT(launch_out[c])) {
            unsigned t=OUT(launch_id_out[c]);require(t<32,"launch ID range");
            require(free[c] && !fault[c],"launch to occupied/faulted core");
            require(OUT(launch_address_out[c])==(t+1)*(EC_BITS/8),"launch address corruption");
            free[c]=false;assigned[t]=int(c);launches.emplace_back(t,c);
        }
    }
    void reset() {
        enable=true;for(unsigned c=0;c<EC_CORES;++c){request[c]=false;free[c]=false;fault[c]=false;}
        tick(true);tick(true);launches.clear();for(auto& c:assigned)c=-1;
    }
    unsigned state(unsigned t) {debug=t;settle();return OUT(debug_state_out);}
    uint32_t result(unsigned t) {debug=t;settle();return OUT(debug_result_out);}
    std::pair<unsigned,uint32_t> cmd(unsigned c,unsigned op,unsigned t=0,uint32_t deps=0,uint32_t data=0,uint32_t addr=0xffffffff) {
        request[c]=true;command[c]=op;id[c]=t;mask[c]=deps;value[c]=data;address[c]=addr==0xffffffff ? (t+1)*(EC_BITS/8) : addr;
        settle();require(OUT(accepted_out[c]),"single request not accepted");tick();request[c]=false;settle();
        require(OUT(response_out[c]),"registered response missing");return {OUT(status_out[c]),OUT(result_out[c])};
    }
    void issue(unsigned t,uint32_t deps=0){require(cmd(0,1,t,deps).first==0,"issue rejected");}
    void finish(unsigned t,uint32_t data,bool abort=false) {
        require(assigned[t]>=0,"finishing undispatched task");unsigned c=assigned[t];
        require(cmd(c,abort?5:4,0,0,data).first==0,"finish rejected");free[c]=true;assigned[t]=-1;
    }
    void untilLaunched(unsigned n) {for(unsigned i=0;i<300 && launches.size()<n;++i)tick();require(launches.size()==n,"dispatch timeout");}
    void basic() {
        reset();require(OUT(idle_out),"reset idle");
        require(cmd(0,3,0).first==3,"empty result must be not ready");
        require(cmd(0,4).first==4,"nonowner completion");
        require(cmd(0,1,32).first==1,"out-of-range ID");
        require(cmd(0,1,0,1).first==1,"self dependency");
        require(cmd(0,1,0,0,0,1).first==1,"misaligned entry");
        require(cmd(0,99).first==1,"unknown command");
        issue(31);require(state(31)==1,"issue must first wait");tick();require(state(31)==2 && launches.empty(),"selection must reserve before launch");
        require(cmd(0,2,31).first==2,"reserved disarm must reject");
        require(cmd(0,1,31).first==2,"reserved rewrite must reject");
        free[0]=true;untilLaunched(1);require(state(31)==3,"launch state");
        require(cmd(0,2,31).first==2,"running disarm must reject");
        require(cmd(0,1,31).first==2,"running rewrite must reject");
        require(cmd(0,3,31).first==3,"running result must be not ready");
        finish(31,0x87654321);require(state(31)==4 && result(31)==0x87654321,"completion payload");
        require(cmd(0,3,31)==std::pair<unsigned,uint32_t>{0,0x87654321},"read completed result");
        issue(30,0x80000000);require(cmd(0,2,31).first==2,"referenced result disarm");
        require(cmd(0,1,31).first==2,"referenced result reuse");untilLaunched(2);finish(30,0);
        require(cmd(0,2,31).first==0 && state(31)==0,"release terminal slot");
        issue(31);untilLaunched(3);finish(31,5,true);require(state(31)==5,"abort retains result");
        issue(29,0x80000000);issue(28,1u<<29);for(unsigned n=0;n<5;++n)tick();
        require(state(29)==6 && state(28)==6 && result(28)==5,"transitive failure propagation");
        require(launches.size()==3,"cancelled task dispatched");
        reset();issue(1,1u<<0);require(cmd(0,2,1).first==0 && state(1)==6,"pending disarm");
        issue(2,1u<<1);tick();require(state(2)==6 && result(2)==0xfffffffd,"disarm propagation");
        reset();issue(0); // cancel exactly when it would otherwise be selected
        require(cmd(0,2,0).first==0 && state(0)==6,"selection/disarm race");
        free[0]=true;for(unsigned n=0;n<5;++n)tick();require(launches.empty(),"disarmed task selected");
        reset();issue(0,2);issue(1,1);free[0]=true;for(unsigned n=0;n<20;++n)tick();
        require(launches.empty() && !OUT(idle_out),"cycle must remain blocked");
        require(cmd(0,2,0).first==0,"cycle cancellation");tick();require(state(1)==6,"cycle cleanup");
        reset();issue(0);free[0]=true;untilLaunched(1);tick();free[0]=true;tick();
        require(state(0)==5 && result(0)==0xfffffffe,"unexpected HALT failure");
        reset();issue(0);free[0]=true;untilLaunched(1);tick();fault[0]=true;tick();
        require(state(0)==5,"core fault must fail owned task");issue(1);for(unsigned n=0;n<8;++n)tick();require(launches.size()==1,"faulted core reused");
        reset();issue(0);free[0]=true;untilLaunched(1);tick();fault[0]=true;
        require(cmd(0,4,0,0,77).first==4 && state(0)==5 && result(0)==0xfffffffe,"fault/completion race must fail");
        reset();issue(0);enable=false;free[0]=true;for(unsigned n=0;n<5;++n)tick();require(state(0)==1,"disabled scheduler changed state");enable=true;untilLaunched(1);
        reset();require(OUT(idle_out) && state(0)==0,"reset running owner/slots");
        issue(0);tick();reset();require(OUT(idle_out),"reset selected slot");
    }
    void contention() {
        reset();
        for(unsigned c=0;c<EC_CORES;++c){request[c]=true;command[c]=1;id[c]=0;address[c]=EC_BITS/8;mask[c]=0;}
        bool seen[EC_CORES]{};unsigned ok=0;
        for(unsigned n=0;n<EC_CORES;++n){
            settle();unsigned chosen=EC_CORES;
            for(unsigned c=0;c<EC_CORES;++c)if(OUT(accepted_out[c])){require(chosen==EC_CORES,"multiple command grants");chosen=c;}
            require(chosen<EC_CORES && !seen[chosen],"command arbiter starvation");seen[chosen]=true;
            tick();request[chosen]=false;require(OUT(response_out[chosen]),"response owner");
            if(OUT(status_out[chosen])==0)++ok;else require(OUT(status_out[chosen])==2,"concurrent issue status");
        }
        require(ok==1,"same-slot issue not atomic");
        // Sustained requests must give every core exactly one grant per round.
        unsigned grants[EC_CORES]{};
        for(unsigned c=0;c<EC_CORES;++c){request[c]=true;command[c]=6;id[c]=0;}
        for(unsigned n=0;n<EC_CORES*12;++n){settle();unsigned count=0;for(unsigned c=0;c<EC_CORES;++c)if(OUT(accepted_out[c])){++count;++grants[c];}require(count==1,"sustained command arbitration");tick();}
        for(unsigned c=0;c<EC_CORES;++c){require(grants[c]==12,"sustained arbiter fairness");request[c]=false;}
        reset();issue(0);free[0]=true;untilLaunched(1);finish(0,17);
        require(cmd(0,4).first==4,"duplicate completion accepted");
        // Two failed prerequisites: cancellation chooses lowest visible ID and is stable.
        issue(1);untilLaunched(2);finish(1,23,true);
        issue(0);untilLaunched(3);finish(0,19,true);
        issue(2,3);tick();require(state(2)==6 && result(2)==19,"multiple failure priority");
    }
    void randomized() {
        std::mt19937 rng(913);
        for(unsigned round=0;round<80;++round) {
            reset();std::array<uint32_t,32> deps{}, expected{};std::array<unsigned,32> terminal{};std::array<bool,32> ran{};
            for(unsigned t=1;t<31;++t) {for(unsigned p=0;p<t;++p)if(rng()%7==0)deps[t]|=1u<<p;}
            deps[31]=0x7fffffff; // fan-in includes every other task
            if(round%4==0)for(unsigned t=1;t<32;++t)deps[t]=1u<<(t-1); // deep chain
            for(unsigned t=0;t<32;++t) {
                expected[t]=t+17;terminal[t]=4;
                for(unsigned p=0;p<t;++p)if(deps[t]&(1u<<p)) {
                    if(terminal[p]!=4){terminal[t]=6;expected[t]=expected[p];break;}
                    expected[t]+=expected[p];
                }
                if(round%3==0 && t==0){terminal[t]=5;expected[t]=0xbad00000+round;}
            }
            // Build backwards: references to not-yet-issued ancestors must wait.
            for(int t=31;t>=0;--t)issue(t,deps[t]);
            for(unsigned c=0;c<EC_CORES;++c)free[c]=true;
            unsigned checked=0;std::array<bool,32> complete{};
            for(unsigned step=0;step<2000;++step) {
                tick();
                while(checked<launches.size()) {
                    auto [t,c]=launches[checked++];require(!ran[t] && terminal[t]!=6,"duplicate/cancelled dispatch");ran[t]=true;
                    for(unsigned p=0;p<32;++p)if(deps[t]&(1u<<p))require(complete[p] && terminal[p]==4,"dispatch before dependencies complete");
                }
                std::vector<unsigned> running;
                for(unsigned t=0;t<32;++t)if(assigned[t]>=0)running.push_back(t);
                if(!running.empty() && rng()%3!=0) {
                    unsigned t=running[rng()%running.size()];complete[t]=true;finish(t,expected[t],terminal[t]==5);
                }
                bool all=true;for(unsigned t=0;t<32;++t)all &= state(t)>=4;
                if(all)break;
                require(step<1999,"random DAG timeout");
            }
            for(unsigned t=0;t<32;++t) {
                require(state(t)==terminal[t] && result(t)==expected[t],"random DAG state/result mismatch round="+std::to_string(round)+" id="+std::to_string(t));
                require(cmd(0,3,t)==std::pair<unsigned,uint32_t>{0,expected[t]},"random result read");
                require(ran[t]==(terminal[t]!=6),"random dispatch coverage");
            }
            require(OUT(idle_out),"terminal graph not idle");
        }
    }
};
int main(){try{Harness h;h.basic();h.contention();h.randomized();std::cout<<"TasksControl PASS: races, ownership, cancellation, 80 full-slot DAGs\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#endif
