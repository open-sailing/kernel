#undef TRACE_SYSTEM
#define TRACE_SYSTEM bpf

#if !defined(_TRACE_BPF_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_BPF_H

#include <linux/tracepoint.h>

TRACE_EVENT(bpf_output_data,

	TP_PROTO(u64 *src, int size),

	TP_ARGS(src, size),

	TP_STRUCT__entry(
		__dynamic_array(u8,		buf,		size)
	),

	TP_fast_assign(
		memcpy(__get_dynamic_array(buf), src, size);
	),

	TP_printk("%s", __print_hex(__get_dynamic_array(buf),
				    __get_dynamic_array_len(buf)))
);

#endif /* _TRACE_BPF_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
