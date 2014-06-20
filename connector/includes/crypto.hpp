#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <cstdint>

#include <random>
#include <vector>
#include <memory>

std::unique_ptr<unsigned char[]> sha256(const uint8_t *data, size_t len);
std::unique_ptr<unsigned char[]> sha256(const std::unique_ptr<unsigned char[]> & data, size_t len);
std::unique_ptr<unsigned char[]> sha256(const std::vector<uint8_t> & data);

struct randmaker64 {
	uint64_t operator()() {
		return gen();
	}
	std::random_device rd;
	std::mt19937_64 gen;
	randmaker64() : rd(), gen(rd()) {}
};

struct randmaker32 {
	uint32_t operator()() {
		return gen();
	}
	std::random_device rd;
	std::mt19937 gen;
	randmaker32() : rd(), gen(rd()) {}
};


extern struct randmaker64 nonce_gen64;
extern struct randmaker32 nonce_gen32;



#endif
