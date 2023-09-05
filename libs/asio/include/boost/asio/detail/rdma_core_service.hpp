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

#include <boost/asio/buffer.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/detail/buffer_sequence_adapter.hpp>
#include <boost/asio/detail/memory.hpp>
#include <boost/asio/detail/noncopyable.hpp>

#include <boost/asio/detail/reactor.hpp>
#include <boost/asio/detail/reactor_op.hpp>
#include <boost/asio/detail/socket_holder.hpp>
#include <boost/asio/detail/socket_ops.hpp>
#include <boost/asio/detail/socket_types.hpp>

#include <boost/asio/detail/push_options.hpp>

#include <infiniband/verbs.h>

namespace boost
{
    namespace asio
    {
        namespace detail
        {

            struct rdma_cm_id_data
            {
                // Define members of rdma_mr_state structure
                // ...
            };
            struct rdma_mr_state
            {
                // Define members of rdma_mr_state structure
                // ...
            };

            template <typename PortSpace>
            class rdma_core_service : public execution_context_service_base<rdma_core_service<PortSpace>>
            {
            public:
                // The implementation type of the cm id.
                struct implementation_type
                {
                    // The native cm representation.
                    rdma_cm_id_data *cm_id_data_;

                    // state for memory region
                    rdma_mr_state mr_state_;

                    // if there is a queue pair attached to this io object
                    bool has_qp_;
                };

                // Construct a new implementation.
                inline void construct(implementation_type &impl)
                {
                    impl.cm_id_data_ = new rdma_cm_id_data;
                    impl.mr_state_ = RDMA_MR_STATE_UNINIT;
                    impl.has_qp_ = false;
                }

                // Destroy a implementation
                inline void destroy(implementation_type &impl)
                {
                    delete impl.cm_id_data_;
                }

                // move constructor
                inline void move_construct(implementation_type &impl, implementation_type &other_impl)
                {
                    impl.cm_id_data_ = other_impl.cm_id_data_;
                    impl.mr_state_ = other_impl.mr_state_;
                    impl.has_qp_ = other_impl.has_qp_;
                    other_impl.cm_id_data_ = nullptr;
                    other_impl.mr_state_ = RDMA_MR_STATE_UNINIT;
                    other_impl.has_qp_ = false;
                }

                // move assign
                inline void move_assign(implementation_type &impl,
                                        rdma_core_service &other_service, implementation_type &other_impl)
                {
                    destroy(impl);
                    move_construct(impl, other_impl);
                }

                /// Destructor.
                ASIO_DECL virtual ~rdma_core_service()
                {
                    destroy(impl);
                }

                /// Destroy all user-defined handler objects owned by the service.
                ASIO_DECL virtual void shutdown(){
                    destroy(impl);
                }


            };

        }
    }
}

#include <boost/asio/detail/pop_options.hpp>

#endif // !defined(BOOST_ASIO_HAS_IOCP)
       //   && !defined(BOOST_ASIO_HAS_IO_URING_AS_DEFAULT)

#endif // BOOST_ASIO_DETAIL_REACTIVE_SOCKET_SERVICE_HPP
