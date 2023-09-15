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
                                   Handler &handler, const IoExecutor &io_ex)
                    : reactor_op(success_ec, &rdma_cm_connect_op::do_perform, &rdma_cm_connect_op::do_complete),
                      handler_(BOOST_ASIO_MOVE_CAST(Handler)(handler)),
                      work_(handler_, io_ex),
                      channel_dev_fd_(descriptor),
                      wcs_(wcs),
                      cq_(cq)
                //, other initialization
                {
                }

                static status do_perform(reactor_op *base)
                {
                    BOOST_ASIO_ASSUME(base != 0);
                    rdma_cm_connect_op *o(
                        static_cast<rdma_cm_connect_op *>(base));
                    ibv_wc wcs_[1];
                    // int ne = ibv_poll_cq(o->cq_, 2, wcs_);
                    int ne;
                    int num = 0;
                    for (;;)
                    {
                        // 读取一个CQE
                        ne = ibv_poll_cq(o->cq_, 1, wcs_);
                        if (ne)
                        {

                            ibv_ack_cq_events(o->cq_, 1);
                            num += ne;
                        }
                        else
                        {
                            break;
                        }
                    }

                    std::cout << "perform " << num;
                    return done;
                }

                static void do_complete(void *owner, operation *base,
                                        const boost::system::error_code & /*ec*/,
                                        std::size_t /*bytes_transferred*/)
                {
                    rdma_cm_connect_op *o(
                        static_cast<rdma_cm_connect_op *>(base));

                    std::cout << "complete ";
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
