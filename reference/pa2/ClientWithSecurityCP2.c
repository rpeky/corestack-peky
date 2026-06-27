/**
 * ClientWithSecurityAP.c
 * -----------------------
 * Plain FTP client — no encryption, no authentication.
 * Sends files to the server using the length-prefixed wire protocol.
 *
 * Usage: ./ClientWithoutSecurity [PORT] [ADDRESS]
 *
 * Wire protocol (all lengths are 8-byte big-endian):
 *   [MSG_FILENAME=0][filename_len][filename_bytes]
 */
#include "clientCP2.h"
// #include "libs/common.h"

// make this guy global so its easier to set
int use_oaep = 1;

int auth(int sockfd, EVP_PKEY **server_pub_key_req) {
	// TASK 1: MODE3 - MSG_AUTH (client side)
	// 1. Send int 3 via send_int
	// 2. M1: size of M2 in bytes
	// 3. M2: Authentication message

	// Generate secret - generate_session_key(unsigned char
	// key_out[SESSION_KEY_LEN]) key_out[SESSION_KEY_LEN] -> *key_out
	// SESSION_KEY_LEN defined in common.h
	// create a buffer for the session key
	unsigned char session_key[SESSION_KEY_LEN];

	// check if session key is generated
	if (generate_session_key(session_key) != 0)
		return -1;

	// Send MODE 3, size of M2, then M2 itself
	if (send_int(sockfd, MSG_AUTH) != 0)
		return -1;
	if (send_int(sockfd, SESSION_KEY_LEN) != 0)
		return -1;
	if (send_all(sockfd, session_key, SESSION_KEY_LEN) != 0)
		return -1;

	// Expect 4 messages from server
	// A1. M1: size of incoming M2 bytes
	// A2. signed auth message
	// B1. M1: size of incoming M4 bytes (server_signed.crt)
	// B2. M2: server_signed.crt

	// A1 get the size of auth message
	unsigned char *sig_len_buf = read_bytes(sockfd, INT_BYTES);
	if (!sig_len_buf)
		return -1;
	uint64_t sig_len = bytes_to_int(sig_len_buf);
	free(sig_len_buf);

	// A2 get the signed message
	unsigned char *sig = read_bytes(sockfd, sig_len);
	if (!sig)
		return -1;

	// B1 get the size of server_signed.crt
	unsigned char *crt_len_buf = read_bytes(sockfd, INT_BYTES);
	if (!crt_len_buf)
		return -1;

	uint64_t crt_len = bytes_to_int(crt_len_buf);
	free(crt_len_buf);

	// B2 get the cert
	unsigned char *crt_bytes = read_bytes(sockfd, crt_len);
	if (!crt_bytes)
		return -1;

	// Verify Server ID
	X509 *server_cert = load_cert_bytes(crt_bytes, (int)crt_len);
	free(crt_bytes);
	if (!server_cert)
		return -1;

	// Verify the cert with pem file
	if (!verify_server_cert(server_cert, "auth/cacsertificate.crt"))
		return -1;

	// Verify signed message
	if (!verify_message_pss(server_cert, sig, (size_t)sig_len, session_key,
				SESSION_KEY_LEN))
		return -1;

	// Get the public key
	EVP_PKEY *server_pub_key = X509_get_pubkey(server_cert);
	if (!server_pub_key)
		return -1;

	// pass the key to the pointer
	*server_pub_key_req = server_pub_key;

	free(sig);
	X509_free(server_cert);
	return 0;
}

unsigned char *encrypt_file(unsigned char *original, EVP_PKEY *server_pub_key,
			    size_t plain_len, size_t *enc_len_out) {
	/**
	 * use_oaep=1: OAEP with SHA-256 (max 62 bytes for 1024-bit key)
	 * use_oaep=0: PKCS1v15 (max 117 bytes for 1024-bit key)
	 */
	// set chunk size based on use_oaep value
	size_t chunk_size = use_oaep ? RSA_OAEP_CHUNK : RSA_PKCS1_CHUNK;

	// calculate no of blocks based on chunk size
	// add a little more chunksize-1 to round up the blocks needed
	size_t blocks = (plain_len + chunk_size - 1) / chunk_size;
	// calculate the new encoded length -> already in bytes
	size_t enc_total_len = blocks * RSA_KEY_BYTES;

	// malloc the entire enc data
	unsigned char *enc_data = malloc(enc_total_len);
	// check malloc fail
	if (!enc_data)
		return NULL;

	size_t plain_offset = 0;
	size_t enc_offset = 0;

	// start encoding the blocks
	while (plain_offset < plain_len) {
		size_t remainder = plain_len - plain_offset;
		// check to give whole chunk or just remainder
		size_t current_chunk =
			(remainder < chunk_size) ? remainder : chunk_size;

		size_t enc_len = 0;
		// adjust encoded block by offset to the length
		unsigned char *enc_block = rsa_encrypt_block(
			server_pub_key, original + plain_offset, current_chunk,
			&enc_len, use_oaep);

		// check malloc fail
		if (!enc_block) {
			free(enc_data);
			free(enc_block);
			return NULL;
		}

		// copy based on the offset of the starting point
		memcpy(enc_data + enc_offset, enc_block, enc_len);

		free(enc_block);

		// shfit offset values
		plain_offset += current_chunk;
		enc_offset += enc_len;
	}

	// set how long the offset is
	*enc_len_out = enc_offset;
	// return the encoded data
	return enc_data;
}

int main(int argc, char *argv[]) {
	int port = (argc > 1) ? atoi(argv[1]) : 4321;
	const char *server_address = (argc > 2) ? argv[2] : "localhost";

	double start_time = get_time();

	// Try to connect to server
	printf("Establishing connection to server...\n");

	/* Create TCP socket and connect to server */
	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		perror("socket");
		return 1;
	}

	// Reference struct for sockaddr_in

	/* sin_family	-> AF_INET */
	/* sin_port	-> Port number */
	/* sin_addr	-> IPv4 address */

	/*
	struct sockaddr_in {
		sa_family_t sin_family;
		in_port_t sin_port;
		struct in_addr sin_addr;
	};
	*/

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	// define the socket protocol family -> AF_INET (man 2 socket) IPV4
	serv_addr.sin_family = AF_INET;
	// convert unsigned short int from host byte order to network byte order
	serv_addr.sin_port = htons(port);

	struct hostent *he = gethostbyname(server_address);
	if (!he) {
		fprintf(stderr, "Cannot resolve host: %s\n", server_address);
		return 1;
	}
	memcpy(&serv_addr.sin_addr, he->h_addr_list[0], he->h_length);

	/* sa_family_t	-> Address family */
	/* sa_data	-> Socket address */

	/*
	struct sockaddr {
		sa_family_t sa_family;
		char sa_data[];
	};
	*/

	if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) <
	    0) {
		printf("Socket connection failed\n");
		perror("connect");
		return 1;
	}
	printf("Connected\n");

	// TASK 1: MODE3 - MSG_AUTH (client side)
	EVP_PKEY *server_pub_key = NULL;
	if (auth(sockfd, &server_pub_key) != 0) {
		fprintf(stderr, "Authentication failed\n");
		close(sockfd);
		return 1;
	}

	// Task 3
	// Client already authenticated and verified cert in AP has server pub
	// key Means only the correct server can decrypt the file with the sess
	// key

	// Create session key for CP2
	unsigned char session_key[SESSION_KEY_LEN];
	if (generate_session_key(session_key) != 0)
		return 1;

	// Encrypt this session key using server pubkey
	size_t enc_key_len = 0;
	unsigned char *enc_key =
		rsa_encrypt_block(server_pub_key, session_key, SESSION_KEY_LEN,
				  &enc_key_len, use_oaep);

	// Send encrypted session key to server
	send_int(sockfd, MSG_SYMKEY);
	send_int(sockfd, (uint64_t)enc_key_len);
	send_all(sockfd, enc_key, (uint64_t)enc_key_len);

	free(enc_key);

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

	/* Interactive file sending loop */
	while (1) {
		char filename[4096];
		printf("Enter a filename to send (enter -1 to exit):");
		if (!fgets(filename, sizeof(filename), stdin))
			break;

		/* Strip trailing newline */
		filename[strcspn(filename, "\n")] = '\0';

		/* Validate filename */
		while (strcmp(filename, "-1") != 0) {
			struct stat st;
			if (stat(filename, &st) == 0 && S_ISREG(st.st_mode))
				break;
			printf("Invalid filename. Please try again:");
			if (!fgets(filename, sizeof(filename), stdin))
				goto done;
			filename[strcspn(filename, "\n")] = '\0';
		}

		if (strcmp(filename, "-1") == 0) {
			send_int(sockfd, MSG_CLOSE);
			break;
		}

		/* Send the filename: [0][len][bytes] */
		size_t fn_len = strlen(filename);
		send_int(sockfd, MSG_FILENAME);
		send_int(sockfd, fn_len);
		send_all(sockfd, (unsigned char *)filename, fn_len);

		/* Read the entire file into memory */
		FILE *fp = fopen(filename, "rb");
		if (!fp) {
			perror("fopen");
			continue;
		}
		fseek(fp, 0, SEEK_END);
		long file_size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		unsigned char *file_data = malloc(file_size);
		fread(file_data, 1, file_size, fp);
		fclose(fp);

		// Task 2: Encrypt the data to send
		// Task 3: Encrypt by session
		size_t enc_len = 0;
		unsigned char *enc_data = session_encrypt(
			session_key, file_data, (size_t)file_size, &enc_len);
		/*
		unsigned char *enc_data = encrypt_file(
			file_data, server_pub_key, (size_t)file_size, &enc_len);
		*/

		if (!enc_data) {
			fprintf(stderr, "encrypt file failed\n");
			free(file_data);
			continue;
		}

		// Modify for enc data
		/* Send the file data: [1][len][bytes] */
		send_int(sockfd, MSG_FILE_DATA);
		send_int(sockfd, (uint64_t)enc_len);
		send_all(sockfd, enc_data, (uint64_t)enc_len);
		free(file_data);
		free(enc_data);
	}

done:
	/* Send close message */
	send_int(sockfd, MSG_CLOSE);
	EVP_PKEY_free(server_pub_key);
	printf("Closing connection...\n");
	close(sockfd);

	double end_time = get_time();
	printf("Program took %.3fs to run.\n", end_time - start_time);
	// zero session key from memory
	OPENSSL_cleanse(session_key, SESSION_KEY_LEN);
	return 0;
}
