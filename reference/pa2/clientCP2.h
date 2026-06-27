#ifndef CLIENTCP2_H
#define CLIENTCP2_H

#include "libs/common.h"

int auth(int sockfd, EVP_PKEY **server_pub_key_req);
unsigned char *encrypt_file(unsigned char *original, EVP_PKEY *server_pub_key,
			    size_t plain_len, size_t *enc_len_out);

#endif
