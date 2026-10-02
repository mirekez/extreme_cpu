#pragma once
#include "../../tests/FifoHarness.h"

inline void fifo_cases(bool store) {
    Harness h;
    auto& dut = h.dut;
    h.tick(true);
    h.tick(true);

    struct Flight {
        unsigned slot, sequence;
    };

    std::vector<Flight> flying;
    std::deque<unsigned> expected;
    std::mt19937 rng(42);
    unsigned pushed = 0, issued = 0, returned = 0, popped = 0;
    bool saw_full = false;
    for (unsigned cycle = 0; cycle < 15000; ++cycle) {
        h.push_in = pushed < 513 && (rng() % 4 != 0);
        h.address_in = pushed * (EC_BITS / 8);
        h.data_in = pattern(pushed + 9);
        h.kind_in = pushed & 1;
        h.accepted_in = cycle > EC_DEPTH + 4 && rng() % 3 != 0;
        h.pop_in = rng() % 4 != 0;
        h.response_in = !flying.empty() && rng() % 3 != 0;
        unsigned selected = 0;
        if (h.response_in) {
            selected = rng() % flying.size();
            h.response_tag_in = flying[selected].slot;
            h.response_data_in = pattern(flying[selected].sequence + 9);
        }
        h.settle();
        bool add = h.push_in && OUT(ready_out);
        bool send = OUT(request_out) && h.accepted_in;
        bool pop = OUT(valid_out) && (store || h.pop_in);
        saw_full |= !OUT(ready_out);
        unsigned slot = OUT(request_tag_out);
        if (send) {
            require(OUT(request_address_out) == issued * (EC_BITS / 8), "request order");
            if (store) {
                require(read_word(OUT(request_data_out)) == pattern(issued + 9), "store payload");
            }
        }
        if (pop) {
            require(!expected.empty(), "spurious completion");
            require(OUT(result_address_out) == expected.front() * (EC_BITS / 8),
                    "response address order");
            require(bool(OUT(result_kind_out)) == bool(expected.front() & 1), "response metadata");
            if (!store) {
                require(read_word(OUT(result_out)) == pattern(expected.front() + 9),
                        "load payload");
            }
            expected.pop_front();
            ++popped;
        }
        if (h.response_in) {
            flying.erase(flying.begin() + selected);
            ++returned;
        }
        if (send) {
            flying.push_back({slot, issued++});
        }
        if (add) {
            expected.push_back(pushed++);
        }
        h.tick();
        require(!OUT(error_out), "unexpected FIFO error");
        if (popped == 513) {
            break;
        }
    }
    h.settle();
    require(pushed == 513 && issued == 513 && returned == 513 && popped == 513 &&
                expected.empty() && OUT(empty_out),
            "FIFO did not drain");
    require(saw_full, "full backpressure not exercised");
    // Reset cancels queued requests and sticky faults.
    h.push_in = true;
    h.response_in = false;
    h.accepted_in = false;
    h.tick();
    h.push_in = false;
    h.response_in = true;
    h.response_error_in = true;
    h.response_tag_in = 0;
    h.tick();
    require(OUT(error_out), "error must be sticky");
    h.response_in = false;
    h.tick(true);
    h.settle();
    require(OUT(empty_out) && !OUT(error_out) && !OUT(request_out), "reset cancellation");
}
