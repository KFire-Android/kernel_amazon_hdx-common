/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM mmc

#if !defined(_TRACE_MMC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_MMC_H

#include <linux/tracepoint.h>

TRACE_EVENT(mmc_clk,
		TP_PROTO(char *print_info),

		TP_ARGS(print_info),

		TP_STRUCT__entry(
			__string(print_info, print_info)
		),

		TP_fast_assign(
			__assign_str(print_info, print_info);
		),

		TP_printk("%s",
			__get_str(print_info)
		)
);

DECLARE_EVENT_CLASS(start_req,
       TP_PROTO(const char * host, unsigned int cmd,
                unsigned int arg, unsigned int flags,
                unsigned int blksz, unsigned int blocks),
       TP_ARGS(host, cmd, arg, flags, blksz, blocks),

       TP_STRUCT__entry(
           __string(host, host)
           __field(unsigned int, cmd   )
           __field(unsigned int, arg )
           __field(unsigned int, flags )
           __field(unsigned int, blksz )
           __field(unsigned int, blocks )
          ),

       TP_fast_assign(
           __assign_str(host, host);
           __entry->cmd = cmd;
           __entry->arg = arg;
           __entry->flags = flags;
           __entry->blksz = blksz;
           __entry->blocks = blocks;
       ),

       TP_printk("host=%s CMD%u arg=%08x flags=%08x blksz=%08x blocks=%08x",
             __get_str(host), __entry->cmd,
             __entry->arg, __entry->flags,
             __entry->blksz, __entry->blocks)
);

DEFINE_EVENT(start_req, mmc_start_req,
       TP_PROTO(const char *host, unsigned int cmd,
            unsigned int arg, unsigned int flags,
            unsigned int blksz, unsigned int blocks),
       TP_ARGS(host, cmd, arg, flags, blksz, blocks)
);


DECLARE_EVENT_CLASS(req_done,
       TP_PROTO(const char *host, unsigned int cmd,
               int err, unsigned int resp1,
               unsigned int resp2, unsigned int resp3,
               unsigned int resp4),
       TP_ARGS(host, cmd, err, resp1, resp2, resp3, resp4),

       TP_STRUCT__entry(
           __string(host, host)
           __field(unsigned int, cmd   )
           __field(         int, err )
           __field(unsigned int, resp1 )
           __field(unsigned int, resp2 )
           __field(unsigned int, resp3 )
           __field(unsigned int, resp4 )
          ),

       TP_fast_assign(
           __assign_str(host, host);
           __entry->cmd = cmd;
           __entry->err = err;
           __entry->resp1 = resp1;
           __entry->resp2 = resp2;
           __entry->resp3 = resp3;
           __entry->resp4 = resp4;
       ),

       TP_printk("host=%s CMD%u err=%08x resp1=%08x resp2=%08x resp3=%08x resp4=%08x",
               __get_str(host), __entry->cmd,
               __entry->err, __entry->resp1,
               __entry->resp2, __entry->resp3,
               __entry->resp4 )
);

DEFINE_EVENT(req_done, mmc_req_done,
       TP_PROTO(const char *host, unsigned int cmd,
               int err, unsigned int resp1,
               unsigned int resp2, unsigned int resp3,
               unsigned int resp4),
       TP_ARGS(host, cmd, err, resp1, resp2, resp3, resp4)
);
#endif /* if !defined(_TRACE_MMC_H) || defined(TRACE_HEADER_MULTI_READ) */

/* This part must be outside protection */
#include <trace/define_trace.h>
