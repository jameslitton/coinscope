#include <openssl/evp.h>

#include "crypto.hpp"


using namespace std;

struct randmaker64 nonce_gen64;
struct randmaker32 nonce_gen32;


unique_ptr<unsigned char[]> sha256(const uint8_t *data, size_t len) {

	EVP_MD_CTX mdctx;
	EVP_MD_CTX_init(&mdctx);

	const EVP_MD *md = EVP_sha256();

	unique_ptr<unsigned char[]> rv(new unsigned char[EVP_MD_size(md)]);

	EVP_DigestInit_ex(&mdctx, md, NULL);
	EVP_DigestUpdate(&mdctx, data, len);
	EVP_DigestFinal_ex(&mdctx, rv.get(), NULL);

	EVP_MD_CTX_cleanup(&mdctx);
   
	return rv;
   
}

unique_ptr<unsigned char[]> sha256(const unique_ptr<unsigned char[]> & data, size_t len) {
	return sha256(data.get(), len);
}

unique_ptr<unsigned char[]> sha256(const vector<uint8_t> & data) {
	return sha256(data.data(), data.size());
}
