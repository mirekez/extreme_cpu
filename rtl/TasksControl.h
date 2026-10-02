#pragma once
#include "Config.h"

// 32 associative dependency slots, serialized commands, two-step dispatch.
class TasksControl : public Module {
public:
    _PORT(bool) enable_in;
    _PORT(bool) request_in[EC_CORES], free_in[EC_CORES], fault_in[EC_CORES];
    _PORT(u<32>)
    command_in[EC_CORES], id_in[EC_CORES], address_in[EC_CORES], mask_in[EC_CORES],
        value_in[EC_CORES];
    _PORT(bool) accepted_out[EC_CORES], response_out[EC_CORES], launch_out[EC_CORES];
    _PORT(u<32>)
    status_out[EC_CORES], result_out[EC_CORES], launch_address_out[EC_CORES],
        launch_id_out[EC_CORES];
    _PORT(u<32>) debug_id_in;
    _PORT(u<32>) debug_state_out = _ASSIGN(u<32>(state[(uint32_t)debug_id_in() % 32]));
    _PORT(u<32>) debug_result_out = _ASSIGN(result[(uint32_t)debug_id_in() % 32]);
    _PORT(bool) idle_out = _ASSIGN_COMB(idle_comb_func());

private:
    // state: empty, waiting, reserved, running, complete, failed, cancelled.
    reg<u<3>> state[32];
    reg<u<32>> address[32], ancestors[32], result[32];
    reg<u1> owned[EC_CORES], response[EC_CORES], launch[EC_CORES];
    reg<u<5>> owner[EC_CORES];
    reg<u<32>> status[EC_CORES], answer[EC_CORES], launch_address[EC_CORES], launch_id[EC_CORES];
    reg<u1> selected;
    reg<u<5>> selected_id, task_rr;
    reg<u<32>> command_rr, core_rr;
    u<32> grant_comb;

    u<32>& grant_comb_func() {
        unsigned k, c;
        grant_comb = EC_CORES;
        for (k = 0; k < EC_CORES; ++k) {
            c = (uint32_t(command_rr) + k) % EC_CORES;
            if (enable_in() && request_in[c]() && grant_comb == EC_CORES) {
                grant_comb = c;
            }
        }
        return grant_comb;
    }

    bool idle_comb;

    bool& idle_comb_func() {
        unsigned i;
        idle_comb = !selected;
        for (i = 0; i < 32; ++i) {
            if (state[i] >= 1 && state[i] <= 3) {
                idle_comb = false;
            }
        }
        return idle_comb;
    }

public:
    void _assign() {
        unsigned i;
        for (i = 0; i < EC_CORES; ++i) {
            accepted_out[i] = _ASSIGN_I(grant_comb_func() == i);
            response_out[i] = _ASSIGN_I(response[i] != 0);
            status_out[i] = _ASSIGN_I(status[i]);
            result_out[i] = _ASSIGN_I(answer[i]);
            launch_out[i] = _ASSIGN_I(launch[i] != 0);
            launch_address_out[i] = _ASSIGN_I(launch_address[i]);
            launch_id_out[i] = _ASSIGN_I(launch_id[i]);
        }
    }

    void _work(bool reset) {
        unsigned i, c, k, t, cmd, id, chosen;
        uint32_t completed, failed, references, deps, bit, failed_value;
        bool found;
        if (reset) {
            selected._next = 0;
            selected_id._next = 0;
            task_rr._next = 0;
            command_rr._next = 0;
            core_rr._next = 0;
            for (i = 0; i < 32; ++i) {
                state[i]._next = 0;
                address[i]._next = 0;
                ancestors[i]._next = 0;
                result[i]._next = 0;
            }
            for (c = 0; c < EC_CORES; ++c) {
                owned[c]._next = 0;
                owner[c]._next = 0;
                response[c]._next = 0;
                launch[c]._next = 0;
                status[c]._next = 0;
                answer[c]._next = 0;
                launch_address[c]._next = 0;
                launch_id[c]._next = 0;
            }
        } else {
            for (c = 0; c < EC_CORES; ++c) {
                response[c]._next = 0;
                launch[c]._next = 0;
            }
            if (enable_in()) {
                completed = 0;
                failed = 0;
                references = 0;
                for (i = 0; i < 32; ++i) {
                    bit = uint32_t(1) << i;
                    if (state[i] == 4) {
                        completed = completed | bit;
                    }
                    if (state[i] == 5 || state[i] == 6) {
                        failed = failed | bit;
                    }
                    if (state[i] >= 1 && state[i] <= 3) {
                        references = references | uint32_t(ancestors[i]);
                    }
                }
                // A halted/faulted task without TFINISH/TABORT is an abnormal exit.
                // Ignore the launch pulse while the core is consuming its new entry.
                for (c = 0; c < EC_CORES; ++c) {
                    if (owned[c] && !launch[c] && (free_in[c]() || fault_in[c]())) {
                        state[(uint32_t)owner[c]]._next = 5;
                        result[(uint32_t)owner[c]]._next = 0xfffffffe;
                        owned[c]._next = 0;
                    }
                }
                for (i = 0; i < 32; ++i) {
                    if (state[i] == 1 && (uint32_t(ancestors[i]) & failed) != 0) {
                        failed_value = 0;
                        found = false;
                        for (k = 0; k < 32; ++k) {
                            if (!found &&
                                ((uint32_t(ancestors[i]) & failed) & (uint32_t(1) << k)) != 0) {
                                failed_value = result[k];
                                found = true;
                            }
                        }
                        state[i]._next = 6;
                        result[i]._next = failed_value;
                    }
                }
                c = grant_comb_func();
                cmd = 0;
                id = 32;
                if (c < EC_CORES) {
                    cmd = command_in[c]();
                    id = id_in[c]();
                    t = id % 32;
                    command_rr._next = (c + 1) % EC_CORES;
                    response[c]._next = 1;
                    status[c]._next = 0;
                    answer[c]._next = 0;
                    // statuses: OK=0, invalid=1, locked=2, not ready=3, not owner=4.
                    if (cmd == 4 || cmd == 5) {
                        if (!owned[c] || fault_in[c]()) {
                            status[c]._next = 4;
                        } else {
                            state[(uint32_t)owner[c]]._next = cmd == 4 ? 4 : 5;
                            result[(uint32_t)owner[c]]._next = value_in[c]();
                            owned[c]._next = 0;
                        }
                    } else if (id >= 32) {
                        status[c]._next = 1;
                    } else if (cmd == 1) {
                        if (address_in[c]() % (EC_BITS / 8) != 0 ||
                            (mask_in[c]() & (uint32_t(1) << t)) != 0) {
                            status[c]._next = 1;
                        } else if ((state[t] >= 1 && state[t] <= 3) ||
                                   (state[t] >= 4 && (references & (uint32_t(1) << t)) != 0)) {
                            status[c]._next = 2;
                        } else {
                            state[t]._next = 1;
                            address[t]._next = address_in[c]();
                            ancestors[t]._next = mask_in[c]();
                            result[t]._next = 0;
                        }
                    } else if (cmd == 2) {
                        if (state[t] == 2 || state[t] == 3) {
                            status[c]._next = 2;
                        } else if (state[t] == 1) {
                            state[t]._next = 6;
                            result[t]._next = 0xfffffffd;
                        } else if ((references & (uint32_t(1) << t)) != 0) {
                            status[c]._next = 2;
                        } else {
                            state[t]._next = 0;
                            ancestors[t]._next = 0;
                            result[t]._next = 0;
                        }
                    } else if (cmd == 3) {
                        if (state[t] < 4) {
                            status[c]._next = 3;
                        } else {
                            answer[c]._next = result[t];
                        }
                    } else if (cmd == 6) {
                        answer[c]._next = u<32>(state[t]);
                    } else {
                        status[c]._next = 1;
                    }
                }
                if (selected) {
                    chosen = EC_CORES;
                    for (k = 0; k < EC_CORES; ++k) {
                        t = (uint32_t(core_rr) + k) % EC_CORES;
                        if (chosen == EC_CORES && free_in[t]() && !fault_in[t]() && !owned[t] &&
                            !launch[t]) {
                            chosen = t;
                        }
                    }
                    if (chosen < EC_CORES) {
                        t = selected_id;
                        state[t]._next = 3;
                        selected._next = 0;
                        owned[chosen]._next = 1;
                        owner[chosen]._next = t;
                        launch[chosen]._next = 1;
                        launch_address[chosen]._next = address[t];
                        launch_id[chosen]._next = t;
                        core_rr._next = (chosen + 1) % EC_CORES;
                    }
                } else {
                    found = false;
                    for (k = 0; k < 32; ++k) {
                        t = (uint32_t(task_rr) + k) % 32;
                        deps = ancestors[t];
                        if (!found && state[t] == 1 && (deps & completed) == deps &&
                            !((cmd == 1 || cmd == 2) && id == t)) {
                            selected._next = 1;
                            selected_id._next = t;
                            state[t]._next = 2;
                            task_rr._next = (t + 1) % 32;
                            found = true;
                        }
                    }
                }
            }
        }
    }

    void _strobe() {
        unsigned i;
        for (i = 0; i < 32; ++i) {
            state[i].strobe();
            address[i].strobe();
            ancestors[i].strobe();
            result[i].strobe();
        }
        for (i = 0; i < EC_CORES; ++i) {
            owned[i].strobe();
            owner[i].strobe();
            response[i].strobe();
            launch[i].strobe();
            status[i].strobe();
            answer[i].strobe();
            launch_address[i].strobe();
            launch_id[i].strobe();
        }
        selected.strobe();
        selected_id.strobe();
        task_rr.strobe();
        command_rr.strobe();
        core_rr.strobe();
    }
};
