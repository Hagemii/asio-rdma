//
// detail/io_uring_descriptor_write_at_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2023 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BOOST_ASIO_DETAIL_IO_URING_DESCRIPTOR_WRITE_AT_OP_HPP
#define BOOST_ASIO_DETAIL_IO_URING_DESCRIPTOR_WRITE_AT_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <boost/asio/detail/config.hpp>

#if defined(BOOST_ASIO_HAS_IO_URING)

#include <boost/asio/detail/bind_handler.hpp>
#include <boost/asio/detail/buffer_sequence_adapter.hpp>
#include <boost/asio/detail/descriptor_ops.hpp>
#include <boost/asio/detail/fenced_block.hpp>
#include <boost/asio/detail/handler_work.hpp>
#include <boost/asio/detail/io_uring_operation.hpp>
#include <boost/asio/detail/memory.hpp>

#include <boost/asio/detail/push_options.hpp>

namespace boost {
namespace asio {
namespace detail {

template <typename ConstBufferSequence>
class io_uring_descriptor_write_at_op_base : public io_uring_operation
{
public:
  io_uring_descriptor_write_at_op_base(
      const boost::system::error_code& success_ec, int descriptor,
      descriptor_ops::state_type state, uint64_t offset,
      const ConstBufferSequence& buffers, func_type complete_func)
    : io_uring_operation(success_ec,
        &io_uring_descriptor_write_at_op_base::do_prepare,
        &io_uring_descriptor_write_at_op_base::do_perform, complete_func),
      descriptor_(descriptor),
      state_(state),
      offset_(offset),
      buffers_(buffers),
      bufs_(buffers)
  {
  }

  static void do_prepare(io_uring_operation* base, ::io_uring_sqe* sqe)
  {
    BOOST_ASIO_ASSUME(base != 0);
    io_uring_descriptor_write_at_op_base* o(
        static_cast<io_uring_descriptor_write_at_op_base*>(base));

    if ((o->state_ & descriptor_ops::internal_non_blocking) != 0)
    {
      ::io_uring_prep_poll_add(sqe, o->descriptor_, POLLOUT);
    }
    else if (o->bufs_.is_single_buffer && o->bufs_.is_registered_buffer)
    {
      ::io_uring_prep_write_fixed(sqe, o->descriptor_,
          o->bufs_.buffers()->iov_base, o->bufs_.buffers()->iov_len,
          o->offset_, o->bufs_.registered_id().native_handle());
    }
    else
    {
      ::io_uring_prep_writev(sqe, o->descriptor_,
          o->bufs_.buffers(), o->bufs_.count(), o->offset_);
    }
  }

  static bool do_perform(io_uring_operation* base, bool after_completion)
  {
    BOOST_ASIO_ASSUME(base != 0);
    io_uring_descriptor_write_at_op_base* o(
        static_cast<io_uring_descriptor_write_at_op_base*>(base));

    if ((o->state_ & descriptor_ops::internal_non_blocking) != 0)
    {
      if (o->bufs_.is_single_buffer)
      {
        return descriptor_ops::non_blocking_write_at1(o->descriptor_,
            o->offset_, o->bufs_.first(o->buffers_).data(),
            o->bufs_.first(o->buffers_).size(), o->ec_,
            o->bytes_transferred_);
      }
      else
      {
        return descriptor_ops::non_blocking_write_at(o->descriptor_,
            o->offset_, o->bufs_.buffers(), o->bufs_.count(),
            o->ec_, o->bytes_transferred_);
      }
    }

    if (o->ec_ && o->ec_ == boost::asio::error::would_block)
    {
      o->state_ |= descriptor_ops::internal_non_blocking;
      return false;
    }

    return after_completion;
  }

private:
  int descriptor_;
  descriptor_ops::state_type state_;
  uint64_t offset_;
  ConstBufferSequence buffers_;
  buffer_sequence_adapter<boost::asio::const_buffer,
      ConstBufferSequence> bufs_;
};

template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
class io_uring_descriptor_write_at_op
  : public io_uring_descriptor_write_at_op_base<ConstBufferSequence>
{
public:
  BOOST_ASIO_DEFINE_HANDLER_PTR(io_uring_descriptor_write_at_op);

  io_uring_descriptor_write_at_op(const boost::system::error_code& success_ec,
      int descriptor, descriptor_ops::state_type state, uint64_t offset,
      const ConstBufferSequence& buffers, Handler& handler,
      const IoExecutor& io_ex)
    : io_uring_descriptor_write_at_op_base<ConstBufferSequence>(
        success_ec, descriptor, state, offset, buffers,
        &io_uring_descriptor_write_at_op::do_complete),
      handler_(BOOST_ASIO_MOVE_CAST(Handler)(handler)),
      work_(handler_, io_ex)
  {
  }

  static void do_complete(void* owner, operation* base,
      const boost::system::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    // Take ownership of the handler object.
    BOOST_ASIO_ASSUME(base != 0);
    io_uring_descriptor_write_at_op* o
      (static_cast<io_uring_descriptor_write_at_op*>(base));
    ptr p = { boost::asio::detail::addressof(o->handler_), o, o };

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
    detail::binder2<Handler, boost::system::error_code, std::size_t>
      handler(o->handler_, o->ec_, o->bytes_transferred_);
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

//     在这段代码中，﻿ptr p 主要的作用是管理和维护 ﻿o->handler_ 对象的生命周期和资源。
// 当创建 ﻿ptr p 的时候，它接收了 ﻿o->handler_ 的地址，并关联了 ﻿o 这个 ﻿op 对象。一旦 ﻿p 被创建，实际上它就开始管理原始的 ﻿o->handler_ 这个 handler 对象的内存。这是因为 ﻿p.p 和/或 ﻿p.v 会被设置为 ﻿o 对象的地址，由 ﻿p 对象对其进行管理。
// 然后，代码创建了一个新的 handler 对象，这个新对象也绑定了 ﻿o->handler_ 的信息，并改变了 ﻿p.h 的指向，使其指向新创建的 handler 对象。
// 在调用 ﻿p.reset() 时，原始的 ﻿o->handler_ 相关的内存会被立即释放。其中，﻿p.p 或者 ﻿p.v 这部分由 ﻿p 管理的内存会被释放。然而新创建的 handler 对象，由于是在栈上创建的，它的生命周期会持续到函数结束。这就是 ﻿p.reset() 执行后并没有立即使用 ﻿p 的原因。
// 之所以如此设计，主要目的是确保即便在原始的 ﻿o->handler_ 对象的资源被释放之后，仍可以安全地访问和调用新创建的 handler 对象。这在多线程或异步场景是非常重要的，并可能避免潜在的内存错误或竞态条件。
  }

private:
  Handler handler_;
  handler_work<Handler, IoExecutor> work_;
};

} // namespace detail
} // namespace asio
} // namespace boost

#include <boost/asio/detail/pop_options.hpp>

#endif // defined(BOOST_ASIO_HAS_IO_URING)

#endif // BOOST_ASIO_DETAIL_IO_URING_DESCRIPTOR_WRITE_AT_OP_HPP
