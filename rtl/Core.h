#pragma once
#include "LoadFifo.h"
#include "StoreFifo.h"

// Three multicycle stages. Scalar instructions are scheduled serially;
// COPY feeds elastic BUS_WIDTH load/execute/store stage registers every cycle.
class Core : public Module {
public:
    _PORT(bool) load_request_out = _ASSIGN(loads.request_out());
    _PORT(u<32>) load_request_address_out = _ASSIGN(loads.request_address_out());
    _PORT(logic<EC_BITS>) load_request_data_out = _ASSIGN(loads.request_data_out());
    _PORT(u<32>) load_request_tag_out = _ASSIGN(loads.request_tag_out());
    _PORT(bool) load_accepted_in;
    _PORT(bool) load_response_in;
    _PORT(u<32>) load_response_tag_in;
    _PORT(logic<EC_BITS>) load_response_data_in;
    _PORT(bool) load_response_error_in;
    _PORT(u<EC_BITS / 8>) store_request_mask_out = _ASSIGN(stores.request_mask_out());
    _PORT(u<32>) debug_sp_out = _ASSIGN(u<32>(sp));
    _PORT(bool) store_request_out = _ASSIGN(stores.request_out());
    _PORT(u<32>) store_request_address_out = _ASSIGN(stores.request_address_out());
    _PORT(logic<EC_BITS>) store_request_data_out = _ASSIGN(stores.request_data_out());
    _PORT(u<32>) store_request_tag_out = _ASSIGN(stores.request_tag_out());
    _PORT(bool) store_accepted_in;
    _PORT(bool) store_response_in;
    _PORT(u<32>) store_response_tag_in;
    _PORT(logic<EC_BITS>) store_response_data_in;
    _PORT(bool) store_response_error_in;
    LoadFifo loads;
    StoreFifo stores;
    _PORT(bool) copy_active_out = _ASSIGN(state == 4 && !bad);
    _PORT(bool) copy_word_out = _ASSIGN(state == 4 && sv && stores.ready_out() && !bad);
    _PORT(bool) task_launch_in, task_accepted_in, task_response_in;
    _PORT(u<32>) task_address_in, task_id_in, task_status_in, task_result_in;
    _PORT(bool) task_request_out = _ASSIGN(state == 6 && !done && !bad);
    _PORT(u<32>) task_command_out = _ASSIGN(task_command);
    _PORT(u<32>) task_id_out = _ASSIGN(task_target);
    _PORT(u<32>) task_address_out = _ASSIGN(task_address);
    _PORT(u<32>) task_mask_out = _ASSIGN(task_mask);
    _PORT(u<32>) task_value_out = _ASSIGN(task_value);
    _PORT(bool)
    task_free_out = _ASSIGN(started && done && !bad && !loads.error_out() && !stores.error_out() &&
                            loads.empty_out() && stores.empty_out() && !pending);
    _PORT(bool) run_in;
    _PORT(u<32>) entry_in;
    _PORT(bool) halted_out = _ASSIGN(done != 0);
    _PORT(bool) fault_out = _ASSIGN(bad || loads.error_out() || stores.error_out());
    _PORT(u<32>) debug_pc_out = _ASSIGN(pc);
    _PORT(u<32>) debug_register_index_in;
    _PORT(logic<EC_BITS>)
    debug_register_out = _ASSIGN(regs[(uint32_t)debug_register_index_in() % EC_REGS]);
    _PORT(logic<EC_BITS>) load_data_out = _ASSIGN(state == 2 && issued == 0 ? instruction : ld);
    _PORT(logic<EC_BITS>) execute_data_out = _ASSIGN(ed);
    _PORT(logic<EC_BITS>) store_data_out = _ASSIGN(sd);
    _PORT(bool) load_valid_out = _ASSIGN(lv != 0);
    _PORT(bool) execute_valid_out = _ASSIGN(ev != 0);
    _PORT(bool) store_valid_out = _ASSIGN(sv != 0);

private:
    reg<logic<EC_BITS>> regs[EC_REGS];
    reg<logic<EC_BITS>> ib[2], instruction, ld, ed, sd;
    reg<u<32>> ia[2], pc, state, op, dst, left, right, immediate;
    reg<u<32>> loop_pc, issued, transferred, total, source, destination, scalar_address;
    reg<u<clog2(EC_REGS*(EC_BITS / 32) + 1)>> sp, stack_limit;
    reg<u<EC_BITS / 8>> store_mask;
    reg<u<32>> scalar_offset;
    reg<u<32>> task_command, task_target, task_address, task_mask, task_value, current_task;
    reg<u1> iv[2], pending, started, done, bad, lv, ev, sv, loop_active;
    u<32> fetch_address_comb;

    u<32>& fetch_address_comb_func() {
        uint32_t word = (uint32_t)pc / (EC_BITS / 8);
        unsigned slot = word & 1;
        fetch_address_comb = word * (EC_BITS / 8);
        if (iv[slot] && ia[slot] == fetch_address_comb) {
            fetch_address_comb = (word + 1) * (EC_BITS / 8);
        }
        return fetch_address_comb;
    }

    bool fetch_comb;

    bool& fetch_comb_func() {
        uint32_t word = (uint32_t)pc / (EC_BITS / 8);
        unsigned slot = word & 1;
        bool current = iv[slot] && ia[slot] == word * (EC_BITS / 8);
        bool next = iv[slot ^ 1] && ia[slot ^ 1] == (word + 1) * (EC_BITS / 8);
        fetch_comb = started && !done && !bad && !pending && state != 5 && state != 6 &&
                     state != 7 &&
                     (!current || (((uint32_t)pc % (EC_BITS / 8)) >= EC_BITS / 16 && !next));
        return fetch_comb;
    }

    bool data_request_comb;

    bool& data_request_comb_func() {
        data_request_comb = started && !done && !bad &&
                            ((state == 1 && issued == 0) || (state == 4 && issued < total));
        return data_request_comb;
    }

    bool take_comb;

    bool& take_comb_func() {
        take_comb =
            loads.valid_out() && (!loads.result_kind_out() || state == 1 ||
                                  (state == 4 && (!lv || !ev || !sv || stores.ready_out())));
        return take_comb;
    }

public:
    void _assign() {
        loads.push_in = _ASSIGN(fetch_comb_func() || data_request_comb_func());
        loads.address_in =
            _ASSIGN(fetch_comb_func() ? uint32_t(fetch_address_comb_func())
                                      : (state == 1 ? uint32_t(scalar_address)
                                                    : uint32_t(source + issued * (EC_BITS / 8))));
        loads.data_in = _ASSIGN(logic<EC_BITS>(0));
        loads.kind_in = _ASSIGN(!fetch_comb_func());
        loads.pop_in = _ASSIGN_COMB(take_comb_func());
        stores.push_in = _ASSIGN((state == 3 || state == 4) && sv && !bad);
        stores.address_in =
            _ASSIGN(state == 4 ? destination + transferred * (EC_BITS / 8) : scalar_address);
        stores.data_in = _ASSIGN_REG(sd);
        stores.mask_in =
            _ASSIGN(state == 4 ? u<EC_BITS / 8>(~uint64_t(0)) : u<EC_BITS / 8>(store_mask));
        stores.kind_in = _ASSIGN(false);
        stores.pop_in = _ASSIGN(false);
        loads.accepted_in = load_accepted_in;
        loads.response_in = load_response_in;
        loads.response_tag_in = load_response_tag_in;
        loads.response_data_in = load_response_data_in;
        loads.response_error_in = load_response_error_in;
        stores.accepted_in = store_accepted_in;
        stores.response_in = store_response_in;
        stores.response_tag_in = store_response_tag_in;
        stores.response_data_in = store_response_data_in;
        stores.response_error_in = store_response_error_in;
        loads._assign();
        stores._assign();
    }

    void _work(bool reset) {
        unsigned i, slot, pos, code, n, d, a, b, size, lane, index;
        uint32_t x, y;
        logic<EC_BITS> ins, wide_mask, wide_value;
        bool sr, er, lr;
        loads._work(reset || task_launch_in());
        stores._work(reset || task_launch_in());
        if (reset || task_launch_in()) {
            for (i = 0; i < EC_REGS; ++i) {
                regs[i]._next = 0;
            }
            for (i = 0; i < 2; ++i) {
                ib[i]._next = 0;
                ia[i]._next = 0;
                iv[i]._next = 0;
            }
            sp._next = 0;
            stack_limit._next = EC_REGS * (EC_BITS / 32);
            scalar_offset._next = 0;
            store_mask._next = 0;
            task_command._next = 0;
            task_target._next = 0;
            task_address._next = 0;
            task_mask._next = 0;
            task_value._next = 0;
            current_task._next = reset ? uint32_t(0xffffffff) : uint32_t(task_id_in());
            pc._next = reset ? u<32>(0) : task_address_in();
            state._next = 0;
            op._next = 0;
            dst._next = 0;
            left._next = 0;
            right._next = 0;
            immediate._next = 0;
            loop_pc._next = 0;
            issued._next = 0;
            transferred._next = 0;
            total._next = 0;
            source._next = 0;
            destination._next = 0;
            scalar_address._next = 0;
            pending._next = 0;
            started._next = reset ? 0 : 1;
            done._next = 0;
            bad._next = 0;
            loop_active._next = 0;
            instruction._next = 0;
            ld._next = 0;
            ed._next = 0;
            sd._next = 0;
            lv._next = 0;
            ev._next = 0;
            sv._next = 0;
        } else if (!started) {
            if (run_in()) {
                started._next = 1;
                pc._next = entry_in();
                if (entry_in() % (EC_BITS / 8) != 0) {
                    bad._next = 1;
                }
            }
        } else if (!done && !bad) {
            if (loads.error_out() || stores.error_out()) {
                bad._next = 1;
                done._next = 1;
            }
            if (loads.push_in() && loads.ready_out()) {
                if (fetch_comb_func()) {
                    pending._next = 1;
                } else {
                    issued._next = issued + 1;
                }
            }
            if (take_comb_func() && !loads.result_kind_out()) {
                slot = ((uint32_t)loads.result_address_out() / (EC_BITS / 8)) & 1;
                ib[slot]._next = loads.result_out();
                ia[slot]._next = loads.result_address_out();
                iv[slot]._next = 1;
                pending._next = 0;
            }
            if (state == 0) {
                slot = ((uint32_t)pc / (EC_BITS / 8)) & 1;
                pos = (uint32_t)pc % (EC_BITS / 8);
                if (iv[slot] && ia[slot] == ((uint32_t)pc / (EC_BITS / 8)) * (EC_BITS / 8)) {
                    ins = ib[slot] >> (pos * 8);
                    code = (uint32_t)ins & 255;
                    n = 0;
                    if (code <= 8) {
                        n = 1;
                    }
                    if (code == 80 || code == 82) {
                        n = 4;
                    }
                    if (code == 81 || code == 85) {
                        n = 3;
                    }
                    if (code == 83 || code == 84 || code == 86) {
                        n = 2;
                    }
                    if (code == 16 || code == 65) {
                        n = 6;
                    }
                    if (code == 17 || code == 48 || code == 49) {
                        n = 3;
                    }
                    if (code >= 32 && code <= 46) {
                        n = 4;
                    }
                    if (code == 50 || code == 51) {
                        n = 4;
                    }
                    if (code == 67 || code == 68) {
                        n = 2;
                    }
                    if (code == 64 || code == 66 || code == 69) {
                        n = 5;
                    }
                    if (n == 0 || pos + n > EC_BITS / 8) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        instruction._next = ins;
                        op._next = code;
                        pc._next = pc + n;
                        dst._next = (ins >> 8) & 255;
                        left._next = (ins >> 16) & 255;
                        right._next = (ins >> 24) & 255;
                        immediate._next = (code == 64 || code == 66 || code == 69)
                                              ? u<32>(ins >> 8)
                                              : u<32>(ins >> 16);
                        state._next = 2;
                        lv._next = 1;
                    }
                }
            } else if (state == 1) {
                if (take_comb_func() && loads.result_kind_out()) {
                    ld._next = loads.result_out();
                    lv._next = 1;
                    state._next = 2;
                }
            } else if (state == 2) {
                d = (uint32_t)dst % EC_REGS;
                a = (uint32_t)left % EC_REGS;
                b = (uint32_t)right % EC_REGS;
                x = (uint32_t)regs[a];
                y = (uint32_t)regs[b];
                lv._next = 0;
                state._next = 0;
                if ((op == 16 || op == 65 || op == 67 || op == 68) && dst >= EC_REGS) {
                    bad._next = 1;
                    done._next = 1;
                } else if ((op == 17 || op == 48 || op == 49 || op == 50 || op == 51 ||
                            (op >= 32 && op <= 46)) &&
                           (dst >= EC_REGS || left >= EC_REGS ||
                            (op >= 32 && op <= 46 && right >= EC_REGS))) {
                    bad._next = 1;
                    done._next = 1;
                } else if (op >= 80 && op <= 86) {
                    if (dst >= EC_REGS ||
                        ((op == 80 || op == 81 || op == 82 || op == 85) && left >= EC_REGS) ||
                        ((op == 80 || op == 82) && right >= EC_REGS) ||
                        (op == 82 && dst == right)) {
                        bad._next = 1;
                        done._next = 1;
                    } else if (op == 86) {
                        regs[d]._next = logic<EC_BITS>(current_task);
                    } else {
                        task_command._next = op == 80   ? 1
                                             : op == 81 ? 2
                                             : op == 82 ? 3
                                             : op == 83 ? 4
                                             : op == 84 ? 5
                                                        : 6;
                        task_target._next = op == 80 ? uint32_t(regs[d]) : x;
                        task_address._next = x;
                        task_mask._next = y;
                        task_value._next = uint32_t(regs[d]);
                        state._next = (op == 80 || op == 83 || op == 84) ? 5 : 6;
                    }
                } else if (op == 1 || op == 2) {
                    state._next = 5;
                } else if (op == 3) {
                    total._next = (uint32_t)regs[2];
                    source._next = (uint32_t)regs[0];
                    destination._next = (uint32_t)regs[1];
                    issued._next = 0;
                    transferred._next = 0;
                    lv._next = 0;
                    ev._next = 0;
                    sv._next = 0;
                    if ((uint32_t)regs[2] != 0) {
                        state._next = 4;
                    }
                } else if (op == 4) {
                    if (loop_active || (uint32_t)regs[2] == 0) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        loop_pc._next = pc;
                        loop_active._next = 1;
                    }
                } else if (op == 5) {
                    if (!loop_active) {
                        bad._next = 1;
                        done._next = 1;
                    } else if ((uint32_t)regs[2] > 1) {
                        regs[2]._next = logic<EC_BITS>((uint32_t)regs[2] - 1);
                        pc._next = loop_pc;
                    } else {
                        regs[2]._next = 0;
                        loop_active._next = 0;
                    }
                } else if (op == 6) {
                    regs[0]._next = logic<EC_BITS>((uint32_t)regs[0] + EC_BITS / 8);
                } else if (op == 7) {
                    regs[1]._next = logic<EC_BITS>((uint32_t)regs[1] + EC_BITS / 8);
                } else if (op == 16) {
                    regs[d]._next = logic<EC_BITS>(immediate);
                } else if (op == 17) {
                    regs[d]._next = regs[a];
                } else if (op == 32) {
                    regs[d]._next = logic<EC_BITS>(uint32_t(x + y));
                } else if (op == 33) {
                    regs[d]._next = logic<EC_BITS>(uint32_t(x - y));
                } else if (op == 34) {
                    regs[d]._next = logic<EC_BITS>(x & y);
                } else if (op == 35) {
                    regs[d]._next = logic<EC_BITS>(x | y);
                } else if (op == 36) {
                    regs[d]._next = logic<EC_BITS>(x ^ y);
                } else if (op == 37) {
                    regs[d]._next = logic<EC_BITS>(uint32_t(x << (y & 31)));
                } else if (op == 38) {
                    regs[d]._next = logic<EC_BITS>(x >> (y & 31));
                } else if (op == 39) {
                    regs[d]._next = logic<EC_BITS>(x < y ? 1 : 0);
                } else if (op == 40) {
                    regs[d]._next = logic<EC_BITS>(uint32_t(int32_t(x) >> (y & 31)));
                } else if (op == 41) {
                    regs[d]._next = logic<EC_BITS>(uint32_t(x * y));
                } else if (op == 42) {
                    regs[d]._next = logic<EC_BITS>(y == 0 ? uint32_t(0xffffffff) : x / y);
                } else if (op == 43) {
                    if (y == 0) {
                        regs[d]._next = logic<EC_BITS>(uint32_t(0xffffffff));
                    } else if (x == 0x80000000 && y == 0xffffffff) {
                        regs[d]._next = logic<EC_BITS>(x);
                    } else {
                        regs[d]._next = logic<EC_BITS>(uint32_t(int32_t(x) / int32_t(y)));
                    }
                } else if (op == 44) {
                    regs[d]._next = logic<EC_BITS>(y == 0 ? x : x % y);
                } else if (op == 45) {
                    if (y == 0) {
                        regs[d]._next = logic<EC_BITS>(x);
                    } else if (x == 0x80000000 && y == 0xffffffff) {
                        regs[d]._next = 0;
                    } else {
                        regs[d]._next = logic<EC_BITS>(uint32_t(int32_t(x) % int32_t(y)));
                    }
                } else if (op == 46) {
                    regs[d]._next = logic<EC_BITS>(int32_t(x) < int32_t(y) ? 1 : 0);
                } else if (op == 50 || op == 51) {
                    size = 1u << ((uint32_t)right & 3);
                    if ((right != 0 && right != 1 && right != 2 && right != 4 && right != 5) ||
                        (op == 51 && right > 2) || x % size != 0) {
                        bad._next = 1;
                        done._next = 1;
                    } else if (op == 50) {
                        if (issued == 0) {
                            scalar_address._next = x & ~(EC_BITS / 8 - 1);
                            scalar_offset._next = x % (EC_BITS / 8);
                            state._next = 1;
                        } else {
                            y = (uint32_t)(ld >> (scalar_offset * 8));
                            if ((right & 3) == 0) {
                                y = y & 255;
                                if (right == 0 && (y & 128)) {
                                    y = y | 0xffffff00;
                                }
                            }
                            if ((right & 3) == 1) {
                                y = y & 65535;
                                if (right == 1 && (y & 32768)) {
                                    y = y | 0xffff0000;
                                }
                            }
                            regs[d]._next = logic<EC_BITS>(y);
                            issued._next = 0;
                        }
                    } else {
                        scalar_address._next = x & ~(EC_BITS / 8 - 1);
                        store_mask._next = ((uint64_t(1) << size) - 1) << (x % (EC_BITS / 8));
                        sd._next = logic<EC_BITS>(uint32_t(regs[d])) << ((x % (EC_BITS / 8)) * 8);
                        sv._next = 1;
                        state._next = 3;
                    }
                } else if (op == 48) {
                    if (issued == 0) {
                        scalar_address._next = x;
                        state._next = 1;
                    } else {
                        regs[d]._next = ld;
                        issued._next = 0;
                    }
                } else if (op == 49) {
                    store_mask._next = ~uint64_t(0);
                    sd._next = regs[d];
                    scalar_address._next = x;
                    sv._next = 1;
                    state._next = 3;
                } else if (op == 69) {
                    if (sp != 0 || immediate == 0 || immediate > EC_REGS * (EC_BITS / 32)) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        stack_limit._next = immediate;
                    }
                } else if (op == 66 || op == 68) {
                    if (sp >= stack_limit || (op == 68 && (uint32_t)regs[d] % (EC_BITS / 8) != 0)) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        index = (uint32_t)sp / (EC_BITS / 32);
                        lane = ((uint32_t)sp % (EC_BITS / 32)) * 32;
                        wide_mask = logic<EC_BITS>(uint32_t(0xffffffff)) << lane;
                        wide_value = logic<EC_BITS>(uint32_t((pc + EC_BITS / 8 - 1) &
                                                             ~uint32_t(EC_BITS / 8 - 1)))
                                     << lane;
                        regs[index]._next = (regs[index] & ~wide_mask) | wide_value;
                        sp._next = sp + 1;
                        pc._next =
                            op == 66 ? uint32_t(immediate * (EC_BITS / 8)) : uint32_t(regs[d]);
                    }
                } else if (op == 8) {
                    if (sp == 0) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        index = ((uint32_t)sp - 1) / (EC_BITS / 32);
                        lane = (((uint32_t)sp - 1) % (EC_BITS / 32)) * 32;
                        x = (uint32_t)(regs[index] >> lane);
                        if (x % (EC_BITS / 8) != 0) {
                            bad._next = 1;
                            done._next = 1;
                        } else {
                            pc._next = x;
                            sp._next = sp - 1;
                        }
                    }
                } else if (op == 67) {
                    if ((uint32_t)regs[d] % (EC_BITS / 8) != 0) {
                        bad._next = 1;
                        done._next = 1;
                    } else {
                        pc._next = (uint32_t)regs[d];
                    }
                } else if (op == 64 || (op == 65 && (uint32_t)regs[d] == 0)) {
                    pc._next = immediate * (EC_BITS / 8);
                }
            } else if (state == 3) {
                if (stores.ready_out()) {
                    sv._next = 0;
                    state._next = 0;
                }
            } else if (state == 4) {
                // Elastic stage registers: no register dependency scoreboard or bypass.
                sr = !sv || stores.ready_out();
                er = !ev || sr;
                lr = !lv || er;
                if (sr) {
                    sv._next = ev;
                    sd._next = ed;
                }
                if (er) {
                    ev._next = lv;
                    ed._next = ld;
                }
                if (lr) {
                    lv._next = 0;
                    if (take_comb_func() && loads.result_kind_out()) {
                        lv._next = 1;
                        ld._next = loads.result_out();
                    }
                }
                if (sv && stores.ready_out()) {
                    transferred._next = transferred + 1;
                    if (transferred + 1 == total) {
                        state._next = 0;
                        issued._next = 0;
                        regs[0]._next = logic<EC_BITS>(uint32_t(source + total * (EC_BITS / 8)));
                        regs[1]._next =
                            logic<EC_BITS>(uint32_t(destination + total * (EC_BITS / 8)));
                        regs[2]._next = 0;
                    }
                }
            } else if (state == 5) {
                if (stores.empty_out() && loads.empty_out() && !pending) {
                    iv[0]._next = 0;
                    iv[1]._next = 0;
                    if (op == 1) {
                        done._next = 1;
                    } else if (op == 80 || op == 83 || op == 84) {
                        state._next = 6;
                    } else {
                        state._next = 0;
                    }
                }
            } else if (state == 6) {
                if (task_accepted_in()) {
                    state._next = 7;
                }
            } else if (state == 7 && task_response_in()) {
                if (op == 83 || op == 84) {
                    done._next = 1;
                    if (task_status_in() != 0) {
                        bad._next = 1;
                    }
                } else {
                    if (op == 82) {
                        regs[(uint32_t)dst]._next = logic<EC_BITS>(task_result_in());
                        regs[(uint32_t)right]._next = logic<EC_BITS>(task_status_in());
                    } else if (op == 85) {
                        regs[(uint32_t)dst]._next =
                            logic<EC_BITS>(task_status_in() == 0 ? uint32_t(task_result_in())
                                                                 : uint32_t(0xffffffff));
                    } else {
                        regs[(uint32_t)dst]._next = logic<EC_BITS>(task_status_in());
                    }
                    state._next = 0;
                }
            }
        }
    }

    void _strobe() {
        unsigned i;
        loads._strobe();
        stores._strobe();
        for (i = 0; i < EC_REGS; ++i) {
            regs[i].strobe();
        }
        for (i = 0; i < 2; ++i) {
            ib[i].strobe();
            ia[i].strobe();
            iv[i].strobe();
        }
        task_command.strobe();
        task_target.strobe();
        task_address.strobe();
        task_mask.strobe();
        task_value.strobe();
        current_task.strobe();
        sp.strobe();
        stack_limit.strobe();
        scalar_offset.strobe();
        store_mask.strobe();
        pc.strobe();
        state.strobe();
        op.strobe();
        dst.strobe();
        left.strobe();
        right.strobe();
        immediate.strobe();
        loop_pc.strobe();
        issued.strobe();
        transferred.strobe();
        total.strobe();
        source.strobe();
        destination.strobe();
        scalar_address.strobe();
        pending.strobe();
        started.strobe();
        done.strobe();
        bad.strobe();
        loop_active.strobe();
        instruction.strobe();
        ld.strobe();
        ed.strobe();
        sd.strobe();
        lv.strobe();
        ev.strobe();
        sv.strobe();
    }
};
