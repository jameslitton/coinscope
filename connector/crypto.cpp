#include <openssl/evp.h>

#include <cstdint>

#include <vector>
#include <memory>

using namespace std;


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
