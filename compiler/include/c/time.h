#pragma once
typedef long time_t;
typedef long clock_t;
struct tm {int tm_sec,tm_min,tm_hour,tm_mday,tm_mon,tm_year,tm_wday,tm_yday,tm_isdst;};
struct timespec {time_t tv_sec;long tv_nsec;};
