#include <iostream>
#include "libs/asio/include/boost/asio.hpp"
#include <vector>
#include <filesystem>

int main()
{

    boost::asio::io_context env;
    boost::asio::random_access_file file(env);

    env.loop([&env, &file]()
             {
        REQUIRE(!file.is_open());
        file.open("test.txt", boost::asio::random_access_file::read_write|
                                boost::asio::random_access_file::create);   
        REQUIRE(file.is_open());
        REQUIRE(file.size() == 0);
        char buffer[64];
        auto ret = fmt::format_to_n(buffer, 64, "hello");
        boost::asio::const_buffer b(buffer, ret.size);
        file.async_write_some_at(
            0,
            b,
            [&file, &env](
                const boost::system::error_code& ec,
                std::size_t bytes_transferred){
                    REQUIRE(bytes_transferred == 5);
                    REQUIRE(!ec);
                    file.close();
                    std::filesystem::remove("test.txt");
                    env.stop();
                }
            ); });
    //   io.run();
    return 0;
}