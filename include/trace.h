#ifndef __FREESTANDING_TRACE_H__
#define __FREESTANDING_TRACE_H__

#ifdef CONFIG_WR_NODE
#include <wrc.h>

#define TRACE_WRAP(...)
#define TRACE_DEV(...) wrc_debug_printf(0, __VA_ARGS__)

#else /* WR_SWITCH */

#include <pp-printf.h>
#define TRACE(...) pp_printf(__VA_ARGS__)
#define TRACE_DEV(...) pp_printf(__VA_ARGS__)

#endif /* node/switch */

#endif
