#include "doctest.h"

#include <cstdint>
#include <string>
#include <vector>

#include "onf/primitives/sha256.h"

using namespace onf;

namespace {

std::vector<std::uint8_t> bytes_of(const std::string& text) { return std::vector<std::uint8_t>(text.begin(), text.end()); }

}  // namespace

// The digests were computed with Python's hashlib, independently of the code under test.
TEST_CASE("sha256: known digests") {
    CHECK(sha256_hex(bytes_of("abc")) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex(bytes_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("sha256: lengths around the block size, a run of 'a'") {
    struct Case { std::size_t size; const char* digest; };
    const Case cases[] = {
    {0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"},
    {3, "9834876dcfb05cb167a5c24953eba58c4ac89b1adf57f28f2f9d09af107ee8f0"},
    {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
    {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
    {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
    {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
    {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    {111, "6374f73208854473827f6f6a3f43b1f53eaa3b82c21c1a6d69a2110b2a79baad"},
    {112, "f54353008a2553262ecdc4a34749563ba0950e8b0fc8652780b0a614b99683c1"},
    {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
    {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    {127, "c57e9278af78fa3cab38667bef4ce29d783787a2f731d4e12200270f0c32320a"},
    {128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e"},
    {129, "c12cb024a2e5551cca0e08fce8f1c5e314555cc3fef6329ee994a3db752166ae"},
    {1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.size);
        CHECK(sha256_hex(std::vector<std::uint8_t>(c.size, 'a')) == c.digest);
    }
}

TEST_CASE("sha256: one million bytes") {
    CHECK(sha256_hex(std::vector<std::uint8_t>(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("sha256: the raw digest has 32 bytes and matches the hex form") {
    const auto digest = sha256(bytes_of("abc"));
    CHECK(digest.size() == 32);
    CHECK(digest[0] == 0xba);
    CHECK(digest[31] == 0xad);
}
