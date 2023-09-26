#include <iostream>
#include "libs/asio/include/boost/asio.hpp"
#include <chrono>
#include <thread>
void print(const boost::system::error_code & /*e*/)
{
    std::cout << "Hello, world! ";
}
// 定义一个端口空间类型
struct MyPortSpace
{
    // 添加端口空间的具体定义
};
struct RDMA_PortSpace
{
    static const int InfiniBand = 0;
    static const int RoCE = 1;
    static const int Ethernet = 2;
};

int main()
{

    auto handler = [](const boost::system::error_code &ec, std::size_t cq_num, ibv_wc *wcs_)
    {
        if (!ec)
        {
            std::cout << "Poll CQ completed successfully. " << cq_num << std::endl;
        }
        else
        {
            std::cerr << "Lambda handler: Async operation failed with error: " << ec.message() << std::endl;
        }
    };

    boost::asio::io_context io;
    boost::asio::rdma_connection<MyPortSpace> rdma(io);
    boost::asio::io_context::work worker(io);
    char* buf;

    rdma.init_rdma_server(buf);

    // rdma.send_test(handler);
    rdma.test1(handler);
  

 //   rdma.send_test(handler);


    std::thread thread([&](){ io.run(); });
 //   std::cout<<buf<< std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));
  
 
thread.join();  
    return 0;
}