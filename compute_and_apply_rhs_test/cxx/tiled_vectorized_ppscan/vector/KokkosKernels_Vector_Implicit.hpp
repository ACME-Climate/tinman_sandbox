#ifndef __KOKKOSKERNELS_VECTOR_IMPLICIT_HPP__
#define __KOKKOSKERNELS_VECTOR_IMPLICIT_HPP__

namespace KokkosKernels {
namespace Batched {
namespace Experimental {
///
/// ImplicitVector
///

template <typename fptype, int _vector_length, typename SpT>
class Vector<VectorTag<ImplicitVector<fptype, SpT>, _vector_length> > {
public:
  using type = Vector<VectorTag<ImplicitVector<fptype, SpT>, _vector_length> >;
  using value_type = fptype;
  using real_type = fptype;

  enum : int { vector_length = _vector_length };

  struct data_type {
    fptype d[_vector_length];
  };

  KOKKOS_INLINE_FUNCTION
  static const char *label() { return "Implicit Vector"; }

private:
  mutable data_type _data;

public:
  inline Vector() : _data.d({0.0}) {}

  inline Vector(const value_type val) {
    for (int i = 0; i < vector_length; i++) {
      _data.d[i] = val;
    }
  }

  inline Vector(const type &b) {
    for (int i = 0; i < vector_length; i++) {
      _data.d[i] = b._data.[i];
    }
  }

  inline type &loadAligned(value_type const *p) {
    for (int i = 0; i < vector_length; i++, p++) {
      _data.d[i] = *p;
    }
    return *this;
  }

  inline type &loadUnaligned(value_type const *p) {
    for (int i = 0; i < vector_length; i++, p++) {
      _data.d[i] = *p;
    }
    return *this;
  }

  inline void storeAligned(value_type *p) const {
    for (int i = 0; i < vector_length; i++, p++) {
      *p = _data.d[i];
    }
  }

  inline void storeUnaligned(value_type *p) const {
    for (int i = 0; i < vector_length; i++, p++) {
      *p = _data.d[i];
    }
  }

  inline value_type &operator[](int i) const { return _data.d[i]; }
};

template <typename fptype, int vector_length, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, vector_length> >
operator+(Vector<VectorTag<ImplicitVector<fptype, SpT>, vector_length> > const &a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, vector_length> > const &b) {
  a::type sum;
  for(int i = 0; i < vector_length; i++) {
    sum._data.d[i] = a._data.d[i] + b._data.d[i];
  }
  return sum;
}

template <typename fptype, int vector_length, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator+(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          const fptype b) {
  a::type sum;
  for(int i = 0; i < vector_length; i++) {
    sum._data.d[i] = a._data.d[i] + b;
  }
  return sum;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator+(const double a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  return b + a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator+=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] += b._data.d[i];
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator+=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           const double b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] += b;
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator++(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a, int) {
  Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > a0 = a;
  a += 1.0;
  return a0;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator++(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a) {
  a += 1.0;
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator-(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  a::type diff;
  for(int i = 0; i < vector_length; i++) {
    diff._data.d[i] = a._data.d[i] - b._data.d[i];
  }
  return diff;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator-(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          const double b) {
  a::type diff;
  for(int i = 0; i < vector_length; i++) {
    diff._data.d[i] = a._data.d[i] - b;
  }
  return diff;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator-(const double a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  return b - a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator-=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] -= b._data.d[i];
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator-=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           const double b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] -= b;
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator--(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a, int) {
  Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > a0 = a;
  a -= 1.0;
  return a0;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator--(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a) {
  a -= 1.0;
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator*(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  a::type prod;
  for(int i = 0; i < vector_length; i++) {
    prod._data.d[i] = a._data.d[i] * b._data.d[i];
  }
  return prod;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator*(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          const double b) {
  a::type prod;
  for(int i = 0; i < vector_length; i++) {
    prod._data.d[i] = a._data.d[i] * b;
  }
  return prod;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator*(const double a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  return b * a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator*=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] *= b._data.d[i];
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator*=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           const double b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] *= b;
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator/(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  a::type quo;
  for(int i = 0; i < vector_length; i++) {
    quo._data.d[i] = a._data.d[i] / b._data.d[i];
  }
  return quo;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator/(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a,
          const double b) {
  a::type quo;
  for(int i = 0; i < vector_length; i++) {
    quo._data.d[i] = a._data.d[i] / b;
  }
  return quo;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator/(const double a,
          Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  b::type quo;
  for(int i = 0; i < vector_length; i++) {
    quo._data.d[i] = b / a._data.d[i];
  }
  return quo;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator/=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] /= b._data.d[i];
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &
operator/=(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > &a,
           const double b) {
  for(int i = 0; i < vector_length; i++) {
    a._data.d[i] /= b;
  }
  return a;
}

template <typename fptype, typename SpT>
inline static Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> >
operator-(Vector<VectorTag<ImplicitVector<fptype, SpT>, 8> > const &a) {
  return -1 * a;
}

} // Experimental
} // Batched
} // KokkosKernels

#endif // __KOKKOSKERNELS_VECTOR_IMPLICIT_HPP__
