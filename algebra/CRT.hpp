#ifndef ALGEBRA_CRT_HPP
#define ALGEBRA_CRT_HPP
#include <NTL/ZZ.h>
#include <vector>
namespace MDL {
/// @brief Chinese Remainder Theorm
/// @param a. the remainders
/// @param p. the primes
/// @return x such that x = a[0] mod p[0]
///                     x = a[1] mod p[1] ....
template <typename T, typename R>
NTL::ZZ CRT(const std::vector<T> &a, const std::vector<R> &p);

template <>
NTL::ZZ CRT(const std::vector<long> &a, const std::vector<NTL::ZZ> &p);
} // namespace MDL
#endif // algebra//crt.hpp
