#include "cxxhack.hpp"

namespace cyclonedds {

Participant create_participant(uint32_t domainid, const std::optional<Qos>& qos) {
  return Participant(domainid, qos);
}

Publisher create_publisher(Participant dp, const std::optional<Qos>& qos) {
  return Publisher(dp, qos);
}

Subscriber create_subscriber(Participant dp, const std::optional<Qos>& qos) {
  return Subscriber(dp, qos);
}

}
