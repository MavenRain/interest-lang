/* Run native moves in geth and preserve the ERC-721 owner address bound. */
#include "asm.h"
#include "keccak.h"
#include "../domain/data.h"
#include <stdlib.h>
#include <string.h>

static const char sender[] = "0000000000000000000000000000000000001000";
static const char receiver[] = "0000000000000000000000007265636569766572";
static char runtime[2 * EVM_CAPACITY + 1];

static void hex(const unsigned char *bytes, size_t count, char *text) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < count; i++) {
    text[2 * i] = digits[bytes[i] >> 4];
    text[2 * i + 1] = digits[bytes[i] & 15];
  }
  text[2 * count] = '\0';
}

static void word(unsigned long long n, unsigned char bytes[32]) {
  memset(bytes, 0, 32);
  for (size_t i = 0; i < 8; i++) {
    bytes[31 - i] = (unsigned char)n;
    n >>= 8;
  }
}

static void slot(unsigned base, unsigned long long key, unsigned char out[32]) {
  unsigned char input[64];
  word(key, input);
  word(base, input + 32);
  lang_keccak256(input, sizeof input, out);
}

static void storage(FILE *out, const unsigned char key[32], unsigned value) {
  char text[65];
  hex(key, 32, text);
  fprintf(out, "\"0x%s\":\"0x%064x\",", text, value);
}

static int make_runtime(LangFacade facade) {
  LangDomainData data = {0};
  data.start = 1;
  data.charters = 3;
  data.facade = facade;
  data.holders = 1;
  data.holder[0] = (LangHolder){4096, 1, 1, 0, 1};
  data.issuers = 1;
  data.issuer[0] = (LangIssuer){1, 1};
  for (unsigned p = 0; p < LANG_PARTITIONS; p++)
    for (unsigned c = 0; c < 3; c++)
      for (unsigned f = 0; f < LANG_PROFILES; f++)
        for (unsigned t = 0; t < LANG_PROFILES; t++)
          data.cap[p][c][f][t].tag = LANG_CAP_ANY;
  unsigned char codes[3] = {1, 1, 1};
  LangContract contract = {1, LANG_REGIME_DEBREU, codes, 3, 3, &data};
  FILE *out = tmpfile();
  if (out == NULL)
    return 1;
  int failed = lang_evm_write(&contract, LANG_PART_RUNTIME, out, stderr);
  rewind(out);
  size_t count = fread(runtime, 1, sizeof runtime - 1, out);
  failed = failed || ferror(out) || !feof(out) || count == 0;
  fclose(out);
  while (count > 0 && runtime[count - 1] == '\n')
    count--;
  runtime[count] = '\0';
  return failed;
}

static int prestate(void) {
  FILE *out = fopen(".gatework/nft-boundaries/prestate.json", "w");
  if (out == NULL)
    return 1;
  fprintf(out, "{\"config\":{\"chainId\":1,\"homesteadBlock\":0,\"eip150Block\":0,"
          "\"eip155Block\":0,\"eip158Block\":0,\"byzantiumBlock\":0,\"constantinopleBlock\":0,"
          "\"petersburgBlock\":0,\"istanbulBlock\":0,\"berlinBlock\":0,\"londonBlock\":0,"
          "\"mergeNetsplitBlock\":0,\"terminalTotalDifficulty\":0,\"shanghaiTime\":0,\"cancunTime\":0},"
          "\"coinbase\":\"0x0000000000000000000000000000000000000000\",\"difficulty\":\"0x0\","
          "\"gasLimit\":\"0x1000000\",\"nonce\":\"0x0000000000000000\",\"timestamp\":\"0x0\","
          "\"number\":\"0x0\",\"excessBlobGas\":\"0x0\",\"blobGasUsed\":\"0x0\","
          "\"alloc\":{\"%s\":{\"balance\":\"0x100000\"},"
          "\"%s\":{\"balance\":\"0x0\",\"code\":\"0x%s\",\"storage\":{", sender, receiver, runtime);
  unsigned char key[32], input[64];
  slot(1, 4096, key);
  storage(out, key, 2);
  slot(0, 1, key);
  storage(out, key, 1);
  slot(13, 1, input + 32);
  word(1, input);
  lang_keccak256(input, sizeof input, key);
  storage(out, key, 1);
  word(3, key);
  storage(out, key, 1);
  word(14, key);
  storage(out, key, 1);
  fputs("\"0x000000000000000000000000000000000000000000000000000000000000000f\":"
        "\"0x0000000000000000000000000000000000000000000000000000000000000002\"}}}}", out);
  int failed = ferror(out);
  return fclose(out) != 0 || failed;
}

static int check(const char *label, const char *signature, const unsigned char to[32],
                 unsigned quantity, int recover, int rejected) {
  unsigned char digest[32], value[32];
  char selector[9], destination[65], amount[65], input[201], command[768];
  lang_keccak256((const unsigned char *)signature, strlen(signature), digest);
  hex(digest, 4, selector);
  hex(to, 32, destination);
  word(quantity, value);
  hex(value, 32, amount);
  snprintf(input, sizeof input, "%s%s%s%s", selector,
           recover ? "0000000000000000000000000000000000000000000000000000000000000001" : "",
           destination, amount);
  snprintf(command, sizeof command,
           "evm --verbosity 0 run --prestate .gatework/nft-boundaries/prestate.json "
           "--gas 16777216 --sender 0x%s --receiver 0x%s --input %s --json "
           "> .gatework/nft-boundaries/result.jsonl 2> .gatework/nft-boundaries/result.err",
           sender, receiver, input);
  if (system(command) != 0) {
    fprintf(stderr, "nft-boundaries: %s: geth failed\n", label);
    return 1;
  }
  FILE *out = fopen(".gatework/nft-boundaries/result.jsonl", "r");
  if (out == NULL)
    return 1;
  char line[4096];
  int reverted = 0, finished = 0, fault = 0;
  while (fgets(line, sizeof line, out) != NULL) {
    int error = strstr(line, "\"error\":") != NULL;
    int revert = strstr(line, "execution reverted") != NULL;
    reverted = reverted || revert;
    fault = fault || (error && !revert);
    finished = finished || strstr(line, "\"gasUsed\":") != NULL;
  }
  int failed = ferror(out) || fault || !finished || reverted != rejected;
  fclose(out);
  if (failed)
    fprintf(stderr, "nft-boundaries: %s: expected %s, reverted=%d finished=%d fault=%d\n",
            label, rejected ? "revert" : "success", reverted, finished, fault);
  return failed;
}

int main(void) {
  if (system("mkdir -p .gatework/nft-boundaries") != 0)
    return 1;
  unsigned char wide[32] = {0}, last[32] = {0}, full[32];
  wide[11] = 1; /* 2^160, the first identity outside an ABI address. */
  memset(last + 12, 0xff, 20);
  memset(full, 0xff, 32);
  full[31] = 0xfe; /* The last identity encodable as identity + 1. */
  int failed = 0;
  for (unsigned facade = 0; facade < 2; facade++) {
    if (make_runtime(facade ? LANG_FACADE_ERC721 : LANG_FACADE_ERC20) || prestate())
      return 1;
    failed += check("transfer-wide", "transfer(uint256,uint256)", wide, 1, 0, facade != 0);
    failed += check("recover-wide", "recover(uint256,uint256,uint256)", wide, 1, 1, facade != 0);
    failed += check("transfer-last-identity", "transfer(uint256,uint256)", full, 1, 0, facade != 0);
    failed += check("recover-last-identity", "recover(uint256,uint256,uint256)", full, 1, 1, facade != 0);
    failed += check("transfer-last-address", "transfer(uint256,uint256)", last, 1, 0, 0);
    failed += check("recover-last-address", "recover(uint256,uint256,uint256)", last, 1, 1, 0);
    failed += check("transfer-zero-wide", "transfer(uint256,uint256)", wide, 0, 0, 0);
    failed += check("recover-zero-wide", "recover(uint256,uint256,uint256)", wide, 0, 1, 0);
  }
  if (failed == 0)
    puts("nft-boundaries: 16 checks passed");
  return failed ? 1 : 0;
}
