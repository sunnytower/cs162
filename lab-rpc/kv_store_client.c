/**
 * Client binary.
 */

#include "kv_store_client.h"

#define HOST "localhost"

CLIENT* clnt_connect(char* host) {
  CLIENT* clnt = clnt_create(host, KVSTORE, KVSTORE_V1, "udp");
  if (clnt == NULL) {
    clnt_pcreateerror(host);
    exit(1);
  }
  return clnt;
}

int example(int input) {
  CLIENT* clnt = clnt_connect(HOST);

  int ret;
  int* result;

  result = example_1(&input, clnt);
  if (result == (int*)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = *result;
  xdr_free((xdrproc_t)xdr_int, (char*)result);

  clnt_destroy(clnt);

  return ret;
}

char* echo(char* input) {
  CLIENT* clnt = clnt_connect(HOST);

  char* ret;
  char** result;
  result = echo_1(&input, clnt);
  if (result == (char**)NULL) {
    clnt_perror(clnt, "call failed");
    exit(1);
  }
  ret = strdup(*result);
  xdr_free((xdrproc_t)xdr_int, (char*)result);

  clnt_destroy(clnt);

  return ret;
}

void put(buf key, buf value) {
  CLIENT* clnt = clnt_connect(HOST);

  key_value input = {key, value};
  put_1(&input, clnt);

  clnt_destroy(clnt);
}

buf* get(buf key) {
  CLIENT* clnt = clnt_connect(HOST);

  buf* ret;

  ret = get_1(&key, clnt);
  clnt_destroy(clnt);

  return ret;
}
