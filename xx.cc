
// basic_random_access_file.hpp
  template <typename ConstBufferSequence,
      BOOST_ASIO_COMPLETION_TOKEN_FOR(void (boost::system::error_code,
        std::size_t)) WriteToken
          BOOST_ASIO_DEFAULT_COMPLETION_TOKEN_TYPE(executor_type)> // WriteToken=...
  BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_PREFIX(WriteToken,
      void (boost::system::error_code, std::size_t))	// auto
  async_write_some_at(uint64_t offset,
      const ConstBufferSequence& buffers,
      BOOST_ASIO_MOVE_ARG(WriteToken) token
        BOOST_ASIO_DEFAULT_COMPLETION_TOKEN(executor_type)) // token=...
    BOOST_ASIO_INITFN_AUTO_RESULT_TYPE_SUFFIX((
      async_initiate<WriteToken,
        void (boost::system::error_code, std::size_t)>(
          declval<initiate_async_write_some_at>(), token, offset, buffers))) // null
  {
	// 模板参数WriteToken+函数对象
	// 实参是initiate_async_write_some_at对象+token+offset+buffers
    return async_initiate<WriteToken,
      void (boost::system::error_code, std::size_t)>(
        initiate_async_write_some_at(this), token, offset, buffers);
  }

// initiate_async_write_some_at函数对象
// self_指向client对象，operator()重载表明它是一个函数对象
  class initiate_async_write_some_at
  {
  public:
	...
    template <typename WriteHandler, typename ConstBufferSequence>
    void operator()(BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        uint64_t offset, const ConstBufferSequence& buffers) const
    { ... }

  private:
    basic_random_access_file* self_;
  };

// async_write_some_at --> async_initiate
// async_initiate函数基本上没什么用，主要是套用constraint做参数检查
template <typename CompletionToken,
    BOOST_ASIO_COMPLETION_SIGNATURE... Signatures,
    typename Initiation, typename... Args>
inline typename constraint<	// 类似于c++20中约束的用法，返回值是async_result::initiate(...)
    detail::async_result_has_initiate_memfn<
      CompletionToken, Signatures...>::value,
    BOOST_ASIO_INITFN_DEDUCED_RESULT_TYPE(CompletionToken, Signatures...,
      (async_result<typename decay<CompletionToken>::type,
        Signatures...>::initiate(declval<BOOST_ASIO_MOVE_ARG(Initiation)>(),
          declval<BOOST_ASIO_MOVE_ARG(CompletionToken)>(),
          declval<BOOST_ASIO_MOVE_ARG(Args)>()...)))>::type
async_initiate(BOOST_ASIO_MOVE_ARG(Initiation) initiation,	// 函数对象
    BOOST_ASIO_NONDEDUCED_MOVE_ARG(CompletionToken) token,	// 回调函数
    BOOST_ASIO_MOVE_ARG(Args)... args)
{
  return async_result<typename decay<CompletionToken>::type,
    Signatures...>::initiate(BOOST_ASIO_MOVE_CAST(Initiation)(initiation),
      BOOST_ASIO_MOVE_CAST(CompletionToken)(token),
      BOOST_ASIO_MOVE_CAST(Args)(args)...);
}

// async_initiate --> async_result
// 1. 当sig0是函数对象，其余不是，判断为简单回调函数，从detail::completion_handler_async_result派生
// 2. 否则从async_result派生
template <typename CompletionToken, BOOST_ASIO_COMPLETION_SIGNATURES_TPARAMS>
class async_result :
  public conditional<
      detail::are_simple_completion_signatures<
        BOOST_ASIO_COMPLETION_SIGNATURES_TARGS>::value,
      detail::completion_handler_async_result<
        CompletionToken, BOOST_ASIO_COMPLETION_SIGNATURES_TARGS>,
      async_result<CompletionToken,
        BOOST_ASIO_COMPLETION_SIGNATURES_TSIMPLEARGS>
    >::type
{
  ...
};

// async_result从completion_handler_async_result派生，initiate出自这里
// completion_handler_async_result::initiate直接调用回调函数
template <typename CompletionToken, BOOST_ASIO_COMPLETION_SIGNATURES_TPARAMS>
class completion_handler_async_result
{
public:
  ...
  template <typename Initiation, typename RawCompletionToken>
  static return_type initiate(
      BOOST_ASIO_MOVE_ARG(Initiation) initiation,
      BOOST_ASIO_MOVE_ARG(RawCompletionToken) token)
  {
    BOOST_ASIO_MOVE_CAST(Initiation)(initiation)(
        BOOST_ASIO_MOVE_CAST(RawCompletionToken)(token));
  }
};

// initiate_async_write_some_at的operator被调用
  class initiate_async_write_some_at
  {
  public:
	...

    template <typename WriteHandler, typename ConstBufferSequence>
    void operator()(BOOST_ASIO_MOVE_ARG(WriteHandler) handler,
        uint64_t offset, const ConstBufferSequence& buffers) const
    {
      // If you get an error on the following line it means that your handler
      // does not meet the documented type requirements for a WriteHandler.
      BOOST_ASIO_WRITE_HANDLER_CHECK(WriteHandler, handler) type_check;

      detail::non_const_lvalue<WriteHandler> handler2(handler);
      // 直接调用client指向的服务提供的接口async_write_some_at
      self_->impl_.get_service().async_write_some_at(
          self_->impl_.get_implementation(), offset, buffers,
          handler2.value, self_->impl_.get_executor());
    }
  };
  
// io_uring_file_service.hpp的async_write_some_at接口
// 直接转发调用descriptor_service_.async_write_some_at
  template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
  void async_write_some_at(implementation_type& impl,
      uint64_t offset, const ConstBufferSequence& buffers,
      Handler& handler, const IoExecutor& io_ex)
  {
    descriptor_service_.async_write_some_at(
        impl, offset, buffers, handler, io_ex);
  }

// io_uring_descriptor_service.hpp
// async_write_some_at接口，
  template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
  void async_write_some_at(implementation_type& impl, uint64_t offset,
      const ConstBufferSequence& buffers, Handler& handler,
      const IoExecutor& io_ex)
  {
    bool is_continuation =
      boost_asio_handler_cont_helpers::is_continuation(handler);

    typename associated_cancellation_slot<Handler>::type slot
      = boost::asio::get_associated_cancellation_slot(handler);

    // Allocate and construct an operation to wrap the handler.
    // NOTE: 这里非常重要！！！
    // p在栈上分配，p.v指向在堆上分配的operator，包含回调+调度元素+io_uring底层结构
    typedef io_uring_descriptor_write_at_op<
      ConstBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { boost::asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    // p.p=p.v，p.p此时是一个初始化后的operator
    p.p = new (p.v) op(success_ec_, impl.descriptor_,
        impl.state_, offset, buffers, handler, io_ex);

    // Optionally register for per-operation cancellation.
    if (slot.is_connected())
    {
      p.p->cancellation_key_ =
        &slot.template emplace<io_uring_op_cancellation>(&io_uring_service_,
            &impl.io_object_data_, io_uring_service::write_op);
    }

    BOOST_ASIO_HANDLER_CREATION((io_uring_service_.context(), *p.p,
          "descriptor", &impl, impl.descriptor_, "async_write_some"));

	// start_op开始异步操作
	// NOTE: 这里也很重要！！！
    start_op(impl, io_uring_service::write_op, p.p, is_continuation,
        buffer_sequence_adapter<boost::asio::const_buffer,
          ConstBufferSequence>::all_empty(buffers));
    // 将p.v和p.p指针清掉，防止自动析构ptr
    p.v = p.p = 0;
  }

// io_uring_descriptor_write_op对象，封装回调函数和io_uring底层的结构
template <typename ConstBufferSequence, typename Handler, typename IoExecutor>
class io_uring_descriptor_write_op
  : public io_uring_descriptor_write_op_base<ConstBufferSequence>
{
public:
  // BOOST_ASIO_DEFINE_HANDLER_PTR这个宏定义了前面的ptr结构
  BOOST_ASIO_DEFINE_HANDLER_PTR(io_uring_descriptor_write_op);

  // io_uring_descriptor_write_op本身又是调度的一个operator，
  // 它初始化时，将do_complete函数填充为operator的函数指针上
  io_uring_descriptor_write_op(const boost::system::error_code& success_ec,
      int descriptor, descriptor_ops::state_type state,
      const ConstBufferSequence& buffers, Handler& handler,
      const IoExecutor& io_ex)
    : io_uring_descriptor_write_op_base<ConstBufferSequence>(success_ec,
        descriptor, state, buffers, &io_uring_descriptor_write_op::do_complete),
      handler_(BOOST_ASIO_MOVE_CAST(Handler)(handler)),
      work_(handler_, io_ex)
  {
  }

  static void do_complete(void* owner, operation* base,
      const boost::system::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
     ...
  }
  ...
};

// BOOST_ASIO_DEFINE_HANDLER_PTR宏定义ptr结构，内部包含三个成员h、v、p
// h指向回调函数指针，allocate在堆上分配用户回调函数，v指向堆上分配的地址
#define BOOST_ASIO_DEFINE_HANDLER_PTR(op) \
  struct ptr \
  { \
    Handler* h; \
    op* v; \
    op* p; \
    ~ptr() \
    { \
      reset(); \
    } \
    static op* allocate(Handler& handler) \
    { \
      typedef typename ::boost::asio::associated_allocator< \
        Handler>::type associated_allocator_type; \
      typedef typename ::boost::asio::detail::get_hook_allocator< \
        Handler, associated_allocator_type>::type hook_allocator_type; \
      BOOST_ASIO_REBIND_ALLOC(hook_allocator_type, op) a( \
            ::boost::asio::detail::get_hook_allocator< \
              Handler, associated_allocator_type>::get( \
                handler, ::boost::asio::get_associated_allocator(handler))); \
      return a.allocate(1); \
    } \
    ...\
  } \
  /**/
  
// start_op的逻辑
// 按照描述符管理，每个描述符下有read、write、exp三个队列，队列里放前面分配的operator
// 对于异步请求，如果该类型上的队列为空，则先分配sqe，填充，然后向io_uring提交
void io_uring_service::start_op(int op_type,
    io_uring_service::per_io_object_data& io_obj,
    io_uring_operation* op, bool is_continuation)
{
  if (!io_obj)
  {
    op->ec_ = boost::asio::error::bad_descriptor;
    post_immediate_completion(op, is_continuation);
    return;
  }

  mutex::scoped_lock io_object_lock(io_obj->mutex_);

  if (io_obj->shutdown_)
  {
    io_object_lock.unlock();
    post_immediate_completion(op, is_continuation);
    return;
  }

  if (io_obj->queues_[op_type].op_queue_.empty())
  {
    if (op->perform(false))
    {
	  // 异步完成处理分支
      io_object_lock.unlock();	// 出队异步请求
      scheduler_.post_immediate_completion(op, is_continuation);	// 将operator塞到调度器上
    }
    else
    {
      // 异步请求处理分支，此时该队列为空
      io_obj->queues_[op_type].op_queue_.push(op);
      io_object_lock.unlock();
      mutex::scoped_lock lock(mutex_);
      // 分配sqe结构
      if (::io_uring_sqe* sqe = get_sqe())
      {
        // 填充sqe结构
        op->prepare(sqe);
        ::io_uring_sqe_set_data(sqe, &io_obj->queues_[op_type]);
        scheduler_.work_started();
        // 向io_uring提交
        post_submit_sqes_op(lock);
      }
      else
      {
        lock.unlock();
        io_obj->queues_[op_type].set_result(-ENOBUFS);
        post_immediate_completion(&io_obj->queues_[op_type], is_continuation);
      }
    }
  }
  else
  {
    io_obj->queues_[op_type].op_queue_.push(op);	// 入队异步请求
    scheduler_.work_started();
  }
}

// 以async_write_some_at为例，它的调用过程为
// async_write_some_at --> async_initiate --> async_result --> async_result::initiate
// 	--> descriptor_service_.async_write_some_at
