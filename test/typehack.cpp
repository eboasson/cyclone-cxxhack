#include "sertypetrait.hpp"

#define POLLUTE_NAMESPACE
#include "type.hpp"
#include "dds/ddsi/ddsi_serdata.h"

template<> cyclonedds::impl::ddsi_sertype *cyclonedds::impl::SertypeTrait<TestType>::get_sertype()
{
  // reinterpret_cast is because of namespace trouble caused by me trying to keep
  // the application name space clean despite having to import all kinds of stuff
  // from C and the "normal" C++ binding
  return reinterpret_cast<cyclonedds::impl::ddsi_sertype *>(org::eclipse::cyclonedds::topic::TopicTraits<TestType>::getSerType());
}

template<> void cyclonedds::impl::SertypeTrait<TestType>::unref(cyclonedds::impl::ddsi_sertype *st)
{
  ddsi_sertype_unref(reinterpret_cast<::ddsi_sertype *>(st));
}
