/* SPDX-License-Identifier: ((GPL-2.0 WITH Linux-syscall-note) OR BSD-2-Clause) */
/*
 * Copyright 2018-2019 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef __EFA_EVERBS_H__
#define __EFA_EVERBS_H__

#include <linux/types.h>

enum efa_everbs_commands {
	EFA_EVERBS_CMD_GET_AH = 1,
	EFA_EVERBS_CMD_MAX,
};

struct efa_everbs_get_ah {
	__u32 command;
	__u16 in_words;
	__u16 out_words;
	__u32 comp_mask;
	__u16 pdn;
	__u8 reserved_30[2];
	__aligned_u64 response;
	__aligned_u64 user_handle;
	__u8 gid[16];
};

struct efa_everbs_get_ah_resp {
	__u32 comp_mask;
	__u16 efa_address_handle;
	__u8 reserved_30[2];
};

#endif /* __EFA_EVERBS_H__ */
