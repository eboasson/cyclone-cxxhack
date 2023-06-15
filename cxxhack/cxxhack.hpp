#ifndef cxxhack_
#define cxxhack_

#include <string>
#include <optional>
#include <chrono>
#include <ratio>
#include <span>
#include <type_traits>

#include "sertypetrait.hpp"

/* The classes below are exported */
#pragma GCC visibility push(default)

namespace cyclonedds {
namespace impl {
#include "dds/dds.h"
#include "dds/ddsi/ddsi_xqos.h"
}

using dds_sample_info_t = impl::dds_sample_info_t;
using namespace std::chrono_literals;

struct Qos {
  impl::dds_qos q;
  Qos() noexcept { impl::ddsi_xqos_init_empty(&q); }
  ~Qos() { impl::ddsi_xqos_fini(&q); }
  Qos(const Qos& u) {
    impl::ddsi_xqos_init_empty(&this->q); // do I need this?
    impl::ddsi_xqos_mergein_missing(&this->q, &u.q, ~static_cast<uint64_t>(0));
  }
  Qos operator+(const Qos& v) {
    Qos tmp = v; // hopefully triggers what I hope is my copy constructor
    impl::ddsi_xqos_mergein_missing(&tmp.q, &this->q, ~static_cast<uint64_t>(0));
    return tmp;
  }
  Qos operator<<(const Qos& v) { return *this + v; }
};

Qos BestEffort(std::chrono::nanoseconds mbt) {
  Qos q; impl::dds_qset_reliability(&q.q, impl::DDS_RELIABILITY_BEST_EFFORT, mbt.count()); return q;
}
Qos Reliable(std::chrono::nanoseconds mbt) {
  Qos q; impl::dds_qset_reliability(&q.q, impl::DDS_RELIABILITY_RELIABLE, mbt.count()); return q;
}
Qos BestEffort() { return BestEffort(100ms); }
Qos Reliable() { return Reliable(100ms); }
Qos KeepAll() { Qos q; impl::dds_qset_history(&q.q, impl::DDS_HISTORY_KEEP_ALL, 0); return q; }
Qos KeepLast(int32_t d) { Qos q; impl::dds_qset_history(&q.q, impl::DDS_HISTORY_KEEP_LAST, d); return q; }

static inline const impl::dds_qos *unwrap_qos(const std::optional<Qos>& qos) {
  return qos.has_value() ? &qos->q : nullptr;
}

struct TEntity {
  int32_t handle{0};
  std::shared_ptr<const TEntity> parent{};
  ~TEntity() {
    // also deletes all children, we don't mind too much because of the
    // backrefs to the parent and because the core library is quite good
    // at preventing use-after-free problems
    impl::dds_delete(handle);
  }
};

template<typename Q>
struct Entity { std::shared_ptr<Q> inner; };

struct TParticipant : TEntity {
  TParticipant(uint32_t domainid = DDS_DOMAIN_DEFAULT, const std::optional<Qos>& qos = std::nullopt) {
    if ((handle = impl::dds_create_participant(domainid, unwrap_qos(qos), nullptr)) < 0) throw;
  }
};
struct Participant : Entity<TParticipant> {
  Participant(uint32_t domainid = DDS_DOMAIN_DEFAULT, const std::optional<Qos>& qos = std::nullopt) {
    inner = std::make_shared<TParticipant>(domainid, qos);
  }
};

struct TPublisher : TEntity {
  TPublisher(const Participant dp, const std::optional<Qos>& qos = std::nullopt) {
    parent = dp.inner;
    if ((handle = impl::dds_create_publisher(dp.inner->handle, unwrap_qos(qos), nullptr)) < 0) throw;
  }
};
struct Publisher : Entity<TPublisher> {
  Publisher(const Participant dp, const std::optional<Qos>& qos = std::nullopt) {
    inner = std::make_shared<TPublisher>(dp, qos);
  }
};

struct TSubscriber : TEntity {
  TSubscriber(const Participant dp, const std::optional<Qos>& qos = std::nullopt) {
    parent = dp.inner;
    if ((handle = impl::dds_create_subscriber(dp.inner->handle, unwrap_qos(qos), nullptr)) < 0) throw;
  }
};
struct Subscriber : Entity<TSubscriber> {
  Subscriber(const Participant dp, const std::optional<Qos>& qos = std::nullopt) {
    inner = std::make_shared<TSubscriber>(dp, qos);
  }
};

template<typename T>
struct TTopic : TEntity {
  TTopic(const Participant pp, const std::string& name, const std::optional<Qos>& qos = std::nullopt) {
    parent = pp.inner;
    auto sertype = impl::SertypeTrait<T>::get_sertype();
    if ((handle = impl::dds_create_topic_sertype(pp.inner->handle, name.c_str(), &sertype, unwrap_qos(qos), nullptr, nullptr)) < 0) {
      impl::SertypeTrait<T>::unref(sertype);
      throw;
    }
  }
};
template<typename T>
struct Topic : Entity<TTopic<T>> {
  Topic(const Participant pp, const std::string& name, const std::optional<Qos>& qos = std::nullopt) {
    this->inner = std::make_shared<TTopic<T>>(pp, name, qos);
  }
};
  
template<typename T>
struct TWriter : TEntity {
public:
  template<typename U, typename std::enable_if<
    std::is_same<U, Participant>::value || std::is_same<U, Publisher>::value, bool>::type = true>
  TWriter(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
    parent = pp.inner;
    if ((handle = impl::dds_create_writer(pp.inner->handle, topic.inner->handle, unwrap_qos(qos), nullptr)) < 0) throw;
  }
};
template<typename T>
struct Writer : Entity<TWriter<T>> {
  template<typename U, typename std::enable_if<
    std::is_same<U, Participant>::value || std::is_same<U, Publisher>::value, bool>::type = true>
  Writer(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
    this->inner = std::make_shared<TWriter<T>>(pp, topic, qos);
  }
  void write(const T& data) {
    int32_t res = impl::dds_write(this->inner->handle, static_cast<const void *>(&data));
    if (res < 0) throw;
  }
  void write(const std::vector<T>& data) {
    for (auto const& d : data) write(d);
  }
  Writer<T> operator<<(const T& data) {
    write(data); return *this;
  }
};

template<typename T> struct TReadCondition;
template<typename T> struct ReadCondition;

template<typename T>
struct TReader : TEntity {
  template<typename U, typename std::enable_if<
    std::is_same<U, Participant>::value || std::is_same<U, Subscriber>::value, bool>::type = true>
  TReader(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
    parent = pp.inner;
    if ((handle = impl::dds_create_reader(pp.inner->handle, topic.inner->handle, unwrap_qos(qos), nullptr)) < 0)
      throw;
  }
};
template<typename T>
struct Reader : Entity<TReader<T>> {
  template<typename U, typename std::enable_if<
    std::is_same<U, Participant>::value || std::is_same<U, Subscriber>::value, bool>::type = true>
  Reader(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
    this->inner = std::make_shared<TReader<T>>(pp, topic, qos);
  }
  ReadCondition<T> create_readcondition() {
    return ReadCondition{*this};
  }
  template<size_t n>
  std::span<std::pair<T, dds_sample_info_t>, std::dynamic_extent>
  take(std::array<std::pair<T, dds_sample_info_t>, n>& xs) {
    std::array<void *, n> xsptr;
    std::array<dds_sample_info_t, n> si;
    for (size_t i = 0; i < n; i++)
      xsptr[i] = static_cast<void *>(&xs[i].first);
    int32_t m = impl::dds_take(this->inner->handle, xsptr.data(), si.data(), static_cast<int32_t>(n), static_cast<int32_t>(n));
    if (m < 0)
      throw;
    for (size_t i = 0; i < static_cast<size_t>(m); i++)
      xs[i].second = si[i];
    return std::span(xs).subspan(0, static_cast<size_t>(m));
  }
};

template<typename T>
struct TReadCondition : TEntity {
  TReadCondition(const Reader<T> rd) {
    parent = rd.inner;
    if ((handle = impl::dds_create_readcondition(rd.inner->handle, DDS_ANY_STATE)) < 0) throw;
  }
};
template<typename T>
struct ReadCondition : Entity<TReadCondition<T>> {
  ReadCondition(const Reader<T> rd) {
    this->inner = std::make_shared<TReadCondition<T>>(rd);
  }
};

template<typename X, typename std::enable_if<std::is_convertible<X, intptr_t>::value, bool>::type = true>
struct TWaitset : TEntity {
  TWaitset(const std::optional<Participant> dp = std::nullopt) {
    using dds_entity_t = impl::dds_entity_t;
    if (dp.has_value()) parent = dp->inner;
    if ((handle = impl::dds_create_waitset(dp.has_value() ? dp->inner->handle : DDS_CYCLONEDDS_HANDLE)) < 0) throw;
  }
};
template<typename X, typename std::enable_if<std::is_convertible<X, intptr_t>::value, bool>::type = true>
struct Waitset : Entity<TWaitset<X>> {
  Waitset(const std::optional<Participant> dp = std::nullopt) {
    this->inner = std::make_shared<TWaitset<X>>(dp);
  }
  template<typename E>
  void attach(const Entity<E> e, const std::optional<X> x = std::nullopt) {
    if (impl::dds_waitset_attach(this->inner->handle, e.inner->handle, x.has_value() ? static_cast<intptr_t>(x.value()) : 0) < 0) throw;
  }
  bool wait(const std::chrono::nanoseconds to) {
    int32_t m = impl::dds_waitset_wait(this->inner->handle, nullptr, 0, to.count());
    if (m < 0) throw;
    return m > 0;
  }
  template<size_t n, typename std::enable_if<std::is_convertible<X, std::array<intptr_t, n>>::value, bool>::type = true>
  std::span<X, std::dynamic_extent> wait(std::array<X, n>& xs, const std::chrono::nanoseconds to) {
    int32_t m = impl::dds_waitset_wait(this->inner->handle, xs.data(), n, to.count());
    if (m < 0) throw;
    return std::span(xs).subspan(0, static_cast<size_t>(m));
  }
  template<size_t n, typename std::enable_if<!std::is_convertible<X, std::array<intptr_t, n>>::value, bool>::type = true>
  std::span<X, std::dynamic_extent> wait(std::array<X, n>& xs, const std::chrono::nanoseconds to) {
    std::array<intptr_t, n> tmp;
    int32_t m = impl::dds_waitset_wait(this->inner->handle, tmp.data(), n, to.count());
    if (m < 0) throw;
    size_t lim = static_cast<size_t>(m) < n ? static_cast<size_t>(m) : n;
    for (size_t i = 0; i < lim; i++)
      xs[i] = static_cast<X>(tmp[i]);
    return std::span(xs).subspan(0, static_cast<size_t>(m));
  }
};

inline Participant create_participant(uint32_t domainid = DDS_DOMAIN_DEFAULT, const std::optional<Qos>& qos = std::nullopt) {
  return Participant(domainid, qos);
}
inline Publisher create_publisher(const Participant dp, const std::optional<Qos>& qos) {
  return Publisher(dp, qos);
}
inline Subscriber create_subscriber(const Participant dp, const std::optional<Qos>& qos) {
  return Subscriber(dp, qos);
}
template<typename T>
Topic<T> create_topic(const Participant pp, const std::string& name, const std::optional<Qos>& qos = std::nullopt) {
  return Topic<T>(pp, name, qos);
}
template<typename T, typename U>
Writer<T> create_writer(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
  return Writer<T>(pp, topic, qos);
}
template<typename T, typename U>
Reader<T> create_reader(const U pp, const Topic<T> topic, const std::optional<Qos>& qos = std::nullopt) {
  return Reader<T>(pp, topic, qos);
}

}

#pragma GCC visibility pop
#endif
