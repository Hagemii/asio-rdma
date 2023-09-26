//
// detail/rdma_cm_connect_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_DETAIL_RDMA_CM_CONNECT_OP_HPP
#define BOOST_ASIO_DETAIL_RDMA_CM_CONNECT_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>
#include <boost/asio/detail/bind_handler.hpp>
#include <boost/asio/detail/fenced_block.hpp>
#include <boost/asio/detail/handler_alloc_helpers.hpp>
#include <boost/asio/detail/handler_invoke_helpers.hpp>
#include <boost/asio/detail/handler_work.hpp>
#include <boost/asio/detail/memory.hpp>
#include <boost/asio/detail/reactor_op.hpp>

#include <boost/asio/detail/push_options.hpp>

#include <infiniband/verbs.h>
#include <fstream>
namespace boost
{
    namespace asio
    {
        namespace detail
        {

            template <typename Handler, typename IoExecutor>
            class rdma_cm_connect_op : public reactor_op
            {
            public:
                typedef Handler handler_type;
                typedef IoExecutor io_executor_type;

                BOOST_ASIO_DEFINE_HANDLER_PTR(rdma_cm_connect_op);

                rdma_cm_connect_op(const boost::system::error_code &success_ec, int descriptor, ibv_wc *wcs, ibv_cq *cq,
                                   Handler &handler, const IoExecutor &io_ex, ibv_comp_channel *channel)
                    : reactor_op(success_ec, &rdma_cm_connect_op::do_perform, &rdma_cm_connect_op::do_complete),
                      handler_(BOOST_ASIO_MOVE_CAST(Handler)(handler)),
                      work_(handler_, io_ex),
                      channel_dev_fd_(descriptor),
                      wcs_(wcs),
                      cq_(cq),
                      channel_(channel)
                //, other initialization
                {
                }

                static status do_perform(reactor_op *base)
                {
                    BOOST_ASIO_ASSUME(base != 0);
                    rdma_cm_connect_op *o(
                        static_cast<rdma_cm_connect_op *>(base));

                    struct ibv_cq *ev_cq;
                    void *ev_ctx;

                    if (ibv_get_cq_event(o->channel_, &ev_cq, &ev_ctx))
                    {
                        fprintf(stderr, "Failed to get cq_event\n");
                    }
                    ibv_req_notify_cq(o->cq_, 0);
                    ibv_wc wcs_[1];
                    int ne;
                    int num = 0;
                    for (;;)
                    {
                        // 读取一个CQE
                        ne = ibv_poll_cq(o->cq_, 1, wcs_);
                        if (ne)
                        {
                            if (wcs_[0].opcode == IBV_WC_SEND)
                            {
                                std::cout << "SEND success" << std::endl;
                            }else if(wcs_[0].opcode == IBV_WC_RDMA_READ){
                                std::cout << "READ success" << std::endl;
                            }else if(wcs_[0].opcode == IBV_WC_RDMA_WRITE){
                                std::cout << "WRITE success" << std::endl;
                            }else if(wcs_[0].opcode == IBV_WC_RECV){
                                std::cout << "RECV success" << std::endl;
                            }
                            else
                            {
                                std::cout << "opcode: " << wcs_[0].opcode << std::endl;
                            }
                            o->wcs_[num] = wcs_[0];
                            num += ne;
                        }
                        else
                        {
                            break;
                        }
                    }
                    o->bytes_transferred_ = num;
                    ibv_ack_cq_events(o->cq_, num);

                    std::ofstream file("output.txt", std::ofstream::app); // 追加写入模式
                    if (!file)
                    {
                        std::cerr << "Couldn't open file." << std::endl;
                    }
                    file << "perform " << num << std::endl;

                    return done;
                }

                static void do_complete(void *owner, operation *base,
                                        const boost::system::error_code & /*ec*/,
                                        std::size_t /*bytes_transferred*/)
                {
                    // Take ownership of the handler object.
                    BOOST_ASIO_ASSUME(base != 0);
                    rdma_cm_connect_op *o(
                        static_cast<rdma_cm_connect_op *>(base));

                    ptr p = {boost::asio::detail::addressof(o->handler_), o, o};
                    BOOST_ASIO_HANDLER_COMPLETION((*o));

                    // Take ownership of the operation's outstanding work.
                    handler_work<Handler, IoExecutor> w(
                        BOOST_ASIO_MOVE_CAST2(handler_work<Handler, IoExecutor>)(
                            o->work_));

                    BOOST_ASIO_ERROR_LOCATION(o->ec_);

                    // Make a copy of the handler so that the memory can be deallocated before
                    // the upcall is made. Even if we're not about to make an upcall, a
                    // sub-object of the handler may be the true owner of the memory associated
                    // with the handler. Consequently, a local copy of the handler is required
                    // to ensure that any owning sub-object remains valid until after we have
                    // deallocated the memory here.
                    detail::binder3<Handler, boost::system::error_code, std::size_t, ibv_wc*>
                        handler(o->handler_, o->ec_, o->bytes_transferred_, o->wcs_);
                    p.h = boost::asio::detail::addressof(handler.handler_);
                    p.reset();

                    // Make the upcall if required.
                    if (owner)
                    {
                        fenced_block b(fenced_block::half);
                        BOOST_ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_, handler.arg2_));
                        w.complete(handler, handler.handler_);
                        BOOST_ASIO_HANDLER_INVOCATION_END;
                    }

                    std::ofstream file("output.txt", std::ofstream::app); // 追加写入模式
                    if (!file)
                    {
                        std::cerr << "Couldn't open file." << std::endl;
                    }
                    file << "complete " << std::endl;
                    // TODO ... do your io completion logic
                }

                static void do_immediate(operation *base, bool, const void *io_ex)
                {
                    // Take ownership of the handler object.
                    BOOST_ASIO_ASSUME(base != 0);
                    rdma_cm_connect_op *o(static_cast<rdma_cm_connect_op *>(base));
                    ptr p = {boost::asio::detail::addressof(o->handler_), o, o};

                    // On success, assign new connection to peer socket object.
                    o->do_assign();

                    BOOST_ASIO_HANDLER_COMPLETION((*o));

                    // Take ownership of the operation's outstanding work.
                    immediate_handler_work<Handler, IoExecutor> w(
                        BOOST_ASIO_MOVE_CAST2(handler_work<Handler, IoExecutor>)(
                            o->work_));

                    BOOST_ASIO_ERROR_LOCATION(o->ec_);

                    // Make a copy of the handler so that the memory can be deallocated before
                    // the upcall is made. Even if we're not about to make an upcall, a
                    // sub-object of the handler may be the true owner of the memory associated
                    // with the handler. Consequently, a local copy of the handler is required
                    // to ensure that any owning sub-object remains valid until after we have
                    // deallocated the memory here.
                    detail::binder1<Handler, boost::system::error_code>
                        handler(o->handler_, o->ec_);
                    p.h = boost::asio::detail::addressof(handler.handler_);
                    p.reset();

                    BOOST_ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_));
                    w.complete(handler, handler.handler_, io_ex);
                    BOOST_ASIO_HANDLER_INVOCATION_END;
                }

            private:
                struct ibv_comp_channel *channel_;
                int channel_dev_fd_;
                struct ibv_cq *cq_;
                struct ibv_wc *wcs_;
                Handler handler_;
                handler_work<Handler, IoExecutor> work_;
            };

        } // namespace detail
    }     // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // BOOST_ASIO_DETAIL_REACTIVE_NULL_BUFFERS_OP_HPP
