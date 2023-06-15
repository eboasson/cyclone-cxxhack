#ifndef sertypetrait_h
#define sertypetrait_h

namespace cyclonedds { namespace impl {
struct ddsi_sertype;

template<typename T>
struct SertypeTrait {
  static ddsi_sertype *get_sertype();
  static void unref(ddsi_sertype *st);
};

} }

#endif /* topictrait_h */
