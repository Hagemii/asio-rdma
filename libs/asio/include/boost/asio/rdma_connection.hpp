
#ifndef BOOST_ASIO_RDMA_CONNECTION_HPP
#define BOOST_ASIO_RDMA_CONNECTION_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HAS_BOOST_DATE_TIME) || defined(GENERATING_DOCUMENTATION)

#include <cstddef>
#include <boost/asio/any_io_executor.hpp>

#include "detail/rdma_core_service.hpp"

#include <boost/asio/detail/handler_type_requirements.hpp>
#include <boost/asio/detail/io_object_impl.hpp>
#include <boost/asio/detail/non_const_lvalue.hpp>
#include <boost/asio/detail/throw_error.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/execution_context.hpp>
#include <boost/asio/time_traits.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost
{
    namespace asio
    {
        // struct options
        // {
        //     char *device_name;
        //     unsigned int  port_number;
        //     unsigned int queue_depth;
        // };

        struct RDMA_PortSpace
        {
            static const int InfiniBand = 0;
            static const int RoCE = 1;
            static const int Ethernet = 2;
        };

        // IO object for rdma, provides rdma functionality
        template <typename PortSpace, typename Executor = any_io_executor>
        class rdma_connection
        {
        public:
            // ....
            typedef Executor executor_type;
            using service_type = detail::rdma_core_service<PortSpace>;

            explicit rdma_connection(const executor_type &ex)
                : impl_(0, ex)
            {
            }

            template <typename ExecutionContext>
            explicit rdma_connection(ExecutionContext &context,
                                     typename constraint<
                                         is_convertible<ExecutionContext &, execution_context &>::value>::type = 0)
                : impl_(0, 0, context)
            {
            }
            ~rdma_connection() {}

        private:
            detail::io_object_impl<detail::rdma_core_service<PortSpace>, Executor> impl_;

        public: // implement interface
            template <typename Handler>
            void test1(Handler &handler)
            {
                struct options_test
                {
                    /* data */
                };
                options_test k;

                boost::system::error_code ec;
                impl_.get_service().poll_cq(impl_.get_implementation(),
                                         k,
                                         ec,
                                         handler,
                                         impl_.get_executor());
            }

            // 初始化rdma服务端，并获得可写入的内存地址
            void init_rdma_server(char *&bufAddress)
            {
                boost::system::error_code ec;
                impl_.get_service().create_rdma_core(impl_.get_implementation(), NULL, ec);
                bufAddress = impl_.get_service().get_buf(impl_.get_implementation());
            }
            void init_rdma_client(char *servername, char *&bufAddress)
            {
                boost::system::error_code ec;
                impl_.get_service().create_rdma_core(impl_.get_implementation(), servername, ec);
                bufAddress = impl_.get_service().get_buf(impl_.get_implementation());
            }

            template <typename Handler>
            void send_test(Handler &handler)
            {
                boost::system::error_code ec;
                impl_.get_service().send(impl_.get_implementation(),
                                         ec,
                                         handler,
                                         impl_.get_executor());
            }

            template <typename Handler>
            void write_test(Handler &handler)
            {
                boost::system::error_code ec;
                impl_.get_service().rdma_write(impl_.get_implementation(),
                                               ec,
                                               handler,
                                               impl_.get_executor());
            }

            template <typename Handler>
            void read_test(Handler &handler)
            {
                boost::system::error_code ec;
                impl_.get_service().rdma_read(impl_.get_implementation(),
                                               ec,
                                               handler,
                                               impl_.get_executor());
            }

            template <typename Handler>
            void test2(Handler &handler)
            {
                struct options_test
                {
                    /* data */
                };
                options_test k;

                boost::system::error_code ec;
                impl_.get_service().test2(impl_.get_implementation(),
                                          k,
                                          ec,
                                          handler,
                                          impl_.get_executor());
            }
        };
    }
}

#include <boost/asio/detail/pop_options.hpp>

#endif // defined(BOOST_ASIO_HAS_BOOST_DATE_TIME)
       // || defined(GENERATING_DOCUMENTATION)

#endif // BOOST_ASIO_BASIC_DEADLINE_TIMER_HPP