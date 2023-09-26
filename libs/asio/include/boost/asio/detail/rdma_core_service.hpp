//
// detail/rdma_core_service.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//

#ifndef BOOST_ASIO_DETAIL_RDMA_CORE_SERVICE_HPP
#define BOOST_ASIO_DETAIL_RDMA_CORE_SERVICE_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#if !defined(BOOST_ASIO_HAS_IOCP) && !defined(BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)
#include <stdlib.h>
#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/detail/buffer_sequence_adapter.hpp>
#include <boost/asio/detail/memory.hpp>
#include <boost/asio/detail/noncopyable.hpp>
#include <boost/asio/detail/epoll_reactor.hpp>
#include <boost/asio/detail/reactor_op.hpp>
#include <boost/asio/detail/push_options.hpp>

#include "rdma_cm_connect_op.hpp"
#include "pingpong.hpp"
#include <infiniband/verbs.h>
#include <iostream>
#include <fcntl.h>
#include <sys/time.h>
enum
{
    PINGPONG_RECV_WRID = 1,
    PINGPONG_SEND_WRID = 2,
};

int use_dm = 0;
int use_ts = 0;
int use_odp = 0;
int implicit_odp = 0;
int use_new_send = 0;

namespace boost
{
    namespace asio
    {
        namespace detail
        {
            // The selector that performs event demultiplexing for the service.

            struct rdma_context
            {
                struct ibv_context *context;
                struct ibv_comp_channel *channel;
                struct ibv_pd *pd;
                struct ibv_mr *mr;
                struct ibv_dm *dm;
                union
                {
                    struct ibv_cq *cq;
                    struct ibv_cq_ex *cq_ex;
                } cq_s;
                struct ibv_qp *qp;
                struct ibv_qp_ex *qpx;
                char *buf;
                int size;
                int send_flags;
                int rx_depth;
                int pending;
                struct ibv_port_attr portinfo;
                uint64_t completion_timestamp_mask;

                struct pingpong_dest *rem_dest;
                uint64_t buf_addr;
                struct pingpong_dest *my_dest;
            };

            struct pingpong_dest
            {
                int lid;
                int qpn;
                int psn;
                union ibv_gid gid;
            };

            template <typename PortSpace>
            class rdma_core_service : public execution_context_service_base<rdma_core_service<PortSpace>>
            {
            public:
                epoll_reactor &reactor_;
                // Cached success value to avoid accessing category singleton.
                const boost::system::error_code success_ec_;
                // The implementation type of the cm id.
                struct implementation_type
                {
                    struct rdma_context *ctx;

                    int channel_dev_fd;
                    
                    // Per-descriptor data used by the reactor.
                    reactor::per_descriptor_data reactor_data_;
                };

                rdma_core_service(execution_context &context)
                    : execution_context_service_base<
                          rdma_core_service<PortSpace>>(context),
                      reactor_(use_service<reactor>(context))
                {
                    reactor_.init_task();
                }

                // Construct a new implementation.
                inline void construct(implementation_type &impl)
                {
                    impl.ctx = new rdma_context;

                    impl.channel_dev_fd = -1;

                    impl.reactor_data_ = reactor::per_descriptor_data();
                }

                // Destroy a implementation
                inline void destroy(implementation_type &impl)
                {
                    delete impl.ctx;
                }

                // move constructor
                inline void move_construct(implementation_type &impl, implementation_type &other_impl)
                {
                }

                // move assign
                inline void move_assign(implementation_type &impl,
                                        rdma_core_service &other_service, implementation_type &other_impl)
                {
                    destroy(impl);
                    move_construct(impl, other_impl);
                }

                /// Destructor.
                BOOST_ASIO_DECL virtual ~rdma_core_service()
                {
                }

                /// Destroy all user-defined handler objects owned by the service.
                BOOST_ASIO_DECL virtual void shutdown()
                {
                }

                // Destroy all user-defined handler objects owned by the service.

                static int pp_connect_ctx(struct rdma_context *ctx, int port, int my_psn,
                                          enum ibv_mtu mtu, int sl,
                                          struct pingpong_dest *dest, int sgid_idx)
                {
                    struct ibv_qp_attr attr = {
                        .qp_state = IBV_QPS_RTR,
                        .path_mtu = mtu,
                        .rq_psn = (uint32_t)dest->psn,
                        .dest_qp_num = (uint32_t)dest->qpn,
                        .ah_attr = {
                            .dlid = (uint16_t)dest->lid,
                            .sl = (uint8_t)sl,
                            .src_path_bits = 0,
                            .is_global = 0,
                            .port_num = (uint8_t)port},
                        .max_dest_rd_atomic = 1,
                        .min_rnr_timer = 12,
                    };

                    if (dest->gid.global.interface_id)
                    {
                        attr.ah_attr.is_global = 1;
                        attr.ah_attr.grh.hop_limit = 1;
                        attr.ah_attr.grh.dgid = dest->gid;
                        attr.ah_attr.grh.sgid_index = sgid_idx;
                    }
                    if (ibv_modify_qp(ctx->qp, &attr,
                                      IBV_QP_STATE |
                                          IBV_QP_AV |
                                          IBV_QP_PATH_MTU |
                                          IBV_QP_DEST_QPN |
                                          IBV_QP_RQ_PSN |
                                          IBV_QP_MAX_DEST_RD_ATOMIC |
                                          IBV_QP_MIN_RNR_TIMER))
                    {
                        fprintf(stderr, "Failed to modify QP to RTR\n");
                        return 1;
                    }

                    attr.qp_state = IBV_QPS_RTS;
                    attr.timeout = 14;
                    attr.retry_cnt = 7;
                    attr.rnr_retry = 7;
                    attr.sq_psn = my_psn;
                    attr.max_rd_atomic = 1;
                    if (ibv_modify_qp(ctx->qp, &attr,
                                      IBV_QP_STATE |
                                          IBV_QP_TIMEOUT |
                                          IBV_QP_RETRY_CNT |
                                          IBV_QP_RNR_RETRY |
                                          IBV_QP_SQ_PSN |
                                          IBV_QP_MAX_QP_RD_ATOMIC))
                    {
                        fprintf(stderr, "Failed to modify QP to RTS\n");
                        return 1;
                    }

                    return 0;
                }

                // 与服务器进行信息交换，获取其PingPong目标参数
                // @param[in] servername 服务器名
                // @param[in] port 服务器端口
                // @param[in] my_dest 我的目标地址信息
                // @return 返回从服务器获取的PingPong目标地址信息。失败时返回NULL。
                static struct pingpong_dest *pp_client_exch_dest(struct rdma_context *ctx,
                                                                 const char *servername, int port,
                                                                 const struct pingpong_dest *my_dest)
                {
                    struct addrinfo *res, *t;
                    struct addrinfo hints = {.ai_family = AF_UNSPEC,
                                             .ai_socktype = SOCK_STREAM};
                    char *service;
                    char msg[sizeof "0000:000000:000000:00000000:0000000000000000:00000000000000000000000000000000"];
                    int n;
                    int sockfd = -1;
                    struct pingpong_dest *rem_dest = NULL;
                    char gid[33];

                    if (asprintf(&service, "%d", port) < 0)
                        return NULL;

                    n = getaddrinfo(servername, service, &hints, &res);

                    if (n < 0)
                    {
                        fprintf(stderr, "%s for %s:%d\n", gai_strerror(n), servername,
                                port);
                        free(service);
                        return NULL;
                    }

                    for (t = res; t; t = t->ai_next)
                    {
                        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
                        if (sockfd >= 0)
                        {
                            if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
                                break;
                            close(sockfd);
                            sockfd = -1;
                        }
                    }

                    freeaddrinfo(res);
                    free(service);

                    if (sockfd < 0)
                    {
                        fprintf(stderr, "Couldn't connect to %s:%d\n", servername,
                                port);
                        return NULL;
                    }

                    gid_to_wire_gid(&my_dest->gid, gid);
                    sprintf(msg, "%04x:%06x:%06x:%08x:%016llx:%s", my_dest->lid, my_dest->qpn,
                            my_dest->psn, ctx->mr->lkey, (unsigned long long)(uintptr_t)ctx->buf, gid);
                    if (write(sockfd, msg, sizeof msg) != sizeof msg)
                    {
                        fprintf(stderr, "Couldn't send local address\n");
                        goto out;
                    }

                    if (read(sockfd, msg, sizeof msg) != sizeof msg ||
                        write(sockfd, "done", sizeof "done") != sizeof "done")
                    {
                        perror("client read/write");
                        fprintf(stderr, "Couldn't read/write remote address\n");
                        goto out;
                    }

                    rem_dest = new boost::asio::detail::pingpong_dest;
                    if (!rem_dest)
                        goto out;

                    sscanf(msg, "%x:%x:%x:%x:%016llx:%s", &rem_dest->lid, &rem_dest->qpn,
                           &rem_dest->psn, &ctx->mr->rkey, &ctx->buf_addr, gid);
                    wire_gid_to_gid(gid, &rem_dest->gid);

                out:
                    close(sockfd);
                    return rem_dest;
                }

                static struct pingpong_dest *pp_server_exch_dest(struct rdma_context *ctx,
                                                                 int ib_port, enum ibv_mtu mtu,
                                                                 int port, int sl,
                                                                 const struct pingpong_dest *my_dest,
                                                                 int sgid_idx)
                {
                    struct addrinfo *res, *t;
                    struct addrinfo hints = {
                        .ai_flags = AI_PASSIVE,
                        .ai_family = AF_UNSPEC,
                        .ai_socktype = SOCK_STREAM};
                    char *service;
                    char msg[sizeof "0000:000000:000000:00000000:0000000000000000:00000000000000000000000000000000"];
                    int n;
                    int sockfd = -1, connfd;
                    struct pingpong_dest *rem_dest = NULL;
                    char gid[33];

                    if (asprintf(&service, "%d", port) < 0)
                        return NULL;

                    n = getaddrinfo(NULL, service, &hints, &res);

                    if (n < 0)
                    {
                        fprintf(stderr, "%s for port %d\n", gai_strerror(n), port);
                        free(service);
                        return NULL;
                    }

                    for (t = res; t; t = t->ai_next)
                    {
                        sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);
                        if (sockfd >= 0)
                        {
                            n = 1;

                            setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

                            if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
                                break;
                            close(sockfd);
                            sockfd = -1;
                        }
                    }

                    freeaddrinfo(res);
                    free(service);

                    if (sockfd < 0)
                    {
                        fprintf(stderr, "Couldn't listen to port %d\n", port);
                        return NULL;
                    }

                    listen(sockfd, 1);
                    connfd = accept(sockfd, NULL, NULL);
                    close(sockfd);
                    if (connfd < 0)
                    {
                        fprintf(stderr, "accept() failed\n");
                        return NULL;
                    }

                    n = read(connfd, msg, sizeof msg);
                    if (n != sizeof msg)
                    {
                        perror("server read");
                        fprintf(stderr, "%d/%d: Couldn't read remote address\n", n, (int)sizeof msg);
                        goto out;
                    }

                    rem_dest = new boost::asio::detail::pingpong_dest;

                    if (!rem_dest)
                        goto out;

                    sscanf(msg, "%x:%x:%x:%x:%016llx:%s", &rem_dest->lid, &rem_dest->qpn,
                           &rem_dest->psn, &ctx->mr->rkey, &ctx->buf_addr, gid);
                    wire_gid_to_gid(gid, &rem_dest->gid);

                    if (pp_connect_ctx(ctx, ib_port, my_dest->psn, mtu, sl, rem_dest,
                                       sgid_idx))
                    {
                        fprintf(stderr, "Couldn't connect to remote QP\n");
                        free(rem_dest);
                        rem_dest = NULL;
                        goto out;
                    }

                    gid_to_wire_gid(&my_dest->gid, gid);
                    sprintf(msg, "%04x:%06x:%06x:%08x:%016llx:%s", my_dest->lid,
                            my_dest->qpn, my_dest->psn, ctx->mr->lkey,
                            (unsigned long long)(uintptr_t)ctx->buf, gid);

                    if (write(connfd, msg, sizeof msg) != sizeof msg ||
                        read(connfd, msg, sizeof msg) != sizeof "done")
                    {
                        fprintf(stderr, "Couldn't send/recv local address\n");
                        free(rem_dest);
                        rem_dest = NULL;
                        goto out;
                    }

                out:
                    close(connfd);
                    return rem_dest;
                }

                // 初始化本地网卡设置
                static struct rdma_context *init_ctx(struct ibv_device *ib_dev,
                                                     uint32_t rx_depth, uint8_t port,
                                                     int use_event, int size)
                {
                    struct rdma_context *ctx;
                    // 定义网络适配器访问权限，允许在本地对缓冲区进行写操作
                    int access_flags = IBV_ACCESS_LOCAL_WRITE;

                    ctx = new rdma_context();
                    if (!ctx)
                        return NULL;

                    // 设置需要注册的内存区域大小
                    ctx->size = size;
                    ctx->send_flags = IBV_SEND_SIGNALED;
                    ctx->rx_depth = rx_depth;
                    int flags;
                    ctx->buf = new char[size];
                    if (!ctx->buf)
                    {
                        fprintf(stderr, "Couldn't allocate work buf.\n");
                        goto clean_ctx;
                    }

                    // 将 buf 中的每个字节都设置为0x7b，即"{"字符
                    /* FIXME memset(ctx->buf, 0, size); */
                    memset(ctx->buf, 0x7b, size);

                    ctx->context = ibv_open_device(ib_dev);
                    if (!ctx->context)
                    {
                        fprintf(stderr, "Couldn't get context for %s\n",
                                ibv_get_device_name(ib_dev));
                        goto clean_buffer;
                    }

                    // 使用时间通知，创建完成通道
                    if (use_event)
                    {
                        ctx->channel = ibv_create_comp_channel(ctx->context);
                        if (!ctx->channel)
                        {
                            fprintf(stderr, "Couldn't create completion channel\n");
                            goto clean_device;
                        }
                    }
                    else
                        ctx->channel = NULL;

                    // 为完成通道创建保护域
                    ctx->pd = ibv_alloc_pd(ctx->context);
                    if (!ctx->pd)
                    {
                        fprintf(stderr, "Couldn't allocate PD\n");
                        goto clean_comp_channel;
                    }

                    // todo：使用ODP、ts、dm功能？
                    if (use_odp || use_ts || use_dm)
                    {
                        // 定义 RC ODP 功能掩码
                        const uint32_t rc_caps_mask = IBV_ODP_SUPPORT_SEND |
                                                      IBV_ODP_SUPPORT_RECV;
                        struct ibv_device_attr_ex attrx;

                        if (ibv_query_device_ex(ctx->context, NULL, &attrx))
                        {
                            fprintf(stderr, "Couldn't query device for its features\n");
                            goto clean_pd;
                        }

                        if (use_odp)
                        {
                            if (!(attrx.odp_caps.general_caps & IBV_ODP_SUPPORT) ||
                                (attrx.odp_caps.per_transport_caps.rc_odp_caps & rc_caps_mask) != rc_caps_mask)
                            {
                                fprintf(stderr, "The device isn't ODP capable or does not support RC send and receive with ODP\n");
                                goto clean_pd;
                            }
                            if (implicit_odp &&
                                !(attrx.odp_caps.general_caps & IBV_ODP_SUPPORT_IMPLICIT))
                            {
                                fprintf(stderr, "The device doesn't support implicit ODP\n");
                                goto clean_pd;
                            }
                            access_flags |= IBV_ACCESS_ON_DEMAND;
                        }

                        if (use_ts)
                        {
                            if (!attrx.completion_timestamp_mask)
                            {
                                fprintf(stderr, "The device isn't completion timestamp capable\n");
                                goto clean_pd;
                            }
                            ctx->completion_timestamp_mask = attrx.completion_timestamp_mask;
                        }

                        if (use_dm)
                        {
                            struct ibv_alloc_dm_attr dm_attr = {};

                            if (!attrx.max_dm_size)
                            {
                                fprintf(stderr, "Device doesn't support dm allocation\n");
                                goto clean_pd;
                            }

                            if (attrx.max_dm_size < size)
                            {
                                fprintf(stderr, "Device memory is insufficient\n");
                                goto clean_pd;
                            }

                            dm_attr.length = size;
                            ctx->dm = ibv_alloc_dm(ctx->context, &dm_attr);
                            if (!ctx->dm)
                            {
                                fprintf(stderr, "Dev mem allocation failed\n");
                                goto clean_pd;
                            }

                            access_flags |= IBV_ACCESS_ZERO_BASED;
                        }
                    }
                    // 如果使用隐式ODP，则将全部内存区域注册到预先创建的保护域中
                    if (implicit_odp)
                    {
                        ctx->mr = ibv_reg_mr(ctx->pd, NULL, SIZE_MAX, access_flags);
                    }
                    else
                    { // 否则，根据是否使用DM进行内存区域的注册
                        ctx->mr = use_dm ? ibv_reg_dm_mr(ctx->pd, ctx->dm, 0,
                                                         size, access_flags)
                                         : ibv_reg_mr(ctx->pd, ctx->buf, size, IBV_ACCESS_LOCAL_WRITE |
						      IBV_ACCESS_REMOTE_WRITE |
						      IBV_ACCESS_REMOTE_READ);
                    }

                    if (!ctx->mr)
                    {
                        fprintf(stderr, "Couldn't register MR\n");
                        goto clean_dm;
                    }
                    // 如果使用TS，会创建一个带有完成时间戳的完成队列

                    if (use_ts)
                    {
                        struct ibv_cq_init_attr_ex attr_ex = {
                            .cqe = rx_depth + 1,
                            .cq_context = NULL,
                            .channel = ctx->channel,
                            .comp_vector = 0,
                            .wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP};

                        ctx->cq_s.cq_ex = ibv_create_cq_ex(ctx->context, &attr_ex);
                    }
                    else
                    { // 如果不使用TS，则创建一个常规完整队列
                        ctx->cq_s.cq = ibv_create_cq(ctx->context, rx_depth + 1, NULL,
                                                     ctx->channel, 0);
                    }

                    if (!pp_cq(ctx))
                    {
                        fprintf(stderr, "Couldn't create CQ\n");
                        goto clean_mr;
                    }

                    {
                        struct ibv_qp_attr attr;
                        struct ibv_qp_init_attr init_attr = {
                            .send_cq = pp_cq(ctx),
                            .recv_cq = pp_cq(ctx),
                            .cap = {
                                .max_send_wr = 1,
                                .max_recv_wr = rx_depth,
                                .max_send_sge = 1,
                                .max_recv_sge = 1},
                            .qp_type = IBV_QPT_RC};

                        if (use_new_send)
                        {
                            struct ibv_qp_init_attr_ex init_attr_ex = {};

                            init_attr_ex.send_cq = pp_cq(ctx);
                            init_attr_ex.recv_cq = pp_cq(ctx);
                            init_attr_ex.cap.max_send_wr = 1;
                            init_attr_ex.cap.max_recv_wr = rx_depth;
                            init_attr_ex.cap.max_send_sge = 1;
                            init_attr_ex.cap.max_recv_sge = 1;
                            init_attr_ex.qp_type = IBV_QPT_RC;

                            init_attr_ex.comp_mask |= IBV_QP_INIT_ATTR_PD |
                                                      IBV_QP_INIT_ATTR_SEND_OPS_FLAGS;
                            init_attr_ex.pd = ctx->pd;
                            init_attr_ex.send_ops_flags = IBV_QP_EX_WITH_SEND;

                            ctx->qp = ibv_create_qp_ex(ctx->context, &init_attr_ex);
                        }
                        else
                        {
                            ctx->qp = ibv_create_qp(ctx->pd, &init_attr);
                        }

                        if (!ctx->qp)
                        {
                            fprintf(stderr, "Couldn't create QP\n");
                            goto clean_cq;
                        }

                        if (use_new_send)
                            ctx->qpx = ibv_qp_to_qp_ex(ctx->qp);

                        ibv_query_qp(ctx->qp, &attr, IBV_QP_CAP, &init_attr);
                        if (init_attr.cap.max_inline_data >= size && !use_dm)
                            ctx->send_flags |= IBV_SEND_INLINE;
                    }

                    {
                        struct ibv_qp_attr attr = {
                            .qp_state = IBV_QPS_INIT,
                            .qp_access_flags = 0,
                            .pkey_index = 0,
                            .port_num = port};

                        if (ibv_modify_qp(ctx->qp, &attr,
                                          IBV_QP_STATE |
                                              IBV_QP_PKEY_INDEX |
                                              IBV_QP_PORT |
                                              IBV_QP_ACCESS_FLAGS))
                        {
                            fprintf(stderr, "Failed to modify QP to INIT\n");
                            goto clean_qp;
                        }
                    }

                    // 获取设备文件描述符当前打开方式
                    flags = fcntl(ctx->channel->fd, F_GETFL);
                    // 为设备文件描述符增加非阻塞IO方式
                    fcntl(ctx->channel->fd, F_SETFL, flags | O_NONBLOCK);

                    return ctx;

                clean_qp:
                    ibv_destroy_qp(ctx->qp);

                clean_cq:
                    ibv_destroy_cq(pp_cq(ctx));

                clean_mr:
                    ibv_dereg_mr(ctx->mr);

                clean_dm:
                    if (ctx->dm)
                        ibv_free_dm(ctx->dm);

                clean_pd:
                    ibv_dealloc_pd(ctx->pd);

                clean_comp_channel:
                    if (ctx->channel)
                        ibv_destroy_comp_channel(ctx->channel);

                clean_device:
                    ibv_close_device(ctx->context);

                clean_buffer:
                    free(ctx->buf);

                clean_ctx:
                    free(ctx);

                    return NULL;
                }

                static int pp_post_recv(struct rdma_context *ctx, int n)
                {
                    struct ibv_sge list = {
                        .addr = use_dm ? 0 : (uintptr_t)ctx->buf,
                        .length = (uint32_t)ctx->size,
                        .lkey = ctx->mr->lkey};
                    struct ibv_recv_wr wr = {
                        .wr_id = PINGPONG_RECV_WRID,
                        .sg_list = &list,
                        .num_sge = 1,
                    };
                    struct ibv_recv_wr *bad_wr;
                    int i;

                    for (i = 0; i < n; ++i)
                        if (ibv_post_recv(ctx->qp, &wr, &bad_wr))
                            break;

                    return i;
                }

                static int pp_post_send(struct rdma_context *ctx)
                {
                    struct ibv_sge list = {
                        .addr = use_dm ? 0 : (uintptr_t)ctx->buf,
                        .length = ctx->size,
                        .lkey = ctx->mr->lkey};
                    struct ibv_send_wr wr = {
                        .wr_id = 123123,
                        .sg_list = &list,
                        .num_sge = 1,
                        .opcode = IBV_WR_SEND,
                        .send_flags = ctx->send_flags,
                        // .wr.rdma.remote_addr =,
                        // .wr.rdma.rkey =,
                    };
                    struct ibv_send_wr *bad_wr;

                    if (use_new_send)
                    {
                        ibv_wr_start(ctx->qpx);

                        ctx->qpx->wr_id = PINGPONG_SEND_WRID;
                        ctx->qpx->wr_flags = ctx->send_flags;

                        ibv_wr_send(ctx->qpx);
                        ibv_wr_set_sge(ctx->qpx, list.lkey, list.addr, list.length);

                        return ibv_wr_complete(ctx->qpx);
                    }
                    else
                    {
                        return ibv_post_send(ctx->qp, &wr, &bad_wr);
                    }
                }

                static int pp_post_write(struct rdma_context *ctx, int offset)
                {

                    struct ibv_sge list = {
                        .addr = (uintptr_t)ctx->buf,
                        .length = 512,
                        .lkey = ctx->mr->lkey};
                    struct ibv_send_wr wr = {
                        .wr_id = 123123,
                        .sg_list = &list,
                        .num_sge = 1,
                        .opcode = IBV_WR_RDMA_WRITE,
                        .send_flags = ctx->send_flags,

                    };
                    wr.wr.rdma.remote_addr = (uintptr_t)ctx->buf_addr + offset;
                    wr.wr.rdma.rkey = ctx->mr->rkey;
                    struct ibv_send_wr *bad_wr;

                    if (use_new_send)
                    {
                        ibv_wr_start(ctx->qpx);

                        ctx->qpx->wr_id = PINGPONG_SEND_WRID;
                        ctx->qpx->wr_flags = ctx->send_flags;

                        ibv_wr_send(ctx->qpx);
                        ibv_wr_set_sge(ctx->qpx, list.lkey, list.addr, list.length);

                        return ibv_wr_complete(ctx->qpx);
                    }
                    else
                    {
                        return ibv_post_send(ctx->qp, &wr, &bad_wr);
                    }
                }

                static int pp_post_read(struct rdma_context *ctx, int offset)
                {

                    struct ibv_sge list = {
                        .addr = (uintptr_t)ctx->buf,
                        .length = 1024,
                        .lkey = ctx->mr->lkey};
                    struct ibv_send_wr wr = {
                        .wr_id = 123124,
                        .sg_list = &list,
                        .num_sge = 1,
                        .opcode = IBV_WR_RDMA_READ,
                        .send_flags = ctx->send_flags,

                    };
                    wr.wr.rdma.remote_addr = (uintptr_t)ctx->buf_addr + offset;
                    wr.wr.rdma.rkey = ctx->mr->rkey;
                    struct ibv_send_wr *bad_wr;

                    if (use_new_send)
                    {
                        ibv_wr_start(ctx->qpx);

                        ctx->qpx->wr_id = PINGPONG_SEND_WRID;
                        ctx->qpx->wr_flags = ctx->send_flags;

                        ibv_wr_send(ctx->qpx);
                        ibv_wr_set_sge(ctx->qpx, list.lkey, list.addr, list.length);

                        return ibv_wr_complete(ctx->qpx);
                    }
                    else
                    {
                        return ibv_post_send(ctx->qp, &wr, &bad_wr);
                    }
                }
                void
                create_rdma_core(implementation_type &impl, const char *servername, boost::system::error_code &ec)
                {
                    struct ibv_device *ib_dev;

                    // unsigned int size = 4096;
                    int size = 1024;
                    uint32_t rx_depth = 50;
                    unsigned int port = 12345;
                    uint8_t ib_port = 1;
                    int use_event = 1;
                    int gidx = 1;
                    char gid[33];
                    srand48(getpid() * time(NULL));

                    struct ibv_device **dev_list;
                    dev_list = ibv_get_device_list(NULL);

                    if (!dev_list)
                    {
                        perror("Failed to get IB devices list");
                        return;
                    }
                    char *ib_devname = NULL;
                    if (!ib_devname)
                    {
                        ib_dev = *dev_list;
                        if (!ib_dev)
                        {
                            fprintf(stderr, "No IB devices found\n");
                            return;
                        }
                    }
                    else
                    {
                        int i;
                        for (i = 0; dev_list[i]; ++i)
                            if (!strcmp(ibv_get_device_name(dev_list[i]), ib_devname))
                                break;
                        ib_dev = dev_list[i];
                        if (!ib_dev)
                        {
                            fprintf(stderr, "IB device %s not found\n", ib_devname);
                            return;
                        }
                    }

                    impl.ctx = init_ctx(ib_dev, rx_depth, ib_port, use_event, size);
                    impl.channel_dev_fd = impl.ctx->channel->fd;
                    ibv_req_notify_cq(pp_cq(impl.ctx), 0);

                    int routs = pp_post_recv(impl.ctx, impl.ctx->rx_depth);

                    if (routs < impl.ctx->rx_depth)
                    {
                        fprintf(stderr, "Couldn't post receive (%d)\n", routs);
                        return;
                    }

                    if (use_event)
                        if (ibv_req_notify_cq(pp_cq(impl.ctx), 0))
                        {
                            fprintf(stderr, "Couldn't request CQ notification\n");
                            return;
                        }

                    if (ibv_query_port(impl.ctx->context, ib_port, &impl.ctx->portinfo))
                    {
                        fprintf(stderr, "Couldn't get port info\n");
                        return;
                    }
                    impl.ctx->my_dest = new boost::asio::detail::pingpong_dest;

                    impl.ctx->my_dest->lid = impl.ctx->portinfo.lid;
                    if (impl.ctx->portinfo.link_layer != IBV_LINK_LAYER_ETHERNET &&
                        !impl.ctx->my_dest->lid)
                    {
                        fprintf(stderr, "Couldn't get local LID\n");
                        return;
                    }
                    if (gidx >= 0)
                    {
                        if (ibv_query_gid(impl.ctx->context, ib_port, gidx, &impl.ctx->my_dest->gid))
                        {
                            fprintf(stderr, "can't read sgid of index %d\n", gidx);
                            return;
                        }
                    }
                    else
                        memset(&impl.ctx->my_dest->gid, 0, sizeof impl.ctx->my_dest->gid);

                    impl.ctx->my_dest->qpn = impl.ctx->qp->qp_num;
                    impl.ctx->my_dest->psn = lrand48() & 0xffffff;
                    inet_ntop(AF_INET6, &impl.ctx->my_dest->gid, gid, sizeof gid);
                    printf("  local address:  LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
                           impl.ctx->my_dest->lid, impl.ctx->my_dest->qpn, impl.ctx->my_dest->psn, gid);

                    if (int err = reactor_.register_descriptor(impl.channel_dev_fd, impl.reactor_data_))
                    {
                        ec = boost::system::error_code(err, boost::asio::error::get_system_category());
                        return;
                    }

                    if (servername)
                        impl.ctx->rem_dest = pp_client_exch_dest(impl.ctx, servername, port, impl.ctx->my_dest);
                    else
                        impl.ctx->rem_dest = pp_server_exch_dest(impl.ctx, ib_port, IBV_MTU_1024, port, 0,
                                                                 impl.ctx->my_dest, gidx);
                    if (!impl.ctx->rem_dest)
                        return;

                    inet_ntop(AF_INET6, &impl.ctx->rem_dest->gid, gid, sizeof gid);
                    printf("  remote address: LID 0x%04x, QPN 0x%06x, PSN 0x%06x, GID %s\n",
                           impl.ctx->rem_dest->lid, impl.ctx->rem_dest->qpn, impl.ctx->rem_dest->psn, gid);

                    if (servername)
                        if (pp_connect_ctx(impl.ctx, ib_port, impl.ctx->my_dest->psn, IBV_MTU_1024, 0, impl.ctx->rem_dest,
                                           gidx))
                            return;
                }

                // 获得内存区域的地址
                char *get_buf(implementation_type &impl)
                {
                    return impl.ctx->buf;
                }

                // Start the asynchronous operation for handlers that are specialised for
                // immediate completion.
                template <typename Op>
                void start_op(implementation_type &impl, int op_type, Op *op,
                              bool is_continuation, bool is_non_blocking, bool noop,
                              const void *io_ex, ...)
                {
                    return do_start_op(impl, op_type, op, is_continuation,
                                       is_non_blocking, noop, &Op::do_immediate, io_ex);
                }

                // Start the asynchronous operation for handlers that are not specialised for
                // immediate completion.
                template <typename Op>
                void start_op(implementation_type &impl, int op_type, Op *op,
                              bool is_continuation, bool is_non_blocking, bool noop, const void *,
                              typename enable_if<
                                  is_same<
                                      typename associated_immediate_executor<
                                          typename Op::handler_type,
                                          typename Op::io_executor_type>::asio_associated_immediate_executor_is_unspecialised,
                                      void>::value>::type *)
                {
                    return do_start_op(impl, op_type, op, is_continuation, is_non_blocking,
                                       noop, &reactor::call_post_immediate_completion, &reactor_);
                }

                void do_start_op(
                    implementation_type &impl, int op_type,
                    reactor_op *op, bool is_continuation, bool is_non_blocking, bool noop,
                    void (*on_immediate)(operation *op, bool, const void *),
                    const void *immediate_arg)
                {
                    if (!noop)
                    {

                        reactor_.start_op_(op_type, impl.channel_dev_fd, impl.reactor_data_, op,
                                           is_continuation, is_non_blocking, on_immediate, immediate_arg);
                        return;
                    }

                    on_immediate(op, is_continuation, immediate_arg);
                }

                static struct ibv_cq *pp_cq(struct rdma_context *ctx)
                {
                    return use_ts ? ibv_cq_ex_to_cq(ctx->cq_s.cq_ex) : ctx->cq_s.cq;
                }

                template <typename Handler, typename IoExecutor>
                boost::system::error_code send(implementation_type &impl,
                                               boost::system::error_code &ec,
                                               Handler &handler,
                                               const IoExecutor &io_ex)
                {

                    impl.ctx->pending = PINGPONG_RECV_WRID;

                    if (pp_post_send(impl.ctx))
                    {
                        fprintf(stderr, "Couldn't post send\n");
                        return ec;
                    }
                    impl.ctx->pending |= PINGPONG_SEND_WRID;

                    return ec;
                }

                template <typename Handler, typename IoExecutor>
                boost::system::error_code rdma_write(implementation_type &impl,
                                                     boost::system::error_code &ec,
                                                     Handler &handler,
                                                     const IoExecutor &io_ex)
                {
                    impl.ctx->pending = PINGPONG_RECV_WRID;

                    if (pp_post_write(impl.ctx, 0))
                    {
                        fprintf(stderr, "Couldn't post write\n");
                        return ec;
                    }
                    impl.ctx->pending |= PINGPONG_SEND_WRID;
                    return ec;
                }

                template <typename Handler, typename IoExecutor>
                boost::system::error_code rdma_read(implementation_type &impl,
                                                    boost::system::error_code &ec,
                                                    Handler &handler,
                                                    const IoExecutor &io_ex)
                {
                    impl.ctx->pending = PINGPONG_RECV_WRID;

                    if (pp_post_read(impl.ctx, 0))
                    {
                        fprintf(stderr, "Couldn't post write\n");
                        return ec;
                    }
                    impl.ctx->pending |= PINGPONG_SEND_WRID;
                    return ec;
                }

                // Set a socket option.
                template <typename Option, typename Handler, typename IoExecutor>
                boost::system::error_code poll_cq(implementation_type &impl,
                                                  Option &option, boost::system::error_code &ec,
                                                  Handler &handler,
                                                  const IoExecutor &io_ex)
                {

                    ibv_wc *wcs = new ibv_wc;
                    bool is_continuation =
                        boost_asio_handler_cont_helpers::is_continuation(handler);

                    typedef rdma_cm_connect_op<Handler, IoExecutor> op;
                    typename op::ptr p = {boost::asio::detail::addressof(handler),
                                          op::ptr::allocate(handler), 0};
                    p.p = new (p.v) op(success_ec_, impl.channel_dev_fd, wcs, impl.ctx->cq_s.cq, handler, io_ex, impl.ctx->channel);

                    BOOST_ASIO_HANDLER_CREATION((reactor_.context(), *p.p, "rdma",
                                                 &impl, impl.channel_dev_fd, "rdma_test"));

                    start_op(impl, reactor::read_op,
                             p.p, is_continuation, false, false, &io_ex, 0);

                    p.v = p.p = 0;

                    return ec;
                }
            };
        }
    }
}

#include <boost/asio/detail/pop_options.hpp>

#endif // !defined(BOOST_ASIO_HAS_IOCP)
//   && !defined(BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)

#endif // BOOST_ASIO_DETAIL_REACTIVE_SOCKET_SERVICE_HPP
