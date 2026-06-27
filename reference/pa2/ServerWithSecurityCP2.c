/**
 * ServerWithSecurityAP.c
 * -----------------------
 * Plain FTP server — no encryption, no authentication.
 * Receives files from the client using the length-prefixed wire protocol.
 *
 * Usage: ./ServerWithoutSecurity [PORT] [ADDRESS]
 *
 * Received files are saved to recv_files/ with a "recv_" prefix.
 */
#include "serverCP2.h"
// #include "libs/common.h"

/* Global socket for SIGINT cleanup */
static int server_fd = -1;

// Global to make setting oaep easier
int use_oaep = 1;

void sigint_handler(int sig) {
	(void)sig;
	printf("\nSIGINT or CTRL-C detected. Exiting gracefully\n");
	if (server_fd >= 0)
		close(server_fd);
	exit(0);
}

unsigned char *decrypt_file(unsigned char *encrypted_data,
			    EVP_PKEY *server_priv_key, size_t enc_len,
			    size_t *plain_len_out) {

	// malloc space for decrypted data
	// decrypted data is always at least smaller than enc cause of padding
	unsigned char *dec_data = malloc(enc_len);
	if (!dec_data)
		return NULL;

	size_t dec_offset = 0;

	printf("Encrypted data content: %s\n", encrypted_data);
	printf("----------------------------------\n");

	// each block is uniform to 182 bytes, decrypt by block
	for (size_t enc_off = 0; enc_off < enc_len;
	     enc_off += (size_t)RSA_KEY_BYTES) {
		size_t dec_len = 0;
		unsigned char *dec_block = rsa_decrypt_block(
			server_priv_key, (encrypted_data + enc_off),
			(size_t)RSA_KEY_BYTES, &dec_len, use_oaep);

		// check malloc fail
		if (!dec_block) {
			free(dec_data);
			return NULL;
		}

		printf("Decrypted block %zu - %zu content: \n%s\n", enc_off,
		       (size_t)RSA_KEY_BYTES, dec_block);
		printf("----------------------------------\n");

		// copy based on offset
		memcpy(dec_data + dec_offset, dec_block, dec_len);
		free(dec_block);

		dec_offset += dec_len;
	}

	*plain_len_out = dec_offset;
	return dec_data;
}

int main(int argc, char *argv[]) {
	signal(SIGINT, sigint_handler);
	unsigned char session_key[SESSION_KEY_LEN];
	int session_key_flag = 0;

	int port = (argc > 1) ? atoi(argv[1]) : 4321;
	const char *address = (argc > 2) ? argv[2] : "localhost";

	/* Create listening socket */
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd < 0) {
		perror("socket");
		return 1;
	}

	int opt = 1;
	setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(port);

	if (strcmp(address, "localhost") == 0)
		serv_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	else if (strcmp(address, "0.0.0.0") == 0)
		serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	else
		inet_pton(AF_INET, address, &serv_addr.sin_addr);

	// forcefully attach server fd to port
	if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <
	    0) {
		perror("bind");
		return 1;
	}
	listen(server_fd, 1);
	printf("Server listening on %s:%d\n", address, port);

	/* Accept one client */
	int client_fd = accept(server_fd, NULL, NULL);
	// check if fail otherwise client_fd is a non neg int
	if (client_fd < 0) {
		perror("accept");
		return 1;
	}
	printf("Client connected.\n");

	char filename[4096] = {0};

	while (1) {
		/* Read 8-byte message type */
		unsigned char *type_buf = read_bytes(client_fd, INT_BYTES);
		if (!type_buf)
			break;
		uint64_t msg_type = bytes_to_int(type_buf);
		free(type_buf);

		/* ======================================================================
		 * Protocol message types
		 * ======================================================================
		 */
		/*
		MSG_FILENAME 0
		MSG_FILE_DATA 1
		MSG_CLOSE 2
		MSG_AUTH 3
		MSG_SYMKEY 4
		*/

		switch (msg_type) {
		case MSG_FILENAME: {
			/* If the packet is for transferring the filename */
			printf("Receiving file...\n");
			unsigned char *len_buf =
				read_bytes(client_fd, INT_BYTES);
			uint64_t fn_len = bytes_to_int(len_buf);
			free(len_buf);

			unsigned char *fn_buf = read_bytes(client_fd, fn_len);
			memset(filename, 0, sizeof(filename));
			memcpy(filename, fn_buf, fn_len);
			free(fn_buf);
			break;
		}
		case MSG_FILE_DATA: {
			/* If the packet is for transferring a chunk of the file
			 */
			double start_time = get_time();

			unsigned char *len_buf =
				read_bytes(client_fd, INT_BYTES);
			uint64_t enc_len = bytes_to_int(len_buf);
			free(len_buf);

			unsigned char *enc_data =
				read_bytes(client_fd, enc_len);

			// Task 2 decrypt the encrypted data
			// load private key
			EVP_PKEY *priv =
				load_private_key("auth/private_key.pem");
			if (!priv) {
				free(enc_data);
				goto done;
			}

			// TASK 2
			// TASK 3
			size_t file_len = 0;
			unsigned char *file_data;
			if (session_key_flag) {
				printf("Encrypted data content: %s\n",
				       enc_data);
				printf("----------------------------------\n");
				file_data = session_decrypt(
					session_key, enc_data, (size_t)enc_len,
					&file_len);
			} else {
				file_data = decrypt_file(enc_data, priv,
							 enc_len, &file_len);
			}

			EVP_PKEY_free(priv);
			free(enc_data);

			// sancheck file_data
			if (!file_data) {
				goto done;
			}

			/* Extract basename and prepend "recv_" */
			const char *base = strrchr(filename, '/');
			base = base ? base + 1 : filename;

			char outpath[4096];
			snprintf(outpath, sizeof(outpath), "recv_files/recv_%s",
				 base);

			/* Write the file with 'recv_' prefix */
			FILE *fp = fopen(outpath, "wb");
			if (fp) {
				fwrite(file_data, 1, file_len, fp);
				fclose(fp);
			}
			free(file_data);

			printf("Finished receiving file in %.3fs!\n",
			       get_time() - start_time);
			break;
		}
		case MSG_CLOSE:
			/* Close the connection */
			printf("Closing connection...\n");
			goto done;
		case MSG_AUTH: {
			// TASK 1: Receive MODE3 - MSG_AUTH
			// Send 4 Messages
			// A1. M1: size of outgoing M2 bytes
			// A2. signed auth message
			// B1. M1: size of outgoing M4 bytes (server_signed.crt)
			// B2. M2: server_signed.crt

			// Receive the challenge from client
			unsigned char *len_buf =
				read_bytes(client_fd, INT_BYTES);
			uint64_t challenge_len = bytes_to_int(len_buf);
			free(len_buf);

			unsigned char *challenge =
				read_bytes(client_fd, challenge_len);
			if (!challenge)
				goto done;

			// Load private key
			EVP_PKEY *priv =
				load_private_key("auth/private_key.pem");
			// fail if does not load
			if (!priv)
				goto done;
			size_t sig_len = 0;

			// Sign message with private key
			unsigned char *sig = sign_message_pss(
				priv, challenge, (size_t)challenge_len,
				&sig_len);
			// fail if sig does not load
			if (!sig)
				goto done;

			// A1 send size of signed auth message
			send_int(client_fd, (uint64_t)sig_len);
			// A2 send signed auth message
			send_all(client_fd, sig, (uint64_t)sig_len);

			// Load server_signed.crt
			const char *cert_path = "auth/server_signed.crt";

			// man 3 fopen
			FILE *cert_fp = fopen(cert_path, "r");
			if (!cert_fp) {
				perror("fopen server cert");
				free(sig);
				free(challenge);
				EVP_PKEY_free(priv);
				goto done;
			}

			// man 3 fseek
			// go to end of cert, get the position, rewind
			fseek(cert_fp, 0L, SEEK_END);
			long cert_size = ftell(cert_fp);
			fseek(cert_fp, 0L, SEEK_SET);

			// malloc the size, check if malloc fails and fail
			// gracefully
			unsigned char *cert_bytes = malloc((size_t)cert_size);
			if (!cert_bytes) {
				perror("malloc cert bytes");
				fclose(cert_fp);
				free(sig);
				free(challenge);
				EVP_PKEY_free(priv);
				goto done;
			}

			// store at cert_bytes, read 1 byte long,
			// read size_t cert_size times, at the cert file loc
			fread(cert_bytes, 1, (size_t)cert_size, cert_fp);
			fclose(cert_fp);

			// B1 Send cert size
			send_int(client_fd, (uint64_t)cert_size);
			// B2 Send signed cert
			send_all(client_fd, cert_bytes, (uint64_t)cert_size);

			free(sig);
			free(challenge);
			free(cert_bytes);
			EVP_PKEY_free(priv);
			break;
		}
		case MSG_SYMKEY: {
			unsigned char *len_buf =
				read_bytes(client_fd, INT_BYTES);
			uint64_t enc_key_len = bytes_to_int(len_buf);
			free(len_buf);

			// get the encrypted session key
			unsigned char *enc_key =
				read_bytes(client_fd, enc_key_len);
			if (!enc_key)
				goto done;

			// load private key
			EVP_PKEY *priv =
				load_private_key("auth/private_key.pem");
			if (!priv)
				goto done;

			// decrypt the session key
			size_t dec_key_len = 0;
			unsigned char *dec_key = rsa_decrypt_block(
				priv, enc_key, (size_t)enc_key_len,
				&dec_key_len, use_oaep);

			// sancheck if exist or corrupted / tampered
			if (!dec_key || dec_key_len != SESSION_KEY_LEN) {
				fprintf(stderr, "Session decrypt key failed\n");
				goto done;
			}

			memcpy(session_key, dec_key, SESSION_KEY_LEN);
			session_key_flag = 1;

			free(dec_key);
			free(enc_key);
			EVP_PKEY_free(priv);
			break;
		}
		default:
			fprintf(stderr, "Unknown message type: %lu\n",
				(unsigned long)msg_type);
			goto done;
		}
	}

done:
	close(client_fd);
	close(server_fd);
	// zero session key from memory
	OPENSSL_cleanse(session_key, SESSION_KEY_LEN);
	return 0;
}
