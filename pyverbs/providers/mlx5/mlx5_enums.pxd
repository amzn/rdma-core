# SPDX-License-Identifier: (GPL-2.0 OR Linux-OpenIB)
# Copyright (c) 2019 Mellanox Technologies, Inc. All rights reserved. See COPYING file

#cython: language_level=3

cdef extern from 'infiniband/mlx5dv.h':

    cpdef enum:
        MLX5_OPCODE_NOP
        MLX5_OPCODE_SEND_INVAL
        MLX5_OPCODE_RDMA_WRITE
        MLX5_OPCODE_RDMA_WRITE_IMM
        MLX5_OPCODE_SEND
        MLX5_OPCODE_SEND_IMM
        MLX5_OPCODE_TSO
        MLX5_OPCODE_RDMA_READ
        MLX5_OPCODE_ATOMIC_CS
        MLX5_OPCODE_ATOMIC_FA
        MLX5_OPCODE_ATOMIC_MASKED_CS
        MLX5_OPCODE_ATOMIC_MASKED_FA
        MLX5_OPCODE_FMR
        MLX5_OPCODE_LOCAL_INVAL
        MLX5_OPCODE_CONFIG_CMD
        MLX5_OPCODE_UMR
        MLX5_OPCODE_TAG_MATCHING
        MLX5_OPCODE_MMO

    cpdef enum:
        MLX5_WQE_CTRL_CQ_UPDATE
        MLX5_WQE_CTRL_SOLICITED
        MLX5_WQE_CTRL_FENCE
        MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE

    cpdef enum mlx5dv_context_attr_flags:
        MLX5DV_CONTEXT_FLAGS_DEVX

    cpdef enum mlx5dv_context_attr_comp_mask:
        MLX5DV_CONTEXT_ATTR_MASK_FD_ARRAY

    cpdef enum mlx5dv_context_comp_mask:
        MLX5DV_CONTEXT_MASK_CQE_COMPRESION      = 1 << 0
        MLX5DV_CONTEXT_MASK_SWP                 = 1 << 1
        MLX5DV_CONTEXT_MASK_STRIDING_RQ         = 1 << 2
        MLX5DV_CONTEXT_MASK_TUNNEL_OFFLOADS     = 1 << 3
        MLX5DV_CONTEXT_MASK_DYN_BFREGS          = 1 << 4
        MLX5DV_CONTEXT_MASK_CLOCK_INFO_UPDATE   = 1 << 5
        MLX5DV_CONTEXT_MASK_FLOW_ACTION_FLAGS   = 1 << 6
        MLX5DV_CONTEXT_MASK_DC_ODP_CAPS         = 1 << 7
        MLX5DV_CONTEXT_MASK_NUM_LAG_PORTS       = 1 << 9
        MLX5DV_CONTEXT_MASK_SIGNATURE_OFFLOAD   = 1 << 10
        MLX5DV_CONTEXT_MASK_DCI_STREAMS         = 1 << 11
        MLX5DV_CONTEXT_MASK_WR_MEMCPY_LENGTH    = 1 << 12
        MLX5DV_CONTEXT_MASK_CRYPTO_OFFLOAD      = 1 << 13
        MLX5DV_CONTEXT_MASK_MAX_DC_RD_ATOM      = 1 << 14
        MLX5DV_CONTEXT_MASK_OOO_RECV_WRS        = 1 << 16

    cpdef enum mlx5dv_context_flags:
        MLX5DV_CONTEXT_FLAGS_CQE_V1                     = 1 << 0
        MLX5DV_CONTEXT_FLAGS_MPW_ALLOWED                = 1 << 2
        MLX5DV_CONTEXT_FLAGS_ENHANCED_MPW               = 1 << 3
        MLX5DV_CONTEXT_FLAGS_CQE_128B_COMP              = 1 << 4
        MLX5DV_CONTEXT_FLAGS_CQE_128B_PAD               = 1 << 5
        MLX5DV_CONTEXT_FLAGS_PACKET_BASED_CREDIT_MODE   = 1 << 6
        MLX5DV_CONTEXT_FLAGS_REAL_TIME_TS               = 1 << 7
        MLX5DV_CONTEXT_FLAGS_BLUEFLAME                  = 1 << 8

    cpdef enum mlx5dv_sw_parsing_offloads:
        MLX5DV_SW_PARSING       = 1 << 0
        MLX5DV_SW_PARSING_CSUM  = 1 << 1
        MLX5DV_SW_PARSING_LSO   = 1 << 2

    cpdef enum mlx5dv_cqe_comp_res_format:
        MLX5DV_CQE_RES_FORMAT_HASH          = 1 << 0
        MLX5DV_CQE_RES_FORMAT_CSUM          = 1 << 1
        MLX5DV_CQE_RES_FORMAT_CSUM_STRIDX   = 1 << 2

    cpdef enum mlx5dv_sched_elem_attr_flags:
        MLX5DV_SCHED_ELEM_ATTR_FLAGS_BW_SHARE    = 1 << 0
        MLX5DV_SCHED_ELEM_ATTR_FLAGS_MAX_AVG_BW  = 1 << 1

    cpdef enum mlx5dv_tunnel_offloads:
        MLX5DV_RAW_PACKET_CAP_TUNNELED_OFFLOAD_VXLAN            = 1 << 0
        MLX5DV_RAW_PACKET_CAP_TUNNELED_OFFLOAD_GRE              = 1 << 1
        MLX5DV_RAW_PACKET_CAP_TUNNELED_OFFLOAD_GENEVE           = 1 << 2
        MLX5DV_RAW_PACKET_CAP_TUNNELED_OFFLOAD_CW_MPLS_OVER_GRE = 1 << 3
        MLX5DV_RAW_PACKET_CAP_TUNNELED_OFFLOAD_CW_MPLS_OVER_UDP = 1 << 4

    cpdef enum mlx5dv_flow_action_cap_flags:
        MLX5DV_FLOW_ACTION_FLAGS_ESP_AES_GCM                = 1 << 0
        MLX5DV_FLOW_ACTION_FLAGS_ESP_AES_GCM_REQ_METADATA   = 1 << 1
        MLX5DV_FLOW_ACTION_FLAGS_ESP_AES_GCM_SPI_STEERING   = 1 << 2
        MLX5DV_FLOW_ACTION_FLAGS_ESP_AES_GCM_FULL_OFFLOAD   = 1 << 3
        MLX5DV_FLOW_ACTION_FLAGS_ESP_AES_GCM_TX_IV_IS_ESN   = 1 << 4

    cpdef enum mlx5dv_qp_init_attr_mask:
        MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS    = 1 << 0
        MLX5DV_QP_INIT_ATTR_MASK_DC                 = 1 << 1
        MLX5DV_QP_INIT_ATTR_MASK_SEND_OPS_FLAGS     = 1 << 2
        MLX5DV_QP_INIT_ATTR_MASK_DCI_STREAMS        = 1 << 3

    cpdef enum mlx5dv_qp_create_flags:
        MLX5DV_QP_CREATE_TUNNEL_OFFLOADS            = 1 << 0
        MLX5DV_QP_CREATE_TIR_ALLOW_SELF_LOOPBACK_UC = 1 << 1
        MLX5DV_QP_CREATE_TIR_ALLOW_SELF_LOOPBACK_MC = 1 << 2
        MLX5DV_QP_CREATE_DISABLE_SCATTER_TO_CQE     = 1 << 3
        MLX5DV_QP_CREATE_ALLOW_SCATTER_TO_CQE       = 1 << 4
        MLX5DV_QP_CREATE_PACKET_BASED_CREDIT_MODE   = 1 << 5
        MLX5DV_QP_CREATE_SIG_PIPELINING             = 1 << 6
        MLX5DV_QP_CREATE_OOO_DP                     = 1 << 7

    cpdef enum mlx5dv_dc_type:
        MLX5DV_DCTYPE_DCT   = 1
        MLX5DV_DCTYPE_DCI   = 2

    cpdef enum mlx5dv_mkey_init_attr_flags:
        MLX5DV_MKEY_INIT_ATTR_FLAGS_INDIRECT
        MLX5DV_MKEY_INIT_ATTR_FLAGS_BLOCK_SIGNATURE
        MLX5DV_MKEY_INIT_ATTR_FLAGS_CRYPTO
        MLX5DV_MKEY_INIT_ATTR_FLAGS_REMOTE_INVALIDATE

    cpdef enum mlx5dv_mkey_err_type:
        MLX5DV_MKEY_NO_ERR
        MLX5DV_MKEY_SIG_BLOCK_BAD_GUARD
        MLX5DV_MKEY_SIG_BLOCK_BAD_REFTAG
        MLX5DV_MKEY_SIG_BLOCK_BAD_APPTAG

    cpdef enum mlx5dv_sig_type:
        MLX5DV_SIG_TYPE_T10DIF
        MLX5DV_SIG_TYPE_CRC

    cpdef enum mlx5dv_sig_t10dif_bg_type:
        MLX5DV_SIG_T10DIF_CRC
        MLX5DV_SIG_T10DIF_CSUM

    cpdef enum mlx5dv_sig_t10dif_flags:
        MLX5DV_SIG_T10DIF_FLAG_REF_REMAP
        MLX5DV_SIG_T10DIF_FLAG_APP_ESCAPE
        MLX5DV_SIG_T10DIF_FLAG_APP_REF_ESCAPE

    cpdef enum mlx5dv_sig_crc_type:
        MLX5DV_SIG_CRC_TYPE_CRC32
        MLX5DV_SIG_CRC_TYPE_CRC32C
        MLX5DV_SIG_CRC_TYPE_CRC64_XP10

    cpdef enum mlx5dv_block_size:
        MLX5DV_BLOCK_SIZE_512
        MLX5DV_BLOCK_SIZE_520
        MLX5DV_BLOCK_SIZE_4048
        MLX5DV_BLOCK_SIZE_4096
        MLX5DV_BLOCK_SIZE_4160

    cpdef enum mlx5dv_sig_mask:
        MLX5DV_SIG_MASK_T10DIF_GUARD
        MLX5DV_SIG_MASK_T10DIF_APPTAG
        MLX5DV_SIG_MASK_T10DIF_REFTAG
        MLX5DV_SIG_MASK_CRC32
        MLX5DV_SIG_MASK_CRC32C
        MLX5DV_SIG_MASK_CRC64_XP10

    cpdef enum mlx5dv_sig_block_attr_flags:
        MLX5DV_SIG_BLOCK_ATTR_FLAG_COPY_MASK

    cpdef enum mlx5dv_qp_create_send_ops_flags:
        MLX5DV_QP_EX_WITH_MR_INTERLEAVED    = 1 << 0
        MLX5DV_QP_EX_WITH_MR_LIST           = 1 << 1
        MLX5DV_QP_EX_WITH_MKEY_CONFIGURE    = 1 << 2
        MLX5DV_QP_EX_WITH_RAW_WQE           = 1 << 3
        MLX5DV_QP_EX_WITH_MEMCPY            = 1 << 4

    cpdef enum mlx5dv_cq_init_attr_mask:
        MLX5DV_CQ_INIT_ATTR_MASK_COMPRESSED_CQE = 1 << 0
        MLX5DV_CQ_INIT_ATTR_MASK_FLAGS          = 1 << 1
        MLX5DV_CQ_INIT_ATTR_MASK_CQE_SIZE       = 1 << 2

    cpdef enum mlx5dv_cq_init_attr_flags:
        MLX5DV_CQ_INIT_ATTR_FLAGS_CQE_PAD   = 1 << 0
        MLX5DV_CQ_INIT_ATTR_FLAGS_RESERVED  = 1 << 1

    cpdef enum mlx5dv_flow_action_type:
        MLX5DV_FLOW_ACTION_DEST_IBV_QP
        MLX5DV_FLOW_ACTION_DROP
        MLX5DV_FLOW_ACTION_IBV_COUNTER
        MLX5DV_FLOW_ACTION_IBV_FLOW_ACTION
        MLX5DV_FLOW_ACTION_TAG
        MLX5DV_FLOW_ACTION_DEST_DEVX
        MLX5DV_FLOW_ACTION_COUNTERS_DEVX
        MLX5DV_FLOW_ACTION_DEFAULT_MISS

    cpdef enum mlx5dv_dr_domain_type:
        MLX5DV_DR_DOMAIN_TYPE_NIC_RX
        MLX5DV_DR_DOMAIN_TYPE_NIC_TX
        MLX5DV_DR_DOMAIN_TYPE_FDB

    cpdef enum mlx5dv_qp_comp_mask:
        MLX5DV_QP_MASK_UAR_MMAP_OFFSET
        MLX5DV_QP_MASK_RAW_QP_HANDLES
        MLX5DV_QP_MASK_RAW_QP_TIR_ADDR

    cpdef enum mlx5dv_srq_comp_mask:
        MLX5DV_SRQ_MASK_SRQN

    cpdef enum mlx5dv_obj_type:
        MLX5DV_OBJ_QP
        MLX5DV_OBJ_CQ
        MLX5DV_OBJ_SRQ
        MLX5DV_OBJ_RWQ
        MLX5DV_OBJ_DM
        MLX5DV_OBJ_AH
        MLX5DV_OBJ_PD

    cpdef enum:
        MLX5_RCV_DBR
        MLX5_SND_DBR

    cpdef enum:
        MLX5_CQE_OWNER_MASK
        MLX5_CQE_REQ
        MLX5_CQE_RESP_WR_IMM
        MLX5_CQE_RESP_SEND
        MLX5_CQE_RESP_SEND_IMM
        MLX5_CQE_RESP_SEND_INV
        MLX5_CQE_RESIZE_CQ
        MLX5_CQE_NO_PACKET
        MLX5_CQE_SIG_ERR
        MLX5_CQE_REQ_ERR
        MLX5_CQE_RESP_ERR
        MLX5_CQE_INVALID

    cpdef enum:
        MLX5_SEND_WQE_BB
        MLX5_SEND_WQE_SHIFT

    cpdef enum mlx5dv_vfio_context_attr_flags:
        MLX5DV_VFIO_CTX_FLAGS_INIT_LINK_DOWN

    cpdef enum mlx5dv_wc_opcode:
        MLX5DV_WC_UMR
        MLX5DV_WC_RAW_WQE
        MLX5DV_WC_MEMCPY

    cpdef enum mlx5dv_crypto_standard:
        MLX5DV_CRYPTO_STANDARD_AES_XTS

    cpdef enum mlx5dv_signature_crypto_order:
        MLX5DV_SIGNATURE_CRYPTO_ORDER_SIGNATURE_AFTER_CRYPTO_ON_TX
        MLX5DV_SIGNATURE_CRYPTO_ORDER_SIGNATURE_BEFORE_CRYPTO_ON_TX

    cpdef enum mlx5dv_crypto_login_state:
        MLX5DV_CRYPTO_LOGIN_STATE_VALID
        MLX5DV_CRYPTO_LOGIN_STATE_NO_LOGIN
        MLX5DV_CRYPTO_LOGIN_STATE_INVALID

    cpdef enum mlx5dv_crypto_key_size:
        MLX5DV_CRYPTO_KEY_SIZE_128
        MLX5DV_CRYPTO_KEY_SIZE_256

    cpdef enum mlx5dv_crypto_key_purpose:
        MLX5DV_CRYPTO_KEY_PURPOSE_AES_XTS

    cpdef enum mlx5dv_dek_state:
        MLX5DV_DEK_STATE_READY
        MLX5DV_DEK_STATE_ERROR

    cpdef enum mlx5dv_dek_init_attr_mask:
        MLX5DV_DEK_INIT_ATTR_CRYPTO_LOGIN

    cpdef enum mlx5dv_crypto_engines_caps:
        MLX5DV_CRYPTO_ENGINES_CAP_AES_XTS
        MLX5DV_CRYPTO_ENGINES_CAP_AES_XTS_SINGLE_BLOCK
        MLX5DV_CRYPTO_ENGINES_CAP_AES_XTS_MULTI_BLOCK

    cpdef enum mlx5dv_crypto_wrapped_import_method_caps:
        MLX5DV_CRYPTO_WRAPPED_IMPORT_METHOD_CAP_AES_XTS

    cpdef enum mlx5dv_crypto_caps_flags:
        MLX5DV_CRYPTO_CAPS_CRYPTO
        MLX5DV_CRYPTO_CAPS_WRAPPED_CRYPTO_OPERATIONAL
        MLX5DV_CRYPTO_CAPS_WRAPPED_CRYPTO_GOING_TO_COMMISSIONING

    cpdef enum mlx5dv_dr_action_flags:
        MLX5DV_DR_ACTION_FLAGS_ROOT_LEVEL

    cpdef enum mlx5dv_dr_domain_sync_flags:
        MLX5DV_DR_DOMAIN_SYNC_FLAGS_SW
        MLX5DV_DR_DOMAIN_SYNC_FLAGS_HW
        MLX5DV_DR_DOMAIN_SYNC_FLAGS_MEM

    cpdef enum mlx5dv_dr_matcher_layout_flags:
       MLX5DV_DR_MATCHER_LAYOUT_RESIZABLE
       MLX5DV_DR_MATCHER_LAYOUT_NUM_RULE

    cpdef enum mlx5dv_dr_action_dest_type:
        MLX5DV_DR_ACTION_DEST
        MLX5DV_DR_ACTION_DEST_REFORMAT

    cpdef enum:
        MLX5DV_UMEM_MASK_DMABUF

    cpdef enum mlx5dv_flow_matcher_attr_mask:
        MLX5DV_FLOW_MATCHER_MASK_FT_TYPE
        MLX5DV_FLOW_MATCHER_MASK_IB_PORT

    cdef unsigned long long MLX5DV_RES_TYPE_QP
    cdef unsigned long long MLX5DV_RES_TYPE_RWQ
    cdef unsigned long long MLX5DV_RES_TYPE_DBR
    cdef unsigned long long MLX5DV_RES_TYPE_SRQ
    cdef unsigned long long MLX5DV_PP_ALLOC_FLAGS_DEDICATED_INDEX
    cdef unsigned long long MLX5DV_UAR_ALLOC_TYPE_BF
    cdef unsigned long long MLX5DV_UAR_ALLOC_TYPE_NC
    cdef unsigned long long MLX5DV_QUERY_PORT_VPORT
    cdef unsigned long long MLX5DV_QUERY_PORT_VPORT_VHCA_ID
    cdef unsigned long long MLX5DV_QUERY_PORT_VPORT_STEERING_ICM_RX
    cdef unsigned long long MLX5DV_QUERY_PORT_VPORT_STEERING_ICM_TX
    cdef unsigned long long MLX5DV_QUERY_PORT_VPORT_REG_C0
    cdef unsigned long long MLX5DV_QUERY_PORT_ESW_OWNER_VHCA_ID


cdef extern from 'infiniband/mlx5_user_ioctl_verbs.h':
    cdef enum mlx5_ib_uapi_flow_table_type:
        pass

cdef extern from 'infiniband/mlx5_api.h':
    cdef int MLX5DV_FLOW_TABLE_TYPE_RDMA_RX
    cdef int MLX5DV_FLOW_TABLE_TYPE_RDMA_TX
    cdef int MLX5DV_FLOW_TABLE_TYPE_NIC_RX
    cdef int MLX5DV_FLOW_TABLE_TYPE_NIC_TX
    cdef int MLX5DV_FLOW_TABLE_TYPE_FDB
    cdef int MLX5DV_FLOW_TABLE_TYPE_RDMA_TRANSPORT_RX
    cdef int MLX5DV_FLOW_TABLE_TYPE_RDMA_TRANSPORT_TX

    cdef int MLX5DV_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TUNNEL_TO_L2
    cdef int MLX5DV_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L2_TUNNEL
    cdef int MLX5DV_FLOW_ACTION_PACKET_REFORMAT_TYPE_L3_TUNNEL_TO_L2
    cdef int MLX5DV_FLOW_ACTION_PACKET_REFORMAT_TYPE_L2_TO_L3_TUNNEL

    cdef int MLX5DV_REG_DMABUF_ACCESS_DATA_DIRECT
