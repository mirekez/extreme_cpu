#pragma once

// Opaque type needed by libc++ forward declarations. Wide-character runtime
// operations are not provided by this freestanding environment.
typedef struct {
    unsigned state, value;
} mbstate_t;
