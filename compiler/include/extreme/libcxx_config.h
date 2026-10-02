#pragma once
// Adapt the installed libc++ headers to this freestanding target. No host ABI
// or host runtime is linked into an Extreme executable.
#include <__config_site>
#undef _LIBCPP_HAS_THREADS
#define _LIBCPP_HAS_THREADS 0
#undef _LIBCPP_HAS_WIDE_CHARACTERS
#define _LIBCPP_HAS_WIDE_CHARACTERS 0
#undef _LIBCPP_HAS_LOCALIZATION
#define _LIBCPP_HAS_LOCALIZATION 0
#undef _LIBCPP_HAS_FILESYSTEM
#define _LIBCPP_HAS_FILESYSTEM 0
#undef _LIBCPP_HAS_RANDOM_DEVICE
#define _LIBCPP_HAS_RANDOM_DEVICE 0
#undef _LIBCPP_HAS_TIME_ZONE_DATABASE
#define _LIBCPP_HAS_TIME_ZONE_DATABASE 0
#undef _LIBCPP_PSTL_BACKEND_STD_THREAD
#define _LIBCPP_PSTL_BACKEND_SERIAL

#define _LIBCPP_REMOVE_TRANSITIVE_INCLUDES
