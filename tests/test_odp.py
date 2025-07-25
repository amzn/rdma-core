from pyverbs.mem_alloc import mmap, munmap, madvise, MAP_ANONYMOUS_, MAP_PRIVATE_, \
    MAP_HUGETLB_
from tests.base import RCResources, UDResources, XRCResources
from pyverbs.qp import QPCap, QPAttr, QPInitAttr
from pyverbs.wr import SGE, SendWR, RecvWR
from tests.base import RDMATestCase
from pyverbs.mr import MR
from pyverbs.libibverbs_enums import ibv_odp_transport_cap_bits, ibv_access_flags, ibv_odp_transport_cap_bits, \
    ibv_qp_type, ibv_placement_type, ibv_selectivity_level, ibv_qp_create_send_ops_flags, ibv_wr_opcode, \
    ibv_wc_status, _IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE, _IBV_ADVISE_MR_ADVICE_PREFETCH, \
    _IBV_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT
import tests.utils as u
import unittest

HUGE_PAGE_SIZE = 0x200000


class OdpUD(UDResources):
    def __init__(self, request_user_addr=False, **kwargs):
        self.request_user_addr = request_user_addr
        self.user_addr = None
        super(OdpUD, self).__init__(**kwargs)

    @u.requires_odp('ud', ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SEND)
    def create_mr(self):
        if self.request_user_addr:
            self.user_addr = mmap(length=self.msg_size,
                                  flags=MAP_ANONYMOUS_ | MAP_PRIVATE_)
        self.send_mr = MR(self.pd, self.msg_size + u.GRH_SIZE,
                          ibv_access_flags.IBV_ACCESS_LOCAL_WRITE | ibv_access_flags.IBV_ACCESS_ON_DEMAND,
                          address=self.user_addr)
        self.recv_mr = MR(self.pd, self.msg_size + u.GRH_SIZE,
                          ibv_access_flags.IBV_ACCESS_LOCAL_WRITE)


class OdpRC(RCResources):
    def __init__(self, dev_name, ib_port, gid_index, is_huge=False,
                 request_user_addr=False, use_mr_prefetch=None, is_implicit=False,
                 prefetch_advice=_IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE,
                 msg_size=1024,
                 odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SEND | ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_RECV,
                 use_mixed_mr=False):
        """
        Initialize an OdpRC object.
        :param dev_name: Device name to be used
        :param ib_port: IB port of the device to use
        :param gid_index: Which GID index to use
        :param is_huge: If True, use huge pages for MR registration
        :param request_user_addr: Request to provide the MR's buffer address.
                                  If False, the buffer will be allocated by pyverbs.
        :param use_mr_prefetch: Describes the properties of the prefetch
                                operation. The options are 'sync', 'async'
                                and None to skip the prefetch operation.
        :param is_implicit: If True, register implicit MR.
        :param prefetch_advice: The advice of the prefetch request (ignored
                                if use_mr_prefetch is None).
        :param use_mixed_mr: If True, create a non-ODP MR in addition to the ODP MR.
        """
        self.is_huge = is_huge
        self.request_user_addr = request_user_addr
        self.is_implicit = is_implicit
        self.odp_caps = odp_caps
        self.access = ibv_access_flags.IBV_ACCESS_LOCAL_WRITE | ibv_access_flags.IBV_ACCESS_ON_DEMAND | \
            ibv_access_flags.IBV_ACCESS_REMOTE_ATOMIC | ibv_access_flags.IBV_ACCESS_REMOTE_READ | \
            ibv_access_flags.IBV_ACCESS_REMOTE_WRITE
        self.user_addr = None
        self.use_mixed_mr = use_mixed_mr
        self.non_odp_mr = None
        super(OdpRC, self).__init__(dev_name=dev_name, ib_port=ib_port,
                                    gid_index=gid_index)
        self.use_mr_prefetch = use_mr_prefetch
        self.prefetch_advice = prefetch_advice
        self.msg_size = msg_size

    @u.requires_odp('rc', ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SEND | ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_RECV)
    def create_mr(self):
        u.odp_supported(self.ctx, 'rc', self.odp_caps)
        if self.request_user_addr:
            mmap_flags = MAP_ANONYMOUS_| MAP_PRIVATE_
            length = self.msg_size
            if self.is_huge:
                mmap_flags |= MAP_HUGETLB_
                length = HUGE_PAGE_SIZE
            self.user_addr = mmap(length=length, flags=mmap_flags)
        access = self.access
        if self.is_huge:
            access |= ibv_access_flags.IBV_ACCESS_HUGETLB
        self.mr = MR(self.pd, self.msg_size, access, address=self.user_addr,
                     implicit=self.is_implicit)
        if self.use_mixed_mr:
            self.non_odp_mr = MR(self.pd, self.msg_size, ibv_access_flags.IBV_ACCESS_LOCAL_WRITE)

    def create_qp_init_attr(self):
        return QPInitAttr(qp_type=ibv_qp_type.IBV_QPT_RC, scq=self.cq, sq_sig_all=0,
                          rcq=self.cq, srq=self.srq, cap=self.create_qp_cap())

    def create_qp_attr(self):
        qp_attr = QPAttr(port_num=self.ib_port)
        qp_attr.qp_access_flags = self.access
        return qp_attr

    def create_qp_cap(self):
        if self.use_mixed_mr:
            return QPCap(max_recv_wr=self.num_msgs, max_send_sge=2, max_recv_sge=2)
        return super().create_qp_cap()


class OdpXRC(XRCResources):
    def __init__(self, request_user_addr=False, **kwargs):
        self.request_user_addr = request_user_addr
        self.user_addr = None
        super(OdpXRC, self).__init__(**kwargs)

    @u.requires_odp('xrc',  ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SEND | ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SRQ_RECV)
    def create_mr(self):
        if self.request_user_addr:
            self.user_addr = mmap(length=self.msg_size,
                                  flags=MAP_ANONYMOUS_| MAP_PRIVATE_)
        self.mr = u.create_custom_mr(self, ibv_access_flags.IBV_ACCESS_ON_DEMAND, user_addr=self.user_addr)

class OdpQpExRC(RCResources):
    def __init__(self, dev_name, ib_port, gid_index, is_huge=False,
                 request_user_addr=False, use_mr_prefetch=None, is_implicit=False,
                 prefetch_advice=_IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE,
                 msg_size=8,
                 odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_SEND | ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_RECV,
                 use_mixed_mr=False):

        ''' For object descriptions, refer to OdpRC class '''
        self.request_user_addr = request_user_addr
        self.is_implicit = is_implicit
        self.odp_caps = odp_caps
        self.access = ibv_access_flags.IBV_ACCESS_LOCAL_WRITE | ibv_access_flags.IBV_ACCESS_ON_DEMAND | \
            ibv_access_flags.IBV_ACCESS_REMOTE_ATOMIC | ibv_access_flags.IBV_ACCESS_REMOTE_READ | \
            ibv_access_flags.IBV_ACCESS_REMOTE_WRITE
        self.user_addr = None
        super(OdpQpExRC, self).__init__(dev_name=dev_name, ib_port=ib_port,
                                         gid_index=gid_index)
        self.msg_size = msg_size

        if self.odp_caps & ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_FLUSH:
            self.ptype = ibv_placement_type.IBV_FLUSH_GLOBAL
            self.level = ibv_selectivity_level.IBV_FLUSH_RANGE

    def create_qps(self):
        if self.odp_caps & ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_ATOMIC_WRITE:
            u.create_qp_ex(self, ibv_qp_type.IBV_QPT_RC, ibv_qp_create_send_ops_flags.IBV_QP_EX_WITH_ATOMIC_WRITE)
        elif self.odp_caps & ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_FLUSH:
            u.create_qp_ex(self, ibv_qp_type.IBV_QPT_RC,
                           ibv_qp_create_send_ops_flags.IBV_QP_EX_WITH_FLUSH | \
                           ibv_qp_create_send_ops_flags.IBV_QP_EX_WITH_RDMA_WRITE)
        else:
            raise unittest.SkipTest('There is no qpex test for the specified ODP caps.')

    def create_mr(self):
        u.odp_supported(self.ctx, 'rc', self.odp_caps)
        if self.odp_caps & ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_ATOMIC_WRITE:
            access = self.access
            if self.request_user_addr:
                mmap_flags = MAP_ANONYMOUS_| MAP_PRIVATE_
                length = self.msg_size
                self.user_addr = mmap(length=length, flags=mmap_flags)
            self.mr = MR(self.pd, self.msg_size, access, address=self.user_addr,
                         implicit=self.is_implicit)
        elif self.odp_caps & ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_FLUSH:
            try:
                self.mr = u.create_custom_mr(self, ibv_access_flags.IBV_ACCESS_FLUSH_GLOBAL | \
                                                   ibv_access_flags.IBV_ACCESS_REMOTE_WRITE | \
                                                   ibv_access_flags.IBV_ACCESS_ON_DEMAND)
            except PyverbsRDMAError as ex:
                if ex.error_code == errno.EINVAL:
                    raise unittest.SkipTest('Create mr with IBV_ACCESS_FLUSH_GLOBAL access flag is not supported in kernel')
                raise ex
        else:
            raise unittest.SkipTest('There is no qpex test for the specified ODP caps.')

class OdpTestCase(RDMATestCase):
    def setUp(self):
        super(OdpTestCase, self).setUp()
        self.iters = 100
        self.force_page_faults = True
        self.is_huge = False

    def create_players(self, resource, **resource_arg):
        """
        Init odp tests resources.
        :param resource: The RDMA resources to use. A class of type
                         BaseResources.
        :param resource_arg: Dict of args that specify the resource specific
                             attributes.
        """
        sync_attrs = False if resource == OdpUD else True
        super().create_players(resource, sync_attrs, **resource_arg)
        self.traffic_args['force_page_faults'] = self.force_page_faults

    def tearDown(self):
        if self.server and self.server.user_addr:
            length = HUGE_PAGE_SIZE if self.is_huge else self.server.msg_size
            munmap(self.server.user_addr, length)
        if self.client and self.client.user_addr:
            length = HUGE_PAGE_SIZE if self.is_huge else self.client.msg_size
            munmap(self.client.user_addr, length)
        super(OdpTestCase, self).tearDown()

    def test_odp_rc_traffic(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults)
        u.traffic(**self.traffic_args)

    def test_odp_rc_mixed_mr(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            use_mixed_mr=True)
        u.traffic(**self.traffic_args)

    def test_odp_qp_ex_rc_atomic_write(self):
        super().create_players(OdpQpExRC, request_user_addr=self.force_page_faults,
                               msg_size=8, odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_ATOMIC_WRITE)
        self.client.msg_size = 8
        self.server.msg_size = 8
        u.rdma_traffic(**self.traffic_args,
                       new_send=True, send_op=ibv_wr_opcode.IBV_WR_ATOMIC_WRITE)

    def test_odp_qp_ex_rc_flush(self):
        super().create_players(OdpQpExRC, request_user_addr=self.force_page_faults,
                               odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_FLUSH)
        wcs = u.flush_traffic(**self.traffic_args, new_send=True,
                              send_op=ibv_wr_opcode.IBV_WR_FLUSH)
        if wcs[0].status != ibv_wc_status.IBV_WC_SUCCESS:
            raise PyverbsError(f'Unexpected {wc_status_to_str(wcs[0].status)}')

        self.client.level = ibv_selectivity_level.IBV_FLUSH_MR
        wcs = u.flush_traffic(**self.traffic_args, new_send=True,
                              send_op=ibv_wr_opcode.IBV_WR_FLUSH)
        if wcs[0].status != ibv_wc_status.IBV_WC_SUCCESS:
            raise PyverbsError(f'Unexpected {wc_status_to_str(wcs[0].status)}')

    def test_odp_rc_atomic_cmp_and_swp(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            msg_size=8, odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_ATOMIC)
        u.atomic_traffic(**self.traffic_args,
                         send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP)
        u.atomic_traffic(**self.traffic_args, receiver_val=1, sender_val=1,
                         send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP)

    def test_odp_rc_atomic_fetch_and_add(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            msg_size=8, odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_ATOMIC)
        u.atomic_traffic(**self.traffic_args,
                         send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_odp_rc_rdma_read(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_READ)
        self.server.mr.write('s' * self.server.msg_size, self.server.msg_size)
        u.rdma_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_RDMA_READ)

    def test_odp_rc_rdma_write(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            odp_caps=ibv_odp_transport_cap_bits.IBV_ODP_SUPPORT_WRITE)
        u.rdma_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_RDMA_WRITE)

    def test_odp_implicit_rc_traffic(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            is_implicit=True)
        u.traffic(**self.traffic_args)

    def test_odp_ud_traffic(self):
        self.create_players(OdpUD, request_user_addr=self.force_page_faults)
        # Implement the traffic here because OdpUD uses two different MRs for
        # send and recv.
        ah_client = u.get_global_ah(self.client, self.gid_index, self.ib_port)
        recv_sge = SGE(self.server.recv_mr.buf, self.server.msg_size +
                       u.GRH_SIZE, self.server.recv_mr.lkey)
        server_recv_wr = RecvWR(sg=[recv_sge], num_sge=1)
        send_sge = SGE(self.client.send_mr.buf + u.GRH_SIZE,
                       self.client.msg_size, self.client.send_mr.lkey)
        client_send_wr = SendWR(num_sge=1, sg=[send_sge])
        for i in range(self.iters):
            madvise(self.client.send_mr.buf, self.client.msg_size)
            self.server.qp.post_recv(server_recv_wr)
            u.post_send(self.client, client_send_wr, ah=ah_client)
            u.poll_cq(self.client.cq)
            u.poll_cq(self.server.cq)

    def test_odp_xrc_traffic(self):
        self.create_players(OdpXRC, request_user_addr=self.force_page_faults)
        u.xrc_traffic(self.client, self.server)

    @u.requires_huge_pages()
    def test_odp_rc_huge_traffic(self):
        self.force_page_faults = False
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            is_huge=True)
        u.traffic(**self.traffic_args)

    @u.requires_huge_pages()
    def test_odp_rc_huge_user_addr_traffic(self):
        self.is_huge = True
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            is_huge=True)
        u.traffic(**self.traffic_args)

    def test_odp_sync_prefetch_rc_traffic(self):
        for advice in [_IBV_ADVISE_MR_ADVICE_PREFETCH,
                       _IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE]:
            self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                                use_mr_prefetch='sync', prefetch_advice=advice)
            u.traffic(**self.traffic_args)

    def test_odp_async_prefetch_rc_traffic(self):
        for advice in [_IBV_ADVISE_MR_ADVICE_PREFETCH,
                       _IBV_ADVISE_MR_ADVICE_PREFETCH_WRITE]:
            self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                                use_mr_prefetch='async', prefetch_advice=advice)
            u.traffic(**self.traffic_args)

    def test_odp_implicit_sync_prefetch_rc_traffic(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            use_mr_prefetch='sync', is_implicit=True)
        u.traffic(**self.traffic_args)

    def test_odp_implicit_async_prefetch_rc_traffic(self):
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            use_mr_prefetch='async', is_implicit=True)
        u.traffic(**self.traffic_args)

    def test_odp_prefetch_sync_no_page_fault_rc_traffic(self):
        prefetch_advice = _IBV_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            use_mr_prefetch='sync', prefetch_advice=prefetch_advice)
        u.traffic(**self.traffic_args)

    def test_odp_prefetch_async_no_page_fault_rc_traffic(self):
        prefetch_advice = _IBV_ADVISE_MR_ADVICE_PREFETCH_NO_FAULT
        self.create_players(OdpRC, request_user_addr=self.force_page_faults,
                            use_mr_prefetch='async', prefetch_advice=prefetch_advice)
        u.traffic(**self.traffic_args)
