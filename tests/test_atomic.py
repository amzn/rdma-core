import unittest
import errno

from tests.base import RCResources, RDMATestCase, XRCResources
from pyverbs.pyverbs_error import PyverbsRDMAError
from pyverbs.qp import QPAttr, QPInitAttr
import pyverbs.device as d
from pyverbs.libibverbs_enums import ibv_access_flags, ibv_qp_type, ibv_atomic_cap, ibv_wr_opcode
from pyverbs.mr import MR
import tests.utils as u


class RCAtomic(RCResources):
    def __init__(self, dev_name, ib_port, gid_index, msg_size=8, qp_access=None,
                 mr_access=None):
        """
        Initialize an RCAtomic Resource object.
        :param dev_name: Device name to be used
        :param ib_port: IB port of the device to use
        :param gid_index: Which GID index to use
        :param msg_size: Message size for all resources memory actions
        :param qp_access: The QP access to use when modifying the resource's QP
        :param mr_access: The MR access to use when registering the resource's MR
        """
        atomic_access = ibv_access_flags.IBV_ACCESS_LOCAL_WRITE | \
            ibv_access_flags.IBV_ACCESS_REMOTE_ATOMIC
        self.qp_access = qp_access if qp_access else atomic_access
        self.mr_access = mr_access if mr_access else atomic_access
        super().__init__(dev_name=dev_name, ib_port=ib_port,
                         gid_index=gid_index)
        self.msg_size = msg_size
        self.new_mr_lkey = None

    def create_mr(self):
        try:
            self.mr = MR(self.pd, self.msg_size, self.mr_access)
        except PyverbsRDMAError as ex:
            if ex.error_code == errno.EOPNOTSUPP:
                raise unittest.SkipTest(f'Reg MR with access ({self.mr_access}) is not supported')
            raise ex

    def create_qp_init_attr(self):
        return QPInitAttr(qp_type=ibv_qp_type.IBV_QPT_RC, scq=self.cq, sq_sig_all=0,
                          rcq=self.cq, srq=self.srq, cap=self.create_qp_cap())

    def create_qp_attr(self):
        qp_attr = QPAttr(port_num=self.ib_port)
        qp_attr.qp_access_flags = self.qp_access
        return qp_attr

    @property
    def mr_lkey(self):
        return self.new_mr_lkey if self.new_mr_lkey is not None else self.mr.lkey


class XRCAtomic(XRCResources):
    def create_mr(self):
        try:
            atomic_access = ibv_access_flags.IBV_ACCESS_LOCAL_WRITE | \
                ibv_access_flags.IBV_ACCESS_REMOTE_ATOMIC
            self.mr = MR(self.pd, self.msg_size, atomic_access)
        except PyverbsRDMAError as ex:
            if ex.error_code == errno.EOPNOTSUPP:
                raise unittest.SkipTest(f'Reg MR with access ({atomic_access}) is not supported')
            raise ex


class AtomicTest(RDMATestCase):
    """
    Test various functionalities of the DM class.
    """
    def setUp(self):
        super().setUp()
        self.iters = 10
        self.server = None
        self.client = None
        self.traffic_args = None
        ctx = d.Context(name=self.dev_name)
        if ctx.query_device().atomic_caps == ibv_atomic_cap.IBV_ATOMIC_NONE:
            raise unittest.SkipTest('Atomic operations are not supported')

    def test_atomic_cmp_and_swap(self):
        self.create_players(RCAtomic)
        u.atomic_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP)
        u.atomic_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP,
                         receiver_val=1, sender_val=1)

    def test_atomic_fetch_and_add(self):
        self.create_players(RCAtomic)
        u.atomic_traffic(**self.traffic_args,
                         send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_xrc_atomic_fetch_and_add(self):
        self.create_players(XRCAtomic)
        u.atomic_traffic(**self.traffic_args,
                         send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_xrc_atomic_cmp_and_swap(self):
        self.create_players(XRCAtomic)
        u.atomic_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP)
        u.atomic_traffic(**self.traffic_args, send_op=ibv_wr_opcode.IBV_WR_ATOMIC_CMP_AND_SWP,
                         receiver_val=1, sender_val=1)

    def test_atomic_invalid_qp_access(self):
        self.create_players(RCAtomic, qp_access=ibv_access_flags.IBV_ACCESS_LOCAL_WRITE)
        with self.assertRaises(PyverbsRDMAError) as ex:
            u.atomic_traffic(**self.traffic_args,
                             send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_atomic_invalid_mr_access(self):
        self.create_players(RCAtomic, mr_access=ibv_access_flags.IBV_ACCESS_LOCAL_WRITE)
        with self.assertRaises(PyverbsRDMAError) as ex:
            u.atomic_traffic(**self.traffic_args,
                             send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_atomic_non_aligned_addr(self):
        self.create_players(RCAtomic, msg_size=9)
        self.client.raddr += 1
        with self.assertRaises(PyverbsRDMAError) as ex:
            u.atomic_traffic(**self.traffic_args,
                             send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_atomic_invalid_lkey(self):
        self.create_players(RCAtomic)
        self.client.new_mr_lkey = self.client.mr.lkey + 1
        with self.assertRaises(PyverbsRDMAError) as ex:
            u.atomic_traffic(**self.traffic_args,
                             send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)

    def test_atomic_invalid_rkey(self):
        self.create_players(RCAtomic)
        self.client.rkey += 1
        with self.assertRaises(PyverbsRDMAError) as ex:
            u.atomic_traffic(**self.traffic_args,
                             send_op=ibv_wr_opcode.IBV_WR_ATOMIC_FETCH_AND_ADD)
