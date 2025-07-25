/*
 * Copyright (c) 2007 Cisco, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <config.h>

#include <endian.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <sys/mman.h>

#include <util/mmio.h>

#include "mlx4.h"
#include "mlx4-abi.h"

int mlx4_query_device_ex(struct ibv_context *context,
			 const struct ibv_query_device_ex_input *input,
			 struct ibv_device_attr_ex *attr,
			 size_t attr_size)
{
	struct mlx4_query_device_ex_resp resp = {};
	size_t resp_size = sizeof(resp);
	uint64_t raw_fw_ver;
	unsigned sub_minor;
	unsigned major;
	unsigned minor;
	int err;

	err = ibv_cmd_query_device_any(context, input, attr, attr_size,
				       &resp.ibv_resp, &resp_size);
	if (err)
		return err;

	if (attr_size >= offsetofend(struct ibv_device_attr_ex, rss_caps)) {
		attr->rss_caps.rx_hash_fields_mask =
			resp.rss_caps.rx_hash_fields_mask;
		attr->rss_caps.rx_hash_function =
			resp.rss_caps.rx_hash_function;
	}
	if (attr_size >= offsetofend(struct ibv_device_attr_ex, tso_caps)) {
		attr->tso_caps.max_tso = resp.tso_caps.max_tso;
		attr->tso_caps.supported_qpts = resp.tso_caps.supported_qpts;
	}

	raw_fw_ver = resp.ibv_resp.base.fw_ver;
	major     = (raw_fw_ver >> 32) & 0xffff;
	minor     = (raw_fw_ver >> 16) & 0xffff;
	sub_minor = raw_fw_ver & 0xffff;

	snprintf(attr->orig_attr.fw_ver, sizeof attr->orig_attr.fw_ver,
		 "%d.%d.%03d", major, minor, sub_minor);

	return 0;
}

void mlx4_query_device_ctx(struct mlx4_device *mdev, struct mlx4_context *mctx)
{
	struct ibv_device_attr_ex device_attr;
	struct mlx4_query_device_ex_resp resp;
	size_t resp_size = sizeof(resp);

	if (ibv_cmd_query_device_any(&mctx->ibv_ctx.context, NULL,
				       &device_attr, sizeof(device_attr),
				       &resp.ibv_resp, &resp_size))
		return;

	mctx->max_qp_wr = device_attr.orig_attr.max_qp_wr;
	mctx->max_sge = device_attr.orig_attr.max_sge;
	mctx->max_inl_recv_sz = resp.max_inl_recv_sz;

	if (resp.comp_mask & MLX4_IB_QUERY_DEV_RESP_MASK_CORE_CLOCK_OFFSET) {
		void *hca_clock_page;

		mctx->core_clock.offset = resp.hca_core_clock_offset;
		mctx->core_clock.offset_valid = 1;

		hca_clock_page =
			mmap(NULL, mdev->page_size, PROT_READ, MAP_SHARED,
			     mctx->ibv_ctx.context.cmd_fd, mdev->page_size * 3);
		if (hca_clock_page != MAP_FAILED)
			mctx->hca_core_clock =
				hca_clock_page + (mctx->core_clock.offset &
						  (mdev->page_size - 1));
		else
			fprintf(stderr, PFX
				"Warning: Timestamp available,\n"
				"but failed to mmap() hca core clock page.\n");
	}
}

static int mlx4_read_clock(struct ibv_context *context, uint64_t *cycles)
{
	uint32_t clockhi, clocklo, clockhi1;
	int i;
	struct mlx4_context *ctx = to_mctx(context);

	if (!ctx->hca_core_clock)
		return EOPNOTSUPP;

	/* Handle wraparound */
	for (i = 0; i < 2; i++) {
		clockhi = be32toh(mmio_read32_be(ctx->hca_core_clock));
		clocklo = be32toh(mmio_read32_be(ctx->hca_core_clock + 4));
		clockhi1 = be32toh(mmio_read32_be(ctx->hca_core_clock));
		if (clockhi == clockhi1)
			break;
	}

	*cycles = (uint64_t)clockhi << 32 | (uint64_t)clocklo;

	return 0;
}

int mlx4_query_rt_values(struct ibv_context *context,
			 struct ibv_values_ex *values)
{
	uint32_t comp_mask = 0;
	int err = 0;

	if (!check_comp_mask(values->comp_mask, IBV_VALUES_MASK_RAW_CLOCK))
		return EINVAL;

	if (values->comp_mask & IBV_VALUES_MASK_RAW_CLOCK) {
		uint64_t cycles;

		err = mlx4_read_clock(context, &cycles);
		if (!err) {
			values->raw_clock.tv_sec = 0;
			values->raw_clock.tv_nsec = cycles;
			comp_mask |= IBV_VALUES_MASK_RAW_CLOCK;
		}
	}

	values->comp_mask = comp_mask;

	return err;
}

int mlx4_query_port(struct ibv_context *context, uint8_t port,
		     struct ibv_port_attr *attr)
{
	struct ibv_query_port cmd;
	int err;

	err = ibv_cmd_query_port(context, port, attr, &cmd, sizeof(cmd));
	if (!err && port <= MLX4_PORTS_NUM && port > 0) {
		struct mlx4_context *mctx = to_mctx(context);
		if (!mctx->port_query_cache[port - 1].valid) {
			mctx->port_query_cache[port - 1].link_layer =
				attr->link_layer;
			mctx->port_query_cache[port - 1].caps =
				attr->port_cap_flags;
			mctx->port_query_cache[port - 1].flags =
				attr->flags;
			mctx->port_query_cache[port - 1].valid = 1;
		}
	}

	return err;
}

/* Only the fields in the port cache will be valid */
static int query_port_cache(struct ibv_context *context, uint8_t port_num,
			    struct ibv_port_attr *port_attr)
{
	struct mlx4_context *mctx = to_mctx(context);
	if (port_num <= 0 || port_num > MLX4_PORTS_NUM)
		return -EINVAL;
	if (mctx->port_query_cache[port_num - 1].valid) {
		port_attr->link_layer =
			mctx->
			port_query_cache[port_num - 1].
			link_layer;
		port_attr->port_cap_flags =
			mctx->
			port_query_cache[port_num - 1].
			caps;
		port_attr->flags =
			mctx->
			port_query_cache[port_num - 1].
			flags;
		return 0;
	}
	return mlx4_query_port(context, port_num,
			       (struct ibv_port_attr *)port_attr);

}

struct ibv_pd *mlx4_alloc_pd(struct ibv_context *context)
{
	struct ibv_alloc_pd       cmd;
	struct mlx4_alloc_pd_resp resp;
	struct mlx4_pd		 *pd;

	pd = malloc(sizeof *pd);
	if (!pd)
		return NULL;

	if (ibv_cmd_alloc_pd(context, &pd->ibv_pd, &cmd, sizeof cmd,
			     &resp.ibv_resp, sizeof resp)) {
		free(pd);
		return NULL;
	}

	pd->pdn = resp.pdn;

	return &pd->ibv_pd;
}

int mlx4_free_pd(struct ibv_pd *pd)
{
	int ret;

	ret = ibv_cmd_dealloc_pd(pd);
	if (ret)
		return ret;

	free(to_mpd(pd));
	return 0;
}

struct ibv_xrcd *mlx4_open_xrcd(struct ibv_context *context,
				struct ibv_xrcd_init_attr *attr)
{
	struct ibv_open_xrcd cmd;
	struct ib_uverbs_open_xrcd_resp resp;
	struct verbs_xrcd *xrcd;
	int ret;

	xrcd = calloc(1, sizeof *xrcd);
	if (!xrcd)
		return NULL;

	ret = ibv_cmd_open_xrcd(context, xrcd, sizeof(*xrcd), attr,
				&cmd, sizeof cmd, &resp, sizeof resp);
	if (ret)
		goto err;

	return &xrcd->xrcd;

err:
	free(xrcd);
	return NULL;
}

int mlx4_close_xrcd(struct ibv_xrcd *ib_xrcd)
{
	struct verbs_xrcd *xrcd = container_of(ib_xrcd, struct verbs_xrcd, xrcd);
	int ret;

	ret = ibv_cmd_close_xrcd(xrcd);
	if (ret)
		return ret;

	free(xrcd);
	return 0;
}

int mlx4_get_srq_num(struct ibv_srq *srq, uint32_t *srq_num)
{
	struct mlx4_srq *msrq =
		container_of(srq, struct mlx4_srq, verbs_srq.srq);

	if (!msrq->verbs_srq.xrcd)
		return EOPNOTSUPP;
	*srq_num = msrq->verbs_srq.srq_num;
	return 0;
}

struct ibv_mr *mlx4_reg_mr(struct ibv_pd *pd, void *addr, size_t length,
			   uint64_t hca_va, int access)
{
	struct verbs_mr *vmr;
	struct ibv_reg_mr cmd;
	struct ib_uverbs_reg_mr_resp resp;
	int ret;

	vmr = malloc(sizeof(*vmr));
	if (!vmr)
		return NULL;

	ret = ibv_cmd_reg_mr(pd, addr, length, hca_va, access, vmr, &cmd,
			     sizeof(cmd), &resp, sizeof(resp));
	if (ret) {
		free(vmr);
		return NULL;
	}

	return &vmr->ibv_mr;
}

int mlx4_rereg_mr(struct verbs_mr *vmr,
		  int flags,
		  struct ibv_pd *pd, void *addr,
		  size_t length, int access)
{
	struct ibv_rereg_mr cmd;
	struct ib_uverbs_rereg_mr_resp resp;

	return ibv_cmd_rereg_mr(vmr, flags, addr, length,
				(uintptr_t)addr,
				access, pd,
				&cmd, sizeof(cmd),
				&resp, sizeof(resp));
}

int mlx4_dereg_mr(struct verbs_mr *vmr)
{
	int ret;

	ret = ibv_cmd_dereg_mr(vmr);
	if (ret)
		return ret;

	free(vmr);
	return 0;
}

struct ibv_mw *mlx4_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type)
{
	struct ibv_mw *mw;
	struct ibv_alloc_mw cmd;
	struct ib_uverbs_alloc_mw_resp resp;
	int ret;

	mw = calloc(1, sizeof(*mw));
	if (!mw)
		return NULL;

	ret = ibv_cmd_alloc_mw(pd, type, mw, &cmd, sizeof(cmd),
			     &resp, sizeof(resp));

	if (ret) {
		free(mw);
		return NULL;
	}

	return mw;
}

int mlx4_dealloc_mw(struct ibv_mw *mw)
{
	int ret;

	ret = ibv_cmd_dealloc_mw(mw);
	if (ret)
		return ret;

	free(mw);
	return 0;
}

int mlx4_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
		 struct ibv_mw_bind *mw_bind)
{
	struct ibv_send_wr *bad_wr = NULL;
	struct ibv_send_wr wr = { };
	int ret;


	wr.opcode = IBV_WR_BIND_MW;
	wr.next = NULL;

	wr.wr_id = mw_bind->wr_id;
	wr.send_flags = mw_bind->send_flags;

	wr.bind_mw.mw = mw;
	wr.bind_mw.rkey = ibv_inc_rkey(mw->rkey);
	wr.bind_mw.bind_info = mw_bind->bind_info;

	ret = mlx4_post_send(qp, &wr, &bad_wr);

	if (ret)
		return ret;

	/* updating the mw with the latest rkey. */
	mw->rkey = wr.bind_mw.rkey;

	return 0;
}

enum {
	CREATE_CQ_SUPPORTED_WC_FLAGS = IBV_WC_STANDARD_FLAGS	|
				       IBV_WC_EX_WITH_COMPLETION_TIMESTAMP
};

enum {
	CREATE_CQ_SUPPORTED_COMP_MASK = IBV_CQ_INIT_ATTR_MASK_FLAGS
};

enum {
	CREATE_CQ_SUPPORTED_FLAGS = IBV_CREATE_CQ_ATTR_SINGLE_THREADED
};


static int mlx4_cmd_create_cq(struct ibv_context *context,
			      struct ibv_cq_init_attr_ex *cq_attr,
			      struct mlx4_cq *cq)
{
	struct mlx4_create_cq      cmd;
	struct mlx4_create_cq_resp resp;
	int ret;

	cmd.buf_addr = (uintptr_t) cq->buf.buf;
	cmd.db_addr  = (uintptr_t) cq->set_ci_db;

	ret = ibv_cmd_create_cq(context, cq_attr->cqe, cq_attr->channel,
				cq_attr->comp_vector,
				&cq->verbs_cq.cq,
				&cmd.ibv_cmd, sizeof(cmd),
				&resp.ibv_resp, sizeof(resp));
	if (!ret)
		cq->cqn = resp.cqn;

	return ret;

}

static int mlx4_cmd_create_cq_ex(struct ibv_context *context,
				 struct ibv_cq_init_attr_ex *cq_attr,
				 struct mlx4_cq *cq)
{
	struct mlx4_create_cq_ex      cmd;
	struct mlx4_create_cq_ex_resp resp = {};
	int ret;

	cmd.buf_addr = (uintptr_t) cq->buf.buf;
	cmd.db_addr  = (uintptr_t) cq->set_ci_db;

	ret = ibv_cmd_create_cq_ex(context, cq_attr, NULL,
				   &cq->verbs_cq, &cmd.ibv_cmd,
				   sizeof(cmd),
				   &resp.ibv_resp,
				   sizeof(resp),
				   0);
	if (!ret)
		cq->cqn = resp.cqn;

	return ret;
}

static struct ibv_cq_ex *create_cq(struct ibv_context *context,
				   struct ibv_cq_init_attr_ex *cq_attr,
				   int cq_alloc_flags)
{
	struct mlx4_cq      *cq;
	int                  ret;
	struct mlx4_context *mctx = to_mctx(context);

	/* Sanity check CQ size before proceeding */
	if (cq_attr->cqe > 0x3fffff) {
		errno = EINVAL;
		return NULL;
	}

	if (cq_attr->comp_mask & ~CREATE_CQ_SUPPORTED_COMP_MASK) {
		errno = ENOTSUP;
		return NULL;
	}

	if (cq_attr->comp_mask & IBV_CQ_INIT_ATTR_MASK_FLAGS &&
	    cq_attr->flags & ~CREATE_CQ_SUPPORTED_FLAGS) {
		errno = ENOTSUP;
		return NULL;
	}

	if (cq_attr->wc_flags & ~CREATE_CQ_SUPPORTED_WC_FLAGS) {
		errno = ENOTSUP;
		return NULL;
	}

	/* mlx4 devices don't support slid and sl in cqe when completion
	 * timestamp is enabled in the CQ
	*/
	if ((cq_attr->wc_flags & (IBV_WC_EX_WITH_SLID | IBV_WC_EX_WITH_SL)) &&
	    (cq_attr->wc_flags & IBV_WC_EX_WITH_COMPLETION_TIMESTAMP)) {
		errno = ENOTSUP;
		return NULL;
	}

	cq = malloc(sizeof *cq);
	if (!cq)
		return NULL;

	cq->cons_index = 0;

	if (pthread_spin_init(&cq->lock, PTHREAD_PROCESS_PRIVATE))
		goto err;

	cq_attr->cqe = roundup_pow_of_two(cq_attr->cqe + 1);

	if (mlx4_alloc_cq_buf(to_mdev(context->device), mctx, &cq->buf,
			      cq_attr->cqe, mctx->cqe_size))
		goto err;

	cq->cqe_size = mctx->cqe_size;
	cq->set_ci_db  = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_CQ);
	if (!cq->set_ci_db)
		goto err_buf;

	cq->arm_db     = cq->set_ci_db + 1;
	*cq->arm_db    = 0;
	cq->arm_sn     = 1;
	*cq->set_ci_db = 0;
	cq->flags = cq_alloc_flags;

	if (cq_attr->comp_mask & IBV_CQ_INIT_ATTR_MASK_FLAGS &&
	    cq_attr->flags & IBV_CREATE_CQ_ATTR_SINGLE_THREADED)
		cq->flags |= MLX4_CQ_FLAGS_SINGLE_THREADED;

	--cq_attr->cqe;
	if (cq_alloc_flags & MLX4_CQ_FLAGS_EXTENDED)
		ret = mlx4_cmd_create_cq_ex(context, cq_attr, cq);
	else
		ret = mlx4_cmd_create_cq(context, cq_attr, cq);

	if (ret)
		goto err_db;


	if (cq_alloc_flags & MLX4_CQ_FLAGS_EXTENDED)
		mlx4_cq_fill_pfns(cq, cq_attr);

	return &cq->verbs_cq.cq_ex;

err_db:
	mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_CQ, cq->set_ci_db);

err_buf:
	mlx4_free_buf(to_mctx(context), &cq->buf);

err:
	free(cq);

	return NULL;
}

struct ibv_cq *mlx4_create_cq(struct ibv_context *context, int cqe,
			      struct ibv_comp_channel *channel,
			      int comp_vector)
{
	struct ibv_cq_ex *cq;
	struct ibv_cq_init_attr_ex cq_attr = {.cqe = cqe, .channel = channel,
					      .comp_vector = comp_vector,
					      .wc_flags = IBV_WC_STANDARD_FLAGS};

	cq = create_cq(context, &cq_attr, 0);
	return cq ? ibv_cq_ex_to_cq(cq) : NULL;
}

struct ibv_cq_ex *mlx4_create_cq_ex(struct ibv_context *context,
				    struct ibv_cq_init_attr_ex *cq_attr)
{
	/*
	 * Make local copy since some attributes might be adjusted
	 * for internal use.
	 */
	struct ibv_cq_init_attr_ex cq_attr_c = {.cqe = cq_attr->cqe,
						.channel = cq_attr->channel,
						.comp_vector = cq_attr->comp_vector,
						.wc_flags = cq_attr->wc_flags,
						.comp_mask = cq_attr->comp_mask,
						.flags = cq_attr->flags};

	if (!check_comp_mask(cq_attr_c.comp_mask,
			     IBV_CQ_INIT_ATTR_MASK_FLAGS)) {
		errno = EINVAL;
		return NULL;
	}

	return create_cq(context, &cq_attr_c, MLX4_CQ_FLAGS_EXTENDED);
}

int mlx4_resize_cq(struct ibv_cq *ibcq, int cqe)
{
	struct mlx4_cq *cq = to_mcq(ibcq);
	struct mlx4_resize_cq cmd;
	struct ib_uverbs_resize_cq_resp resp;
	struct mlx4_buf buf;
	int old_cqe, outst_cqe, ret;

	/* Sanity check CQ size before proceeding */
	if (cqe > 0x3fffff)
		return EINVAL;

	pthread_spin_lock(&cq->lock);

	cqe = roundup_pow_of_two(cqe + 1);
	if (cqe == ibcq->cqe + 1) {
		ret = 0;
		goto out;
	}

	/* Can't be smaller then the number of outstanding CQEs */
	outst_cqe = mlx4_get_outstanding_cqes(cq);
	if (cqe < outst_cqe + 1) {
		ret = EINVAL;
		goto out;
	}

	ret = mlx4_alloc_cq_buf(to_mdev(ibcq->context->device),
				to_mctx(ibcq->context), &buf, cqe,
				cq->cqe_size);
	if (ret)
		goto out;

	old_cqe = ibcq->cqe;
	cmd.buf_addr = (uintptr_t) buf.buf;

	ret = ibv_cmd_resize_cq(ibcq, cqe - 1, &cmd.ibv_cmd, sizeof cmd,
				&resp, sizeof resp);
	if (ret) {
		mlx4_free_buf(to_mctx(ibcq->context), &buf);
		goto out;
	}

	mlx4_cq_resize_copy_cqes(cq, buf.buf, old_cqe);

	mlx4_free_buf(to_mctx(ibcq->context), &cq->buf);
	cq->buf = buf;
	mlx4_update_cons_index(cq);

out:
	pthread_spin_unlock(&cq->lock);
	return ret;
}

int mlx4_destroy_cq(struct ibv_cq *cq)
{
	int ret;

	ret = ibv_cmd_destroy_cq(cq);
	if (ret)
		return ret;

	mlx4_free_db(to_mctx(cq->context), MLX4_DB_TYPE_CQ, to_mcq(cq)->set_ci_db);
	mlx4_free_buf(to_mctx(cq->context), &to_mcq(cq)->buf);
	free(to_mcq(cq));

	return 0;
}

struct ibv_srq *mlx4_create_srq(struct ibv_pd *pd,
				struct ibv_srq_init_attr *attr)
{
	struct mlx4_create_srq      cmd;
	struct mlx4_create_srq_resp resp;
	struct mlx4_srq		   *srq;
	int			    ret;

	/* Sanity check SRQ size before proceeding */
	if (attr->attr.max_wr > 1 << 16 || attr->attr.max_sge > 64) {
		errno = EINVAL;
		return NULL;
	}

	srq = malloc(sizeof *srq);
	if (!srq)
		return NULL;

	if (pthread_spin_init(&srq->lock, PTHREAD_PROCESS_PRIVATE))
		goto err;

	srq->max     = roundup_pow_of_two(attr->attr.max_wr + 1);
	srq->max_gs  = attr->attr.max_sge;
	srq->counter = 0;
	srq->ext_srq = 0;

	if (mlx4_alloc_srq_buf(pd, &attr->attr, srq))
		goto err;

	srq->db = mlx4_alloc_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ);
	if (!srq->db)
		goto err_free;

	*srq->db = 0;

	cmd.buf_addr = (uintptr_t) srq->buf.buf;
	cmd.db_addr  = (uintptr_t) srq->db;

	ret = ibv_cmd_create_srq(pd, &srq->verbs_srq.srq, attr,
				 &cmd.ibv_cmd, sizeof cmd,
				 &resp.ibv_resp, sizeof resp);
	if (ret)
		goto err_db;

	return &srq->verbs_srq.srq;

err_db:
	mlx4_free_db(to_mctx(pd->context), MLX4_DB_TYPE_RQ, srq->db);

err_free:
	free(srq->wrid);
	mlx4_free_buf(to_mctx(pd->context), &srq->buf);

err:
	free(srq);

	return NULL;
}

struct ibv_srq *mlx4_create_srq_ex(struct ibv_context *context,
				   struct ibv_srq_init_attr_ex *attr_ex)
{
	if (!(attr_ex->comp_mask & IBV_SRQ_INIT_ATTR_TYPE) ||
	    (attr_ex->srq_type == IBV_SRQT_BASIC))
		return mlx4_create_srq(attr_ex->pd, (struct ibv_srq_init_attr *) attr_ex);
	else if (attr_ex->srq_type == IBV_SRQT_XRC)
		return mlx4_create_xrc_srq(context, attr_ex);

	return NULL;
}

int mlx4_modify_srq(struct ibv_srq *srq,
		     struct ibv_srq_attr *attr,
		     int attr_mask)
{
	struct ibv_modify_srq cmd;

	return ibv_cmd_modify_srq(srq, attr, attr_mask, &cmd, sizeof cmd);
}

int mlx4_query_srq(struct ibv_srq *srq,
		    struct ibv_srq_attr *attr)
{
	struct ibv_query_srq cmd;

	return ibv_cmd_query_srq(srq, attr, &cmd, sizeof cmd);
}

int mlx4_destroy_srq(struct ibv_srq *srq)
{
	int ret;

	if (to_msrq(srq)->ext_srq)
		return mlx4_destroy_xrc_srq(srq);

	ret = ibv_cmd_destroy_srq(srq);
	if (ret)
		return ret;

	mlx4_free_db(to_mctx(srq->context), MLX4_DB_TYPE_RQ, to_msrq(srq)->db);
	mlx4_free_buf(to_mctx(srq->context), &to_msrq(srq)->buf);
	free(to_msrq(srq)->wrid);
	free(to_msrq(srq));

	return 0;
}

static int mlx4_cmd_create_qp_ex_rss(struct ibv_context *context,
				     struct ibv_qp_init_attr_ex *attr,
				     struct mlx4_create_qp *cmd,
				     struct mlx4_qp *qp)
{
	struct mlx4_create_qp_ex_rss cmd_ex = {};
	struct mlx4_create_qp_ex_resp resp;
	int ret;

	if (attr->rx_hash_conf.rx_hash_key_len !=
	    sizeof(cmd_ex.rx_hash_key)) {
		errno = ENOTSUP;
		return errno;
	}

	cmd_ex.rx_hash_fields_mask =
		attr->rx_hash_conf.rx_hash_fields_mask;
	cmd_ex.rx_hash_function =
		attr->rx_hash_conf.rx_hash_function;
	memcpy(cmd_ex.rx_hash_key, attr->rx_hash_conf.rx_hash_key,
	       sizeof(cmd_ex.rx_hash_key));

	ret = ibv_cmd_create_qp_ex2(context, &qp->verbs_qp,
				    attr, &cmd_ex.ibv_cmd,
				    sizeof(cmd_ex), &resp.ibv_resp,
				    sizeof(resp));
	return ret;
}

static struct ibv_qp *_mlx4_create_qp_ex_rss(struct ibv_context *context,
					     struct ibv_qp_init_attr_ex *attr)
{
	struct mlx4_create_qp cmd = {};
	struct mlx4_qp *qp;
	int ret;

	if (!(attr->comp_mask & IBV_QP_INIT_ATTR_RX_HASH) ||
	    !(attr->comp_mask & IBV_QP_INIT_ATTR_IND_TABLE)) {
		errno = EINVAL;
		return NULL;
	}

	if (attr->qp_type != IBV_QPT_RAW_PACKET) {
		errno = EINVAL;
		return NULL;
	}

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	if (pthread_spin_init(&qp->sq.lock, PTHREAD_PROCESS_PRIVATE) ||
	    pthread_spin_init(&qp->rq.lock, PTHREAD_PROCESS_PRIVATE))
		goto err;

	ret = mlx4_cmd_create_qp_ex_rss(context, attr, &cmd, qp);
	if (ret)
		goto err;

	qp->type = MLX4_RSC_TYPE_RSS_QP;

	return &qp->verbs_qp.qp;
err:
	free(qp);
	return NULL;
}

static int mlx4_cmd_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr,
				 struct mlx4_create_qp *cmd,
				 struct mlx4_qp *qp)
{
	struct mlx4_create_qp_ex cmd_ex;
	struct mlx4_create_qp_ex_resp resp;
	int ret;

	memset(&cmd_ex, 0, sizeof(cmd_ex));
	*ibv_create_qp_ex_to_reg(&cmd_ex.ibv_cmd) = cmd->ibv_cmd.core_payload;

	cmd_ex.drv_payload = cmd->drv_payload;

	ret = ibv_cmd_create_qp_ex2(context, &qp->verbs_qp,
				    attr, &cmd_ex.ibv_cmd,
				    sizeof(cmd_ex), &resp.ibv_resp,
				    sizeof(resp));
	return ret;
}

enum {
	MLX4_CREATE_QP_SUP_COMP_MASK = (IBV_QP_INIT_ATTR_PD |
					IBV_QP_INIT_ATTR_XRCD |
					IBV_QP_INIT_ATTR_CREATE_FLAGS |
					IBV_QP_INIT_ATTR_MAX_TSO_HEADER),
};

enum {
	MLX4_CREATE_QP_EX2_COMP_MASK = (IBV_QP_INIT_ATTR_CREATE_FLAGS |
					IBV_QP_INIT_ATTR_MAX_TSO_HEADER),
};

static struct ibv_qp *create_qp_ex(struct ibv_context *context,
				   struct ibv_qp_init_attr_ex *attr,
				   struct mlx4dv_qp_init_attr *mlx4qp_attr)
{
	struct mlx4_context *ctx = to_mctx(context);
	struct mlx4_create_qp     cmd = {};
	struct ib_uverbs_create_qp_resp resp = {};
	struct mlx4_qp		 *qp;
	int			  ret;

	if (attr->comp_mask & (IBV_QP_INIT_ATTR_RX_HASH |
			       IBV_QP_INIT_ATTR_IND_TABLE)) {
		return _mlx4_create_qp_ex_rss(context, attr);
	}

	/* Sanity check QP size before proceeding */
	if (ctx->max_qp_wr) { /* mlx4_query_device succeeded */
		if (attr->cap.max_send_wr  > ctx->max_qp_wr ||
		    attr->cap.max_recv_wr  > ctx->max_qp_wr ||
		    attr->cap.max_send_sge > ctx->max_sge   ||
		    attr->cap.max_recv_sge > ctx->max_sge) {
			errno = EINVAL;
			return NULL;
		}
	} else {
		if (attr->cap.max_send_wr  > 65536 ||
		    attr->cap.max_recv_wr  > 65536 ||
		    attr->cap.max_send_sge > 64    ||
		    attr->cap.max_recv_sge > 64) {
			errno = EINVAL;
			return NULL;
		}
	}
	if (attr->cap.max_inline_data > 1024) {
		errno = EINVAL;
		return NULL;
	}

	if (attr->comp_mask & ~MLX4_CREATE_QP_SUP_COMP_MASK) {
		errno = ENOTSUP;
		return NULL;
	}

	qp = calloc(1, sizeof *qp);
	if (!qp)
		return NULL;

	if (attr->qp_type == IBV_QPT_XRC_RECV) {
		attr->cap.max_send_wr = qp->sq.wqe_cnt = 0;
	} else {
		mlx4_calc_sq_wqe_size(&attr->cap, attr->qp_type, qp, attr);
		/*
		 * We need to leave 2 KB + 1 WQE of headroom in the SQ to
		 * allow HW to prefetch.
		 */
		qp->sq_spare_wqes = (2048 >> qp->sq.wqe_shift) + 1;
		qp->sq.wqe_cnt = roundup_pow_of_two(attr->cap.max_send_wr + qp->sq_spare_wqes);
	}

	if (attr->srq || attr->qp_type == IBV_QPT_XRC_SEND ||
	    attr->qp_type == IBV_QPT_XRC_RECV) {
		attr->cap.max_recv_wr = qp->rq.wqe_cnt = attr->cap.max_recv_sge = 0;
	} else {
		qp->rq.wqe_cnt = roundup_pow_of_two(attr->cap.max_recv_wr);
		if (attr->cap.max_recv_sge < 1)
			attr->cap.max_recv_sge = 1;
		if (attr->cap.max_recv_wr < 1)
			attr->cap.max_recv_wr = 1;
	}

	if (mlx4_alloc_qp_buf(context, attr->cap.max_recv_sge, attr->qp_type, qp,
			      mlx4qp_attr))
		goto err;

	mlx4_init_qp_indices(qp);

	if (pthread_spin_init(&qp->sq.lock, PTHREAD_PROCESS_PRIVATE) ||
	    pthread_spin_init(&qp->rq.lock, PTHREAD_PROCESS_PRIVATE))
		goto err_free;

	if (mlx4qp_attr) {
		if (!check_comp_mask(mlx4qp_attr->comp_mask,
		    MLX4DV_QP_INIT_ATTR_MASK_RESERVED - 1)) {
			errno = EINVAL;
			goto err_free;
		}
		if (mlx4qp_attr->comp_mask & MLX4DV_QP_INIT_ATTR_MASK_INL_RECV)
			cmd.inl_recv_sz = mlx4qp_attr->inl_recv_sz;
	}
	if (attr->cap.max_recv_sge) {
		qp->db = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_RQ);
		if (!qp->db)
			goto err_free;

		*qp->db = 0;
		cmd.db_addr = (uintptr_t) qp->db;
	} else {
		cmd.db_addr = 0;
	}

	cmd.buf_addr	    = (uintptr_t) qp->buf.buf;
	cmd.log_sq_stride   = qp->sq.wqe_shift;
	for (cmd.log_sq_bb_count = 0;
	     qp->sq.wqe_cnt > 1 << cmd.log_sq_bb_count;
	     ++cmd.log_sq_bb_count)
		; /* nothing */
	cmd.sq_no_prefetch = 0;	/* OK for ABI 2: just a reserved field */
	pthread_mutex_lock(&to_mctx(context)->qp_table_mutex);


	if (attr->comp_mask & MLX4_CREATE_QP_EX2_COMP_MASK)
		ret = mlx4_cmd_create_qp_ex(context, attr, &cmd, qp);
	else
		ret = ibv_cmd_create_qp_ex(context, &qp->verbs_qp,
					   attr,
					   &cmd.ibv_cmd, sizeof(cmd), &resp,
					   sizeof(resp));
	if (ret)
		goto err_rq_db;

	if (qp->sq.wqe_cnt || qp->rq.wqe_cnt) {
		ret = mlx4_store_qp(to_mctx(context), qp->verbs_qp.qp.qp_num, qp);
		if (ret)
			goto err_destroy;
	}
	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);

	qp->rq.wqe_cnt = qp->rq.max_post = attr->cap.max_recv_wr;
	qp->rq.max_gs  = attr->cap.max_recv_sge;
	if (attr->qp_type != IBV_QPT_XRC_RECV)
		mlx4_set_sq_sizes(qp, &attr->cap, attr->qp_type);

	qp->doorbell_qpn    = htobe32(qp->verbs_qp.qp.qp_num << 8);
	if (attr->sq_sig_all)
		qp->sq_signal_bits = htobe32(MLX4_WQE_CTRL_CQ_UPDATE);
	else
		qp->sq_signal_bits = 0;

	qp->qpn_cache = qp->verbs_qp.qp.qp_num;
	qp->type = attr->srq ? MLX4_RSC_TYPE_SRQ : MLX4_RSC_TYPE_QP;

	return &qp->verbs_qp.qp;

err_destroy:
	ibv_cmd_destroy_qp(&qp->verbs_qp.qp);

err_rq_db:
	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);
	if (attr->cap.max_recv_sge)
		mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_RQ, qp->db);

err_free:
	free(qp->sq.wrid);
	if (qp->rq.wqe_cnt)
		free(qp->rq.wrid);
	mlx4_free_buf(ctx, &qp->buf);

err:
	free(qp);

	return NULL;
}

struct ibv_qp *mlx4_create_qp_ex(struct ibv_context *context,
				 struct ibv_qp_init_attr_ex *attr)
{
	return create_qp_ex(context, attr, NULL);
}

struct ibv_qp *mlx4dv_create_qp(struct ibv_context *context,
				struct ibv_qp_init_attr_ex *attr,
				struct mlx4dv_qp_init_attr *mlx4_qp_attr)
{
	return create_qp_ex(context, attr, mlx4_qp_attr);
}

struct ibv_qp *mlx4_create_qp(struct ibv_pd *pd, struct ibv_qp_init_attr *attr)
{
	struct ibv_qp_init_attr_ex attr_ex;
	struct ibv_qp *qp;

	memcpy(&attr_ex, attr, sizeof *attr);
	attr_ex.comp_mask = IBV_QP_INIT_ATTR_PD;
	attr_ex.pd = pd;
	qp = mlx4_create_qp_ex(pd->context, &attr_ex);
	if (qp)
		memcpy(attr, &attr_ex, sizeof *attr);
	return qp;
}

struct ibv_qp *mlx4_open_qp(struct ibv_context *context, struct ibv_qp_open_attr *attr)
{
	struct ibv_open_qp cmd;
	struct ib_uverbs_create_qp_resp resp;
	struct mlx4_qp *qp;
	int ret;

	qp = calloc(1, sizeof *qp);
	if (!qp)
		return NULL;

	ret = ibv_cmd_open_qp(context, &qp->verbs_qp, sizeof(qp->verbs_qp), attr,
			      &cmd, sizeof cmd, &resp, sizeof resp);
	if (ret)
		goto err;

	return &qp->verbs_qp.qp;

err:
	free(qp);
	return NULL;
}

int mlx4_query_qp(struct ibv_qp *ibqp, struct ibv_qp_attr *attr,
		   int attr_mask,
		   struct ibv_qp_init_attr *init_attr)
{
	struct ibv_query_qp cmd;
	struct mlx4_qp *qp = to_mqp(ibqp);
	int ret;

	if (qp->type == MLX4_RSC_TYPE_RSS_QP)
		return ENOTSUP;

	ret = ibv_cmd_query_qp(ibqp, attr, attr_mask, init_attr, &cmd, sizeof cmd);
	if (ret)
		return ret;

	init_attr->cap.max_send_wr     = qp->sq.max_post;
	init_attr->cap.max_send_sge    = qp->sq.max_gs;
	init_attr->cap.max_inline_data = qp->max_inline_data;

	attr->cap = init_attr->cap;

	return 0;
}

static int _mlx4_modify_qp_rss(struct ibv_qp *qp, struct ibv_qp_attr *attr,
			       int attr_mask)
{
	struct ibv_modify_qp cmd = {};

	if (attr_mask & ~(IBV_QP_STATE | IBV_QP_PORT))
		return ENOTSUP;

	if (attr->qp_state > IBV_QPS_RTR)
		return ENOTSUP;

	return ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof(cmd));
}

int mlx4_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
		    int attr_mask)
{
	struct ibv_modify_qp cmd = {};
	struct ibv_port_attr port_attr;
	struct mlx4_qp *mqp = to_mqp(qp);
	struct ibv_device_attr device_attr;
	int ret;

	if (mqp->type == MLX4_RSC_TYPE_RSS_QP)
		return _mlx4_modify_qp_rss(qp, attr, attr_mask);

	memset(&device_attr, 0, sizeof(device_attr));
	if (attr_mask & IBV_QP_PORT) {
		ret = ibv_query_port(qp->context, attr->port_num,
				     &port_attr);
		if (ret)
			return ret;
		mqp->link_layer = port_attr.link_layer;

		ret = ibv_query_device(qp->context, &device_attr);
		if (ret)
			return ret;

		switch(qp->qp_type) {
		case IBV_QPT_UD:
			if ((mqp->link_layer == IBV_LINK_LAYER_INFINIBAND) &&
			    (device_attr.device_cap_flags & IBV_DEVICE_UD_IP_CSUM))
				mqp->qp_cap_cache |= MLX4_CSUM_SUPPORT_UD_OVER_IB |
						MLX4_RX_CSUM_VALID;
			break;
		case IBV_QPT_RAW_PACKET:
			if ((mqp->link_layer == IBV_LINK_LAYER_ETHERNET) &&
			    (device_attr.device_cap_flags & IBV_DEVICE_RAW_IP_CSUM))
				mqp->qp_cap_cache |= MLX4_CSUM_SUPPORT_RAW_OVER_ETH |
						MLX4_RX_CSUM_VALID;
			break;
		default:
			break;
		}

	}

	if (qp->state == IBV_QPS_RESET &&
	    attr_mask & IBV_QP_STATE   &&
	    attr->qp_state == IBV_QPS_INIT) {
		mlx4_qp_init_sq_ownership(to_mqp(qp));
	}

	ret = ibv_cmd_modify_qp(qp, attr, attr_mask, &cmd, sizeof cmd);

	if (!ret		       &&
	    (attr_mask & IBV_QP_STATE) &&
	    attr->qp_state == IBV_QPS_RESET) {
		if (qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->recv_cq), qp->qp_num,
				      qp->srq ? to_msrq(qp->srq) : NULL);
		if (qp->send_cq && qp->send_cq != qp->recv_cq)
			mlx4_cq_clean(to_mcq(qp->send_cq), qp->qp_num, NULL);

		mlx4_init_qp_indices(to_mqp(qp));
		if (to_mqp(qp)->rq.wqe_cnt)
			*to_mqp(qp)->db = 0;
	}

	return ret;
}

static void mlx4_lock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);

	if (!qp->send_cq || !qp->recv_cq) {
		if (qp->send_cq)
			pthread_spin_lock(&send_cq->lock);
		else if (qp->recv_cq)
			pthread_spin_lock(&recv_cq->lock);
	} else if (send_cq == recv_cq) {
		pthread_spin_lock(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		pthread_spin_lock(&send_cq->lock);
		pthread_spin_lock(&recv_cq->lock);
	} else {
		pthread_spin_lock(&recv_cq->lock);
		pthread_spin_lock(&send_cq->lock);
	}
}

static void mlx4_unlock_cqs(struct ibv_qp *qp)
{
	struct mlx4_cq *send_cq = to_mcq(qp->send_cq);
	struct mlx4_cq *recv_cq = to_mcq(qp->recv_cq);


	if (!qp->send_cq || !qp->recv_cq) {
		if (qp->send_cq)
			pthread_spin_unlock(&send_cq->lock);
		else if (qp->recv_cq)
			pthread_spin_unlock(&recv_cq->lock);
	} else if (send_cq == recv_cq) {
		pthread_spin_unlock(&send_cq->lock);
	} else if (send_cq->cqn < recv_cq->cqn) {
		pthread_spin_unlock(&recv_cq->lock);
		pthread_spin_unlock(&send_cq->lock);
	} else {
		pthread_spin_unlock(&send_cq->lock);
		pthread_spin_unlock(&recv_cq->lock);
	}
}

static int _mlx4_destroy_qp_rss(struct ibv_qp *ibqp)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	int ret;

	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret)
		return ret;

	free(qp);

	return 0;
}

int mlx4_destroy_qp(struct ibv_qp *ibqp)
{
	struct mlx4_qp *qp = to_mqp(ibqp);
	int ret;

	if (qp->type == MLX4_RSC_TYPE_RSS_QP)
		return _mlx4_destroy_qp_rss(ibqp);

	pthread_mutex_lock(&to_mctx(ibqp->context)->qp_table_mutex);
	ret = ibv_cmd_destroy_qp(ibqp);
	if (ret) {
		pthread_mutex_unlock(&to_mctx(ibqp->context)->qp_table_mutex);
		return ret;
	}

	mlx4_lock_cqs(ibqp);

	if (ibqp->recv_cq)
		__mlx4_cq_clean(to_mcq(ibqp->recv_cq), ibqp->qp_num,
				ibqp->srq ? to_msrq(ibqp->srq) : NULL);
	if (ibqp->send_cq && ibqp->send_cq != ibqp->recv_cq)
		__mlx4_cq_clean(to_mcq(ibqp->send_cq), ibqp->qp_num, NULL);

	if (qp->sq.wqe_cnt || qp->rq.wqe_cnt)
		mlx4_clear_qp(to_mctx(ibqp->context), ibqp->qp_num);

	mlx4_unlock_cqs(ibqp);
	pthread_mutex_unlock(&to_mctx(ibqp->context)->qp_table_mutex);

	if (qp->rq.wqe_cnt) {
		mlx4_free_db(to_mctx(ibqp->context), MLX4_DB_TYPE_RQ, qp->db);
		free(qp->rq.wrid);
	}
	if (qp->sq.wqe_cnt)
		free(qp->sq.wrid);
	mlx4_free_buf(to_mctx(ibqp->context), &qp->buf);
	free(qp);

	return 0;
}

static int link_local_gid(const union ibv_gid *gid)
{
	return gid->global.subnet_prefix == htobe64(0xfe80000000000000ULL);
}

static int is_multicast_gid(const union ibv_gid *gid)
{
	return gid->raw[0] == 0xff;
}

static uint16_t get_vlan_id(union ibv_gid *gid)
{
	uint16_t vid;
	vid = gid->raw[11] << 8 | gid->raw[12];
	return vid < 0x1000 ? vid : 0xffff;
}

static int mlx4_resolve_grh_to_l2(struct ibv_pd *pd, struct mlx4_ah *ah,
				  struct ibv_ah_attr *attr)
{
	int err, i;
	uint16_t vid;
	union ibv_gid sgid;

	if (link_local_gid(&attr->grh.dgid)) {
		memcpy(ah->mac, &attr->grh.dgid.raw[8], 3);
		memcpy(ah->mac + 3, &attr->grh.dgid.raw[13], 3);
		ah->mac[0] ^= 2;

		vid = get_vlan_id(&attr->grh.dgid);
	} else if (is_multicast_gid(&attr->grh.dgid)) {
		ah->mac[0] = 0x33;
		ah->mac[1] = 0x33;
		for (i = 2; i < 6; ++i)
			ah->mac[i] = attr->grh.dgid.raw[i + 10];

		err = ibv_query_gid(pd->context, attr->port_num,
				    attr->grh.sgid_index, &sgid);
		if (err)
			return err;

		ah->av.dlid = htobe16(0xc000);
		ah->av.port_pd |= htobe32(1 << 31);

		vid = get_vlan_id(&sgid);
	} else
		return 1;

	if (vid != 0xffff) {
		ah->av.port_pd |= htobe32(1 << 29);
		ah->vlan = vid | ((attr->sl & 7) << 13);
	}

	return 0;
}

struct ibv_ah *mlx4_create_ah(struct ibv_pd *pd, struct ibv_ah_attr *attr)
{
	struct mlx4_ah *ah;
	struct ibv_port_attr port_attr;

	if (query_port_cache(pd->context, attr->port_num, &port_attr))
		return NULL;

	if (port_attr.flags & IBV_QPF_GRH_REQUIRED &&
	    !attr->is_global) {
		errno = EINVAL;
		return NULL;
	}

	ah = malloc(sizeof *ah);
	if (!ah)
		return NULL;

	memset(&ah->av, 0, sizeof ah->av);

	ah->av.port_pd   = htobe32(to_mpd(pd)->pdn | (attr->port_num << 24));

	if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET) {
		ah->av.g_slid = attr->src_path_bits;
		ah->av.dlid   = htobe16(attr->dlid);
		ah->av.sl_tclass_flowlabel = htobe32(attr->sl << 28);
	} else
		ah->av.sl_tclass_flowlabel = htobe32(attr->sl << 29);

	if (attr->static_rate) {
		ah->av.stat_rate = attr->static_rate + MLX4_STAT_RATE_OFFSET;
		/* XXX check rate cap? */
	}
	if (attr->is_global) {
		ah->av.g_slid   |= 0x80;
		ah->av.gid_index = attr->grh.sgid_index;
		ah->av.hop_limit = attr->grh.hop_limit;
		ah->av.sl_tclass_flowlabel |=
			htobe32((attr->grh.traffic_class << 20) |
				    attr->grh.flow_label);
		memcpy(ah->av.dgid, attr->grh.dgid.raw, 16);
	}

	if (port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
		if (port_attr.port_cap_flags & IBV_PORT_IP_BASED_GIDS) {
			uint16_t vid;

			if (ibv_resolve_eth_l2_from_gid(pd->context, attr,
							ah->mac, &vid)) {
				free(ah);
				return NULL;
			}

			if (vid <= 0xfff) {
				ah->av.port_pd |= htobe32(1 << 29);
				ah->vlan = vid |
					((attr->sl & 7) << 13);
			}

		} else {
			if (mlx4_resolve_grh_to_l2(pd, ah, attr)) {
				free(ah);
				return NULL;
			}
		}
	}

	return &ah->ibv_ah;
}

int mlx4_destroy_ah(struct ibv_ah *ah)
{
	free(to_mah(ah));

	return 0;
}

struct ibv_wq *mlx4_create_wq(struct ibv_context *context,
			      struct ibv_wq_init_attr *attr)
{
	struct mlx4_context		*ctx = to_mctx(context);
	struct mlx4_create_wq		cmd = {};
	struct ib_uverbs_ex_create_wq_resp	resp = {};
	struct mlx4_qp			*qp;
	int				ret;

	if (attr->wq_type != IBV_WQT_RQ) {
		errno = ENOTSUP;
		return NULL;
	}

	/* Sanity check QP size before proceeding */
	if (ctx->max_qp_wr) { /* mlx4_query_device succeeded */
		if (attr->max_wr  > ctx->max_qp_wr ||
		    attr->max_sge > ctx->max_sge) {
			errno = EINVAL;
			return NULL;
		}
	} else {
		if (attr->max_wr  > 65536 ||
		    attr->max_sge > 64) {
			errno = EINVAL;
			return NULL;
		}
	}

	if (!check_comp_mask(attr->comp_mask, IBV_WQ_INIT_ATTR_FLAGS)) {
		errno = ENOTSUP;
		return NULL;
	}

	if ((attr->comp_mask & IBV_WQ_INIT_ATTR_FLAGS) &&
	    (attr->create_flags & ~IBV_WQ_FLAGS_SCATTER_FCS)) {
		errno = ENOTSUP;
		return NULL;
	}

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	if (attr->max_sge < 1)
		attr->max_sge = 1;

	if (attr->max_wr < 1)
		attr->max_wr = 1;

	/* Kernel driver requires a dummy SQ with minimum properties */
	qp->sq.wqe_shift = 6;
	qp->sq.wqe_cnt = 1;

	qp->rq.wqe_cnt = roundup_pow_of_two(attr->max_wr);

	if (mlx4_alloc_qp_buf(context, attr->max_sge, IBV_QPT_RAW_PACKET, qp, NULL))
		goto err;

	mlx4_init_qp_indices(qp);
	mlx4_qp_init_sq_ownership(qp); /* For dummy SQ */

	if (pthread_spin_init(&qp->rq.lock, PTHREAD_PROCESS_PRIVATE))
		goto err_free;

	qp->db = mlx4_alloc_db(to_mctx(context), MLX4_DB_TYPE_RQ);
	if (!qp->db)
		goto err_free;

	*qp->db = 0;
	cmd.db_addr = (uintptr_t)qp->db;

	cmd.buf_addr = (uintptr_t)qp->buf.buf;

	cmd.log_range_size = ctx->log_wqs_range_sz;

	pthread_mutex_lock(&to_mctx(context)->qp_table_mutex);

	ret = ibv_cmd_create_wq(context, attr, &qp->wq, &cmd.ibv_cmd,
				sizeof(cmd), &resp, sizeof(resp));
	if (ret)
		goto err_rq_db;

	ret = mlx4_store_qp(to_mctx(context), qp->wq.wq_num, qp);
	if (ret)
		goto err_destroy;

	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);

	ctx->log_wqs_range_sz = 0;

	qp->rq.max_post = attr->max_wr;
	qp->rq.wqe_cnt = attr->max_wr;
	qp->rq.max_gs  = attr->max_sge;

	qp->wq.state = IBV_WQS_RESET;

	qp->wq.post_recv = mlx4_post_wq_recv;

	qp->qpn_cache = qp->wq.wq_num;

	return &qp->wq;

err_destroy:
	ibv_cmd_destroy_wq(&qp->wq);

err_rq_db:
	pthread_mutex_unlock(&to_mctx(context)->qp_table_mutex);
	mlx4_free_db(to_mctx(context), MLX4_DB_TYPE_RQ, qp->db);

err_free:
	free(qp->rq.wrid);
	mlx4_free_buf(to_mctx(context), &qp->buf);

err:
	free(qp);

	return NULL;
}

int mlx4_modify_wq(struct ibv_wq *ibwq, struct ibv_wq_attr *attr)
{
	struct mlx4_qp *qp = wq_to_mqp(ibwq);
	struct mlx4_modify_wq cmd = {};
	int ret;

	ret = ibv_cmd_modify_wq(ibwq, attr, &cmd.ibv_cmd, sizeof(cmd));

	if (!ret && (attr->attr_mask & IBV_WQ_ATTR_STATE) &&
	    (ibwq->state == IBV_WQS_RESET)) {
		mlx4_cq_clean(to_mcq(ibwq->cq), ibwq->wq_num, NULL);

		mlx4_init_qp_indices(qp);
		*qp->db = 0;
	}

	return ret;
}

struct ibv_flow *mlx4_create_flow(struct ibv_qp *qp, struct ibv_flow_attr *flow_attr)
{
	struct ibv_flow *flow_id;
	int ret;

	flow_id = calloc(1, sizeof *flow_id);
	if (!flow_id)
		return NULL;

	ret = ibv_cmd_create_flow(qp, flow_id, flow_attr,
				  NULL, 0);
	if (!ret)
		return flow_id;

	free(flow_id);
	return NULL;
}

int mlx4_destroy_flow(struct ibv_flow *flow_id)
{
	int ret;

	ret = ibv_cmd_destroy_flow(flow_id);

	if (ret)
		return ret;

	free(flow_id);
	return 0;
}

int mlx4_destroy_wq(struct ibv_wq *ibwq)
{
	struct mlx4_context *mcontext = to_mctx(ibwq->context);
	struct mlx4_qp *qp = wq_to_mqp(ibwq);
	struct mlx4_cq *cq = NULL;
	int ret;

	pthread_mutex_lock(&mcontext->qp_table_mutex);

	ret = ibv_cmd_destroy_wq(ibwq);
	if (ret) {
		pthread_mutex_unlock(&mcontext->qp_table_mutex);
		return ret;
	}

	cq = to_mcq(ibwq->cq);
	pthread_spin_lock(&cq->lock);
	__mlx4_cq_clean(cq, ibwq->wq_num, NULL);

	mlx4_clear_qp(mcontext, ibwq->wq_num);

	pthread_spin_unlock(&cq->lock);

	pthread_mutex_unlock(&mcontext->qp_table_mutex);

	mlx4_free_db(mcontext, MLX4_DB_TYPE_RQ, qp->db);
	free(qp->rq.wrid);
	free(qp->sq.wrid);

	mlx4_free_buf(mcontext, &qp->buf);

	free(qp);

	return 0;
}

struct ibv_rwq_ind_table *mlx4_create_rwq_ind_table(struct ibv_context *context,
						    struct ibv_rwq_ind_table_init_attr *init_attr)
{
	struct ib_uverbs_ex_create_rwq_ind_table_resp resp = {};
	struct ibv_rwq_ind_table *ind_table;
	int err;

	ind_table = calloc(1, sizeof(*ind_table));
	if (!ind_table)
		return NULL;

	err = ibv_cmd_create_rwq_ind_table(context, init_attr, ind_table, &resp,
					   sizeof(resp));
	if (err)
		goto err;

	return ind_table;

err:
	free(ind_table);
	return NULL;
}

int mlx4_destroy_rwq_ind_table(struct ibv_rwq_ind_table *rwq_ind_table)
{
	int ret;

	ret = ibv_cmd_destroy_rwq_ind_table(rwq_ind_table);

	if (ret)
		return ret;

	free(rwq_ind_table);
	return 0;
}

int mlx4_modify_cq(struct ibv_cq *cq, struct ibv_modify_cq_attr *attr)
{
	struct ibv_modify_cq cmd = {};

	return ibv_cmd_modify_cq(cq, attr, &cmd, sizeof(cmd));
}
