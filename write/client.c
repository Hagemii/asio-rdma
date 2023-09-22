#include <stdio.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

/*RDMA operation mode*/
enum mode
{
    M_WRITE,
    M_READ
};

struct message
{
    enum
    {
        MSG_MR,
        MSG_DONE
    } type;
    union
    {
        struct ibv_mr mr;
    } data;
};

struct context
{
    struct ibv_context *ctx;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_comp_channel *comp_channel;
    pthread_t cq_poller_thread;
};

struct connection
{
    struct rdma_cm_id *id;
    struct ibv_qp *qp;
    int connected;
    struct ibv_mr *recv_mr;
    struct ibv_mr *send_mr;
    struct ibv_mr *rdma_local_mr;
    struct ibv_mr *rdma_remote_mr;
    struct ibv_mr peer_mr;
    struct message *recv_msg;
    struct message *send_msg;
    char *rdma_local_region;
    char *rdma_remote_region;
    enum
    {
        SS_INIT,
        SS_MR_SENT,
        SS_RDMA_SENT,
        SS_DONE_SENT
    } send_state;
    enum
    {
        RS_INIT,
        RS_MR_RECV,
        RS_DONE_RECV
    } recv_state;
};

static struct context *s_ctx = NULL;
static enum mode s_mode = M_WRITE;
static const int RDMA_BUFFER_SIZE = 1024;
const int TIMEOUT_IN_MS = 500;

char *
get_peer_message_region(struct connection *conn)
{
    if (s_mode == M_WRITE)
        return conn->rdma_remote_region;
    else
        return conn->rdma_local_region;
}

void completion(struct ibv_wc *wc)
{
    struct connection *conn = (struct connection *)(uintptr_t)wc->wr_id;
    if (wc->status != IBV_WC_SUCCESS)
    {
        printf("wc->status != IBV_WC_SUCCESS");
    }

    if (wc->opcode & IBV_WC_RECV)
    {
        conn->recv_state++;

        if (conn->recv_msg->type == MSG_MR)
        {
            /*收到服务端发送过来的mr,key等信息*/
            memcpy(&conn->peer_mr, &conn->recv_msg->data.mr, sizeof(conn->peer_mr));
            struct ibv_recv_wr wr, *bad_wr = NULL;
            struct ibv_sge sge;

            wr.wr_id = (uintptr_t)conn;
            wr.next = NULL;
            wr.sg_list = &sge;
            wr.num_sge = 1;
            sge.addr = (uintptr_t)conn->recv_msg;
            sge.length = sizeof(struct message);
            sge.lkey = conn->recv_mr->lkey;
            /*预留recv buffer*/
            ibv_post_recv(conn->qp, &wr, &bad_wr);
        }
    }
    else
    {
        conn->send_state++;
    }

    /*客户端经历了一次recv和一次send以后就会进来*/
    if ((conn->send_state == SS_MR_SENT) && (conn->recv_state == RS_MR_RECV))
    {
        struct ibv_send_wr wr, *bad_wr = NULL;
        struct ibv_sge sge;

        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)conn;
        wr.opcode = (s_mode == M_WRITE) ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = (uintptr_t)conn->peer_mr.addr;
        wr.wr.rdma.rkey = conn->peer_mr.rkey;
        sge.addr = (uintptr_t)conn->rdma_local_region;
        sge.length = RDMA_BUFFER_SIZE;
        sge.lkey = conn->rdma_local_mr->lkey;
        /*假如是写模式，客户端把conn->rdma_local_region缓冲区的内容写入到服务端，
        也就是写入conn->peer_mr.addr，如果是读模式，则把服务端的conn->peer_mr.addr
        地址的内容写入到rdma_local_region*/
        ibv_post_send(conn->qp, &wr, &bad_wr);

        /*结束了，为了能走进rdma_disconnect*/
        conn->send_msg->type = MSG_DONE;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uintptr_t)conn;
        wr.opcode = IBV_WR_SEND;
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.send_flags = IBV_SEND_SIGNALED;
        sge.addr = (uintptr_t)conn->send_msg;
        sge.length = sizeof(struct message);
        sge.lkey = conn->send_mr->lkey;
        while (!conn->connected)
            ;
        ibv_post_send(conn->qp, &wr, &bad_wr);
    }
    else if ((conn->send_state == SS_DONE_SENT) && (conn->recv_state == RS_DONE_RECV))
    {
        printf("remote buffer: %s\n", get_peer_message_region(conn));
        rdma_disconnect(conn->id);
    }
    return;
}

void *
poll_cq(void *ctx)
{
    struct ibv_cq *cq;
    struct ibv_wc wc;

    while (1)
    {
        // 监听完成通道
        ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx);
        //确认完成事件
        ibv_ack_cq_events(cq, 1);
        // 在下一个完成事件到来时，接受通知
        ibv_req_notify_cq(cq, 0);
        while (ibv_poll_cq(cq, 1, &wc))
            completion(&wc);
    }
    return NULL;
}

void *
get_local_message_region(void *context)
{
    if (s_mode == M_WRITE)
        return ((struct connection *)context)->rdma_local_region;
    else
        return ((struct connection *)context)->rdma_remote_region;
}

int resolved(struct rdma_cm_id *id)
{
    struct connection *conn;
    struct ibv_qp_init_attr qp_attr;
    struct ibv_recv_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    if (s_ctx)
    {
        if (s_ctx->ctx != id->verbs)
        {
            printf("cannot handle events in more than one context\n");
            return -1;
        }
    }

    s_ctx = (struct context *)malloc(sizeof(struct context));
    s_ctx->ctx = id->verbs;

    s_ctx->pd = ibv_alloc_pd(s_ctx->ctx);
    s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx);
    s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0);
    ibv_req_notify_cq(s_ctx->cq, 0);

    pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL);

    memset(&qp_attr, 0, sizeof(qp_attr));
    qp_attr.send_cq = s_ctx->cq;
    qp_attr.recv_cq = s_ctx->cq;
    qp_attr.qp_type = IBV_QPT_RC;
    qp_attr.cap.max_send_wr = 10;
    qp_attr.cap.max_recv_wr = 10;
    qp_attr.cap.max_send_sge = 1;
    qp_attr.cap.max_recv_sge = 1;

    if (rdma_create_qp(id, s_ctx->pd, &qp_attr))
    {
        printf("rdma_create_qp error\n");
    }

    id->context = conn = (struct connection *)malloc(sizeof(struct connection));
    conn->id = id;
    conn->qp = id->qp;

    conn->send_state = SS_INIT;
    conn->recv_state = RS_INIT;
    conn->connected = 0;

    conn->send_msg = malloc(sizeof(struct message));
    conn->recv_msg = malloc(sizeof(struct message));

    conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);
    conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE);

    conn->send_mr = ibv_reg_mr(s_ctx->pd, conn->send_msg, sizeof(struct message), 0);
    if (!conn->send_mr)
    {
        printf("ibv_reg_mr send_mr error\n");
    }

    conn->recv_mr = ibv_reg_mr(s_ctx->pd, conn->recv_msg, sizeof(struct message),
                               IBV_ACCESS_LOCAL_WRITE);
    if (!conn->recv_mr)
    {
        printf("ibv_reg_mr recv_mr error\n");
    }

    conn->rdma_local_mr = ibv_reg_mr(s_ctx->pd, conn->rdma_local_region, RDMA_BUFFER_SIZE,
                                     ((s_mode == M_WRITE) ? 0 : IBV_ACCESS_LOCAL_WRITE));
    if (!conn->rdma_local_mr)
    {
        printf("ibv_reg_mr local_mr error\n");
    }

    conn->rdma_remote_mr = ibv_reg_mr(s_ctx->pd, conn->rdma_remote_region,
                                      RDMA_BUFFER_SIZE, ((s_mode == M_WRITE) ? (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE) : IBV_ACCESS_REMOTE_READ));
    if (!conn->rdma_remote_mr)
    {
        printf("ibv_reg_mr rdma_remote_mr error\n");
    }

    wr.wr_id = (uintptr_t)conn;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    sge.addr = (uintptr_t)conn->recv_msg;
    sge.length = sizeof(struct message);
    sge.lkey = conn->recv_mr->lkey;

    /*客户端先准备好recv buffer*/
    if (ibv_post_recv(conn->qp, &wr, &bad_wr))
    {
        printf("ibv_post_recv error\n");
    }

    sprintf(get_local_message_region(id->context), "message from active/client side with pid %d", getpid());

    if (rdma_resolve_route(id, TIMEOUT_IN_MS))
    {
        printf("rdma_resolve_route error\n");
    }
    return 0;
}

int route(struct rdma_cm_id *id)
{
    struct rdma_conn_param params;
    memset(&params, 0, sizeof(params));

    params.initiator_depth = params.responder_resources = 1;
    params.rnr_retry_count = 7;
    /*客户端发起连接*/
    rdma_connect(id, &params);
    return 0;
}

int connection(struct rdma_cm_id *id)
{
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;

    ((struct connection *)id->context)->connected = 1;
    struct connection *conn = (struct connection *)id->context;

    /*把rdma_remote_mr等信息拷贝到send_msg*/
    conn->send_msg->type = MSG_MR;
    memcpy(&conn->send_msg->data.mr, conn->rdma_remote_mr, sizeof(struct ibv_mr));

    memset(&wr, 0, sizeof(wr));
    wr.wr_id = (uintptr_t)conn;
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uintptr_t)conn->send_msg;
    sge.length = sizeof(struct message);
    sge.lkey = conn->send_mr->lkey;

    while (!conn->connected)
        ;

    /*服务端接受请求以后进入该函数，客户端把实现准备的好mr,key等信息下发给服务端*/
    if (ibv_post_send(conn->qp, &wr, &bad_wr))
    {
        printf("ibv_post_send error\n");
    }
    return 0;
}

int disconnect(struct rdma_cm_id *id)
{
    struct connection *conn = (struct connection *)id->context;

    rdma_destroy_qp(conn->id);
    ibv_dereg_mr(conn->send_mr);
    ibv_dereg_mr(conn->recv_mr);
    ibv_dereg_mr(conn->rdma_local_mr);
    ibv_dereg_mr(conn->rdma_remote_mr);
    free(conn->send_msg);
    free(conn->recv_msg);
    free(conn->rdma_local_region);
    free(conn->rdma_remote_region);
    rdma_destroy_id(conn->id);
    free(conn);
    return 1;
}

int client_event(struct rdma_cm_event *event)
{
    int ret = 0;
    if (event->event == RDMA_CM_EVENT_ADDR_RESOLVED)
    {
        ret = resolved(event->id);
        printf("RDMA_CM_EVENT_ADDR_RESOLVED\n");
    }
    else if (event->event == RDMA_CM_EVENT_ROUTE_RESOLVED)
    {
        ret = route(event->id);
        printf("RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    }
    else if (event->event == RDMA_CM_EVENT_ESTABLISHED)
    {
        ret = connection(event->id);
        printf("RDMA_CM_EVENT_ESTABLISHED\n");
    }
    else if (event->event == RDMA_CM_EVENT_DISCONNECTED)
    {
        ret = disconnect(event->id);
        printf("RDMA_CM_EVENT_DISCONNECTED\n");
    }
    else
    {
        fprintf(stdout, "on_event: %d\n", event->event);
    }
    return ret;
}

int main(int argc, char **argv)
{
    struct addrinfo *addr;
    struct rdma_cm_event *event = NULL;
    struct rdma_cm_id *conn = NULL;
    struct rdma_event_channel *ec = NULL;

    if (argc != 4)
    {
        fprintf(stdout, "usage:<mode> <server-address> <server-port>\n");
        exit(-1);
    }

    if (!strcmp(argv[1], "write"))
    {
        s_mode = M_WRITE;
    }
    else if (!strcmp(argv[1], "read"))
    {
        s_mode = M_READ;
    }
    else
    {
        fprintf(stdout, "usage:<mode> <server-address> <server-port>\n");
        exit(-1);
    }

    if (getaddrinfo(argv[2], argv[3], NULL, &addr))
    {
        printf("getaddrinfo error\n");
    }

    ec = rdma_create_event_channel();
    if (!ec)
    {
        printf("rdma_create_event_channel error\n");
    }

    if (rdma_create_id(ec, &conn, NULL, RDMA_PS_TCP))
    {
        printf("rdma_create_id error\n");
    }

    if (rdma_resolve_addr(conn, NULL, addr->ai_addr, TIMEOUT_IN_MS))
    {
        printf("rdma_resolve_addr error\n");
    }
    freeaddrinfo(addr);

    while (!rdma_get_cm_event(ec, &event))
    {
        struct rdma_cm_event event_copy;
        memcpy(&event_copy, event, sizeof(*event));
        rdma_ack_cm_event(event);

        if (client_event(&event_copy))
            break;
    }
    rdma_destroy_event_channel(ec);
    return 0;
}
