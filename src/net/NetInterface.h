
#ifndef STREAMSIGHT_NET_NET_INTERFACE_H
#define STREAMSIGHT_NET_NET_INTERFACE_H

#include <string>

namespace streamsight::net {

class NetInterface {
 public:
  static std::string GetLocalIPAddress();
};

}  // namespace streamsight::net

#endif
