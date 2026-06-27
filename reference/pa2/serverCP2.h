#ifndef SERVERCP2_H
#define SERVERCP2_H

#include "libs/common.h"

void sigint_handler(int sig);
unsigned char *decrypt_file(unsigned char *encrypted_data,
			    EVP_PKEY *server_priv_key, size_t enc_len,
			    size_t *plain_len_out);

#endif
