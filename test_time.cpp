#include <iostream>
#include "libs/asio/include/boost/asio.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
using boost::asio::ip::tcp;

void print(const boost::system::error_code& /*e*/)
{
    std::cout << "Hello, world! ";
}

int main()
{
    boost::asio::io_context io;
    boost::asio::ip::tcp::socket socket(io);
    socket.connect(boost::asio::ip::tcp::endpoint(boost::asio::ip::address::from_string("127.0.0.1"),6688));

    boost::asio::deadline_timer t(io, boost::posix_time::seconds(5));
    boost::asio::deadline_timer t2(io, boost::posix_time::seconds(5));
    t.async_wait(print);
    io.run();
    return 0;
}
