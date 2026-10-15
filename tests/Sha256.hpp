#ifndef ORBSIM_TESTS_SHA256_HPP
#define ORBSIM_TESTS_SHA256_HPP
//
// SHA-256, for the read records (M1-129, register decision 527): each test
// case writes the hash of every file it read as it ends, so that a later test
// rewriting the file cannot hide the version this one read. The test driver,
// scripts/run-tests.py, compares the hash with Python's hashlib over the file
// as it is now.
//
// **Written from FIPS 180-4**, sections 4.1.2, 4.2.2, 5.1.1, 5.3.3 and 6.2,
// not from another implementation. It is checked against the standard's own
// examples and against hashlib at the lengths where the padding changes shape
// -- by static_assert in tests/TestReads.cpp, at compile time, and by
// tests/test_read_records.cpp for the million-byte example, which is too long
// for the compiler to evaluate. A mistake here cannot skip a test that should
// run: a hash that differs from hashlib's makes every recorded file look
// changed, so every test runs, and the driver's self-test, which requires a
// skip, fails.
//
// constexpr, so that the examples are proved before a test runs (CLAUDE.md
// non-negotiable 7).
//
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace orb::test {

using Sha256Digest = std::array<std::uint8_t, 32>;

namespace sha256_detail {

inline constexpr std::size_t kBlockBytes = 64;
inline constexpr std::size_t kLengthBytes = 8; // the message length in bits, at a block's end
inline constexpr std::uint8_t kFirstPadByte = 0x80U;

// FIPS 180-4 section 4.2.2: the first 32 bits of the fractional parts of the
// cube roots of the first 64 primes.
inline constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U,
};

// FIPS 180-4 section 5.3.3: the first 32 bits of the fractional parts of the
// square roots of the first 8 primes.
inline constexpr std::array<std::uint32_t, 8> kInitialHash{
    0x6a09e667U,
    0xbb67ae85U,
    0x3c6ef372U,
    0xa54ff53aU,
    0x510e527fU,
    0x9b05688cU,
    0x1f83d9abU,
    0x5be0cd19U,
};

using Block = std::array<std::uint8_t, kBlockBytes>;
using State = std::array<std::uint32_t, 8>;

// FIPS 180-4 section 6.2.2, one 512-bit block into the state.
[[nodiscard]] constexpr State compress(const State& state, const Block& block) noexcept {
    std::array<std::uint32_t, 64> schedule{};
    for (std::size_t t = 0; t < 16; ++t) {
        schedule.at(t) = (std::uint32_t{block.at(4 * t)} << 24U) |
                         (std::uint32_t{block.at((4 * t) + 1)} << 16U) |
                         (std::uint32_t{block.at((4 * t) + 2)} << 8U) |
                         std::uint32_t{block.at((4 * t) + 3)};
    }
    for (std::size_t t = 16; t < 64; ++t) {
        const std::uint32_t w15 = schedule.at(t - 15);
        const std::uint32_t w2 = schedule.at(t - 2);
        const std::uint32_t sigma0 = std::rotr(w15, 7) ^ std::rotr(w15, 18) ^ (w15 >> 3U);
        const std::uint32_t sigma1 = std::rotr(w2, 17) ^ std::rotr(w2, 19) ^ (w2 >> 10U);
        schedule.at(t) = sigma1 + schedule.at(t - 7) + sigma0 + schedule.at(t - 16);
    }

    State work = state;
    for (std::size_t t = 0; t < 64; ++t) {
        const std::uint32_t a = work.at(0);
        const std::uint32_t e = work.at(4);
        const std::uint32_t bigSigma1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const std::uint32_t choose = (e & work.at(5)) ^ (~e & work.at(6));
        const std::uint32_t t1 =
            work.at(7) + bigSigma1 + choose + kRoundConstants.at(t) + schedule.at(t);
        const std::uint32_t bigSigma0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const std::uint32_t majority =
            (a & work.at(1)) ^ (a & work.at(2)) ^ (work.at(1) & work.at(2));
        const std::uint32_t t2 = bigSigma0 + majority;
        work =
            State{t1 + t2, a, work.at(1), work.at(2), work.at(3) + t1, e, work.at(5), work.at(6)};
    }

    State next{};
    for (std::size_t i = 0; i < next.size(); ++i) {
        next.at(i) = state.at(i) + work.at(i);
    }
    return next;
}

// The whole message, its bytes read through `byteAt` so that one routine
// serves a span of std::byte and a string at compile time alike.
template <typename ByteAt>
[[nodiscard]] constexpr Sha256Digest digest(std::size_t length, ByteAt byteAt) noexcept {
    State state = kInitialHash;
    const std::size_t whole = length / kBlockBytes;
    for (std::size_t b = 0; b < whole; ++b) {
        Block block{};
        for (std::size_t i = 0; i < kBlockBytes; ++i) {
            block.at(i) = byteAt((b * kBlockBytes) + i);
        }
        state = compress(state, block);
    }

    // FIPS 180-4 section 5.1.1: the rest, a 1 bit, zeros, and the length in
    // bits as a big-endian 64-bit number -- one block, or two where the rest
    // leaves no room for the length.
    std::array<std::uint8_t, 2 * kBlockBytes> tail{};
    const std::size_t rest = length - (whole * kBlockBytes);
    for (std::size_t i = 0; i < rest; ++i) {
        tail.at(i) = byteAt((whole * kBlockBytes) + i);
    }
    tail.at(rest) = kFirstPadByte;
    const std::size_t tailBytes =
        rest + 1 + kLengthBytes <= kBlockBytes ? kBlockBytes : 2 * kBlockBytes;
    const std::uint64_t bits = std::uint64_t{length} * 8U;
    for (std::size_t i = 0; i < kLengthBytes; ++i) {
        tail.at(tailBytes - 1 - i) = static_cast<std::uint8_t>(bits >> (8U * i));
    }
    for (std::size_t offset = 0; offset < tailBytes; offset += kBlockBytes) {
        Block block{};
        for (std::size_t i = 0; i < kBlockBytes; ++i) {
            block.at(i) = tail.at(offset + i);
        }
        state = compress(state, block);
    }

    Sha256Digest out{};
    for (std::size_t i = 0; i < state.size(); ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            out.at((4 * i) + j) = static_cast<std::uint8_t>(state.at(i) >> (24U - (8U * j)));
        }
    }
    return out;
}

} // namespace sha256_detail

[[nodiscard]] constexpr Sha256Digest sha256(std::span<const std::byte> bytes) noexcept {
    return sha256_detail::digest(bytes.size(), [bytes](std::size_t i) noexcept {
        return static_cast<std::uint8_t>(bytes.subspan(i).front());
    });
}

[[nodiscard]] constexpr Sha256Digest sha256(std::string_view text) noexcept {
    const std::span<const char> chars{text};
    return sha256_detail::digest(chars.size(), [chars](std::size_t i) noexcept {
        return static_cast<std::uint8_t>(chars.subspan(i).front());
    });
}

// The digest as 64 lowercase hexadecimal digits, as hashlib's hexdigest()
// spells it.
[[nodiscard]] constexpr std::array<char, 64> hexOf(const Sha256Digest& digest) noexcept {
    constexpr std::string_view kDigits = "0123456789abcdef";
    std::array<char, 64> text{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        text.at(2 * i) = kDigits.at(digest.at(i) >> 4U);
        text.at((2 * i) + 1) = kDigits.at(digest.at(i) & 0x0fU);
    }
    return text;
}

} // namespace orb::test

#endif // ORBSIM_TESTS_SHA256_HPP
