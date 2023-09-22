#include <iostream>
#include <filesystem>
#include <boost/asio.hpp>

#include <list>
#include <thread>
#define BOOST_ASIO_HAS_IO_URING 1// 打开asio选项
#define BOOST_ASIO_HAS_CO_AWAIT 
#define BOOST_ASIO_HAS_STD_COROUTINE 
#define  BOOST_ASIO_HAS_IO_URING_AS_DEFAULT 
#define  GENERATING_DOCUMENTATION 
#define  ASIO_DISABLE_EPOLL 

void Handler(const boost::system::error_code &ec, std::size_t bytes_transferred)
{
    if (ec)
    {
        std::cerr << "Error during async_write_some_at operation: " << ec << "\n";
    }
    else
    {
        std::cout << "Wrote " << bytes_transferred << " bytes.\n";
    }
}
void print(const boost::system::error_code& /*e*/)
{
    std::cout << "Hello, world! ";
}

int main()
{

    boost::asio::io_context io;
    boost::asio::deadline_timer t(io, boost::posix_time::seconds(5));
    t.async_wait(print);
    io.run();
    t.async_wait(print);
    return 0;
}