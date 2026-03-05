#ifndef OPENCATTUSHC_UTILS_NETWORK
#define OPENCATTUSHC_UTILS_NETWORK

#include <boost/asio/ip/address.hpp>

namespace opencattus::utils::network {

std::uint8_t subnetMaskToCIDR(const boost::asio::ip::address& addr);

}

#endif
