#ifndef OPENCATTUS_UTILS_NETWORK_H_
#define OPENCATTUS_UTILS_NETWORK_H_

#include <boost/asio/ip/address.hpp>

namespace opencattus::utils::network {

std::uint8_t subnetMaskToCIDR(const boost::asio::ip::address& addr);

}

#endif
