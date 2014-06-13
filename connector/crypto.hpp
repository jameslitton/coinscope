#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <cstdint>

#include <vector>
#include <memory>

std::unique_ptr<unsigned char[]> sha256(const uint8_t *data, size_t len);
std::unique_ptr<unsigned char[]> sha256(const std::unique_ptr<unsigned char[]> & data, size_t len);
std::unique_ptr<unsigned char[]> sha256(const std::vector<uint8_t> & data);


#endif
