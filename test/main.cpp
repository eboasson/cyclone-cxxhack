#include <chrono>
#include <thread>
#include <array>
#include <span>
#include <iostream>

#include "cxxhack.hpp"
#include "type.hpp"

using namespace cyclonedds;
using namespace std::chrono_literals;

std::ostream& operator<<(std::ostream& os, const std::vector<bool>& bs)
{
  os << "{";
  for (auto b : bs)
    os << b;
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TestType& t)
{
  os << "key " << t.k() << " bs " << t.bs();
  return os;
}

int main(int argc, const char * argv[])
{
  Participant dp{};
  Topic<TestType> tp{dp, "wauwels", Reliable(10s) + KeepLast(3)};
  Publisher pub{dp};
  Writer<TestType> wr{pub, tp};
  auto rd = create_reader(dp, tp);
  std::this_thread::sleep_for(4s);

  std::vector<TestType> data;
  {
    unsigned a = 1, b = 0;
    for (size_t n = 1; n <= 11; n++) {
      data.push_back(TestType{ "F" + std::to_string(n), {
        (a&64)!=0, (a&32)!=0, (a&16)!=0, (a&8)!=0, (a&4)!=0, (a&2)!=0, (a&1)!=0 }
      });
      unsigned t = a + b; b = a; a = t;
    }
    wr << data[0] << data[1] << data[2];
    wr.write(data[3]);
    data.erase(data.begin(), data.begin()+4);
    wr.write(data);
  }

  Waitset<int> ws{};
  ws.attach(rd);
  auto rc = rd.create_readcondition();
  ws.attach(rc, 3);
  if (!ws.wait(100ms))
    std::cout << "huh?" << std::endl;
  std::array<int, 5> wres_buf;
  auto wres = ws.wait(wres_buf, 100ms);
  for (auto w : wres)
    std::cout << w << std::endl;
  
  std::array<std::pair<TestType, dds_sample_info_t>, 3> xs_buf;
  while (true)
  {
    auto xs = rd.take(xs_buf);
    if (xs.size() == 0)
      break;
    for (const auto& x : xs) {
      std::cout << x.second.source_timestamp << " ih " << x.second.instance_handle << " " << x.first << std::endl;
    }
  }

  std::this_thread::sleep_for(1s);
  return 0;
}
