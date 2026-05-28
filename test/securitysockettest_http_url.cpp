// Url (RFC 3986 percent-encoding) unit tests.
//
// Exercises the static encode / decode / tryDecode helpers that the HTTP
// client (outgoing query) and server (incoming pathParam / query) rely on
// for automatic transcoding. No socket or libcurl dependency — pure
// in-process tests of the codec.

#include <gtest/gtest.h>

#include "../src/implementation/http/Url.hpp"

#include <string>

using Bn3Monkey::Url;

TEST(UrlEncode, UnreservedSetPassesThrough) {
    // RFC 3986 §2.3 — ALPHA / DIGIT / '-' / '_' / '.' / '~' must NOT be
    // percent-encoded. Anything else must be.
    const std::string unreserved =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789-_.~";
    EXPECT_EQ(unreserved, Url::encode(unreserved));
}

TEST(UrlEncode, ReservedAndDelimitersGetPercentEncoded) {
    // Sub-delims + gen-delims + space must all be %XX.
    EXPECT_EQ("%20",   Url::encode(" "));
    EXPECT_EQ("%2F",   Url::encode("/"));
    EXPECT_EQ("%3F",   Url::encode("?"));
    EXPECT_EQ("%23",   Url::encode("#"));
    EXPECT_EQ("%26",   Url::encode("&"));
    EXPECT_EQ("%3D",   Url::encode("="));
    EXPECT_EQ("%2B",   Url::encode("+"));
    EXPECT_EQ("%25",   Url::encode("%"));
    EXPECT_EQ("%3A%2F%2F", Url::encode("://"));
}

TEST(UrlEncode, UsesUppercaseHexDigits) {
    // Per RFC 3986 §2.1 producers SHOULD use uppercase hex. Consumers must
    // accept both — we test the decode side for that.
    EXPECT_EQ("%0A", Url::encode("\n"));
    EXPECT_EQ("%FF", Url::encode(std::string(1, static_cast<char>(0xFF))));
}

TEST(UrlEncode, UtfEightHighBitBytesEachEncoded) {
    // "한" — U+D55C, UTF-8 = 0xED 0x95 0x9C. Each byte → %XX separately
    // (URL encoding is byte-oriented, not codepoint-oriented).
    EXPECT_EQ("%ED%95%9C", Url::encode("한"));
}

TEST(UrlEncode, EmptyAndNullInputsReturnEmpty) {
    EXPECT_EQ(std::string(), Url::encode(""));
    EXPECT_EQ(std::string(), Url::encode(static_cast<const char*>(nullptr)));
    EXPECT_EQ(std::string(), Url::encode(static_cast<const char*>(nullptr), 0));
}

TEST(UrlEncode, ExplicitLengthRespected) {
    // Length argument lets callers encode a slice without a null terminator.
    const char buf[] = "abcXYZ";
    EXPECT_EQ("abc", Url::encode(buf, 3));
}

TEST(UrlDecode, RoundTripsAllByteValues) {
    // encode then decode must yield the original byte sequence for every
    // possible byte value (0..255). Guards against off-by-one / signedness
    // bugs around 0x80..0xFF in particular.
    std::string original;
    original.reserve(256);
    for (int i = 0; i < 256; ++i) original.push_back(static_cast<char>(i));
    const std::string encoded = Url::encode(original);
    EXPECT_EQ(original, Url::decode(encoded));
}

TEST(UrlDecode, AcceptsLowercaseHexDigits) {
    // Producers SHOULD emit uppercase but decoders MUST accept lowercase.
    EXPECT_EQ(" ",    Url::decode("%20"));
    EXPECT_EQ("*",    Url::decode("%2a"));
    EXPECT_EQ("*",    Url::decode("%2A"));
    EXPECT_EQ("\xff", Url::decode("%ff"));
}

TEST(UrlDecode, PreservesPlusAsLiteral) {
    // We follow RFC 3986 strictly — '+' is not whitespace. The form-encoding
    // ('+' -> space) is a separate dialect (application/x-www-form-urlencoded)
    // that callers handle explicitly if they need it.
    EXPECT_EQ("a+b", Url::decode("a+b"));
    EXPECT_EQ("a b", Url::decode("a%20b"));
}

TEST(UrlDecode, TruncatedPercentEscapeIsError) {
    // '%' with fewer than 2 trailing chars is malformed — decode() returns
    // "" (sentinel for error), tryDecode() returns false.
    std::string out;
    EXPECT_FALSE(Url::tryDecode("ab%",  3, out));
    EXPECT_FALSE(Url::tryDecode("ab%1", 4, out));
    EXPECT_EQ(std::string(), Url::decode("ab%"));
    EXPECT_EQ(std::string(), Url::decode("ab%1"));
}

TEST(UrlDecode, NonHexAfterPercentIsError) {
    // %ZZ, %1G, %G1 — all rejected. Each guards a different branch in the
    // hexValue lookup (high nibble vs low nibble).
    std::string out;
    EXPECT_FALSE(Url::tryDecode("%ZZ", 3, out));
    EXPECT_FALSE(Url::tryDecode("%1G", 3, out));
    EXPECT_FALSE(Url::tryDecode("%G1", 3, out));
}

TEST(UrlDecode, EmptyInputIsValid) {
    // Empty input is NOT an error — distinguishes "decode of empty" from
    // "decode of malformed". This is why tryDecode exists.
    std::string out("dirty");
    EXPECT_TRUE(Url::tryDecode("", 0, out));
    EXPECT_EQ(std::string(), out);
}

TEST(UrlDecode, NullInputWithNonzeroLenIsError) {
    // Defensive: caller passed a null pointer with a positive length —
    // bail rather than dereference.
    std::string out;
    EXPECT_FALSE(Url::tryDecode(nullptr, 5, out));
}

TEST(UrlDecode, ReturnsRawBytesNotUtf8Validated) {
    // Codec is byte-oriented. Decoding %FF %FE (a not-valid-UTF-8 sequence)
    // succeeds — the caller decides if the result is meaningful for them.
    const std::string out = Url::decode("%FF%FE");
    ASSERT_EQ(2u, out.size());
    EXPECT_EQ(static_cast<unsigned char>(0xFF), static_cast<unsigned char>(out[0]));
    EXPECT_EQ(static_cast<unsigned char>(0xFE), static_cast<unsigned char>(out[1]));
}
