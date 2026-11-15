#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace esphome {
namespace balboa_spa {

// Host-testable ordering around the UART adapter. This timeout is a local
// transport liveness limit, not a Balboa response deadline or retry interval.
class TransportWrite {
 public:
  static constexpr uint32_t FLUSH_TIMEOUT_MS = 100;

  template<typename Capacity, typename Healthy, typename Direction, typename Write, typename Flush>
  bool send(size_t length, Capacity capacity, Healthy healthy, Direction direction, Write write, Flush flush) {
    attempted_ = false;
    if (faulted_) return false;
    if (!healthy()) { faulted_ = true; return false; }
    const size_t space = capacity();
    // SIZE_MAX means the backend cannot establish nonblocking capacity.
    if (length == 0 || space == std::numeric_limits<size_t>::max() || space < length) {
      faulted_ = true;
      return false;
    }
    direction(true);
    attempted_ = true;
    write();
    bool ok = healthy() && flush(FLUSH_TIMEOUT_MS) && healthy();
    direction(false);
    // A failed drain may leave bytes pending in the UART. Never enable DE for
    // another frame in this boot, which could expose that tail on the bus.
    if (!ok) faulted_ = true;
    return ok;
  }
  bool attempted() const { return attempted_; }
  bool faulted() const { return faulted_; }

 private:
  bool attempted_{false};
  bool faulted_{false};
};

}  // namespace balboa_spa
}  // namespace esphome
