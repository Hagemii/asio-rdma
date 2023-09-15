#include <iostream>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/random_access_file.hpp>

int main() {
    boost::asio::io_context io_context;
    boost::asio::random_access_file file(io_context); 

    file.open("test.txt", 
            boost::asio::random_access_file::read_write|
            boost::asio::random_access_file::create); 

    if (!file.is_open()) {
        std::cout << "Failed to open file!" << std::endl;
        return 1;
    }

    char buffer[64] = "hello";
    boost::asio::const_buffer buf(buffer, 5);

    boost::asio::async_write(file, buf,
        [&file](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::cout << "Write complete: " << length << " bytes." << std::endl;
            } else {
                std::cout << "Error during write operation: " << ec.message() << std::endl;
            }
            file.close();
        });

    io_context.run();
    std::remove("test.txt");
    return 0;
}
