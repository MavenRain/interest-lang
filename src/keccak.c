/* Keccak-256: the Keccak-f[1600] permutation with rate 136 bytes. Lanes
 * are read and written little-endian byte by byte, so the result does not
 * depend on the byte order of the host. */
#include "keccak.h"
#include <stdint.h>

enum { KECCAK_RATE = 136, KECCAK_ROUNDS = 24 };

static const uint64_t round_constant[KECCAK_ROUNDS] = {
  0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808aull,
  0x8000000080008000ull, 0x000000000000808bull, 0x0000000080000001ull,
  0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008aull,
  0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000aull,
  0x000000008000808bull, 0x800000000000008bull, 0x8000000000008089ull,
  0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
  0x000000000000800aull, 0x800000008000000aull, 0x8000000080008081ull,
  0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull
};

/* The rho rotation and the pi lane of each step of the rho-pi walk. */
static const unsigned rotation[24] = {
  1, 3, 6, 10, 15, 21, 28, 36, 45, 55, 2, 14,
  27, 41, 56, 8, 25, 43, 62, 18, 39, 61, 20, 44
};
static const unsigned lane[24] = {
  10, 7, 11, 17, 18, 3, 5, 16, 8, 21, 24, 4,
  15, 23, 19, 13, 12, 2, 20, 14, 22, 9, 6, 1
};

static uint64_t rotate(uint64_t value, unsigned bits) {
  return (value << bits) | (value >> (64 - bits));
}

static void permute(uint64_t state[25]) {
  for (unsigned round = 0; round < KECCAK_ROUNDS; round++) {
    uint64_t column[5];
    for (unsigned x = 0; x < 5; x++)
      column[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
    for (unsigned x = 0; x < 5; x++) {
      uint64_t theta = column[(x + 4) % 5] ^ rotate(column[(x + 1) % 5], 1);
      for (unsigned y = 0; y < 25; y += 5)
        state[y + x] ^= theta;
    }
    uint64_t carried = state[1];
    for (unsigned step = 0; step < 24; step++) {
      uint64_t next = state[lane[step]];
      state[lane[step]] = rotate(carried, rotation[step]);
      carried = next;
    }
    for (unsigned y = 0; y < 25; y += 5) {
      uint64_t row[5];
      for (unsigned x = 0; x < 5; x++)
        row[x] = state[y + x];
      for (unsigned x = 0; x < 5; x++)
        state[y + x] = row[x] ^ (~row[(x + 1) % 5] & row[(x + 2) % 5]);
    }
    state[0] ^= round_constant[round];
  }
}

static void absorb(uint64_t state[25], size_t offset, unsigned value) {
  state[offset / 8] ^= (uint64_t)(value & 0xffu) << (8 * (offset % 8));
}

void lang_keccak256(const unsigned char *data, size_t size, unsigned char digest[32]) {
  uint64_t state[25] = {0};
  size_t full = size - size % KECCAK_RATE;
  for (size_t block = 0; block < full; block += KECCAK_RATE) {
    for (size_t i = 0; i < KECCAK_RATE; i++)
      absorb(state, i, data[block + i]);
    permute(state);
  }
  for (size_t i = full; i < size; i++)
    absorb(state, i - full, data[i]);
  absorb(state, size - full, 0x01u);
  absorb(state, KECCAK_RATE - 1, 0x80u);
  permute(state);
  for (size_t i = 0; i < 32; i++)
    digest[i] = (unsigned char)(state[i / 8] >> (8 * (i % 8)));
}
