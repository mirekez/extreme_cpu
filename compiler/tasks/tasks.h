#pragma once
#include <stdint.h>
// These declarations are compiler intrinsics, not runtime library functions.
extern "C" {
uint32_t __extreme_task_issue(uint32_t id, void (*entry)(), uint32_t predecessors);
uint32_t __extreme_task_disarm(uint32_t id);
uint32_t __extreme_task_read(uint32_t id, uint32_t* result);
uint32_t __extreme_task_state(uint32_t id);
uint32_t __extreme_task_id();
[[noreturn]] void __extreme_task_finish(uint32_t result);
[[noreturn]] void __extreme_task_abort(uint32_t result);
}
namespace extreme::tasks {
using Entry = void (*)();
enum Status : uint32_t { ok=0, invalid=1, locked=2, not_ready=3, not_owner=4 };
enum State : uint32_t { empty=0, waiting=1, reserved=2, running=3, complete=4, failed=5, cancelled=6 };
__attribute__((always_inline)) inline Status issue(uint32_t id, Entry entry, uint32_t predecessors=0) {return Status(__extreme_task_issue(id,entry,predecessors));}
__attribute__((always_inline)) inline Status disarm(uint32_t id) {return Status(__extreme_task_disarm(id));}
// Nonblocking; result must point to writable, naturally aligned uint32_t storage.
__attribute__((always_inline)) inline Status read(uint32_t id, uint32_t& result) {return Status(__extreme_task_read(id,&result));}
__attribute__((always_inline)) inline State state(uint32_t id) {return State(__extreme_task_state(id));}
__attribute__((always_inline)) inline uint32_t id() {return __extreme_task_id();}
[[noreturn]] __attribute__((always_inline)) inline void finish(uint32_t result=0) {__extreme_task_finish(result);}
[[noreturn]] __attribute__((always_inline)) inline void abort(uint32_t result) {__extreme_task_abort(result);}
}
