#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/asn1t.h>

#define BUF_LEN 256
#define OPENSSL_API_COMPAT 30100

int set_socket(char *host, char *service); 
int ssl(char *host, char *endpt);
char* get_req(char *host, char *endpt); 
void print_err_desc(int err); 

int main(int argc, char **argv) {
	//Set URL
	char *host = "api.fiscaldata.treasury.gov\0";
	char *endpt = "/services/api/fiscal_service/v1/accounting/od/schedules_fed_debt_daily_activity?filter=record_date:eq:2022-05-01\0"; 
	int err = ssl(host, endpt);
	if(err < 0) {
		fprintf(stderr, "ssl failed\n");
		return -1; 
	}
	return 0;
}

int set_socket(char *host, char *service) {
	int err = 0;
	int fd = -1;
	//Configure everything for connection to server
	struct addrinfo hints, *res; 
	//Init everything to zero
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = PF_UNSPEC; 
	hints.ai_socktype = SOCK_STREAM;
	//Get list of IP addresses and port numbers for host name
	err = getaddrinfo(host, service, &hints, &res);
	if(err != 0) {
		const char *msg = gai_strerror(err); 
		fprintf(stderr, "getaddrinfo: %s\n", msg);
		return -1; 
	}
	while(res != NULL) {
		//fyi HTTP uses stream sockets which relies on TCP
		fd = socket(res->ai_family, 
			    res->ai_socktype, 
			    res->ai_protocol);
		if(fd == -1) {
			perror("socket: \n");
			res = res->ai_next;
			continue;
		}
		err = connect(fd, res->ai_addr, res->ai_addrlen); 
		if(err == -1) {
			perror("connect: \n");
			close(fd);
			res = res->ai_next;
			continue;
		}
		//We have a successful web socket
		break;
	}
	freeaddrinfo(res);
	return fd; 
}

//Enables secure connections between client and server
int ssl(char *host, char *endpt) {
	if(SSL_library_init() < 0) {
		fprintf(stderr, "Failed to init ossl library\n");
		return -1;
	}
	const SSL_METHOD *meth = TLS_client_method();
	if(meth == NULL) {
		fprintf(stderr, "Failed to negotiate ssl version\n");
		return -1; 
	}
	//ctx contains cryptographic algorithms for secure connection 
	SSL_CTX *ctx = SSL_CTX_new(meth);
	if(ctx == NULL) {
		fprintf(stderr, "SSL_CTX object creation failed\n");
		return -1; 
	}
	//Create structure to hold data necessary for TLS/SSL comm
	SSL *ssl = SSL_new(ctx);
	if(ssl == NULL) {
		fprintf(stderr, "Failed to create ssl struct\n");
		return -1;
	}
	int sock = set_socket(host, "https\0"); 
	if(sock <= 0) {
		fprintf(stderr, "Failed to create sock\n");
		return -1;
	}
	int err = SSL_set_fd(ssl, sock);
	if(err == 0) {
		fprintf(stderr, "Failed to set sock to ssl struct\n");
		return -1; 
	}
	err = SSL_connect(ssl); 
	if(err <= 0) {
		err = SSL_get_error(ssl, err); 
		fprintf(stderr, "Error: SSL_connect\n");
		print_err_desc(err);
		ERR_print_errors_fp(stderr);
		return -1;
	}
	printf("SSL connection using %s\n", SSL_get_cipher(ssl));
	//Get remote certificate into X509 struct
	X509 *cert = SSL_get_peer_certificate(ssl);
	if(cert == NULL)
		fprintf(stderr, "Err no certificate from %s\n", host); 
	X509_NAME *certname = X509_get_subject_name(cert);
	if(certname == 0) 
		fprintf(stderr, "Failed to get cert name\n");
	else {
		X509_NAME_print_ex_fp(stdout, certname, 0, 0);
		printf("\n");
	}
	//Send GET request for data from API endpt
	char *get = get_req(host, endpt); 
	err = SSL_write(ssl, get, strlen(get));
	if(err <= 0) {
		err = SSL_get_error(ssl, err); 
		fprintf(stderr, "SSL_write: %i\n", err);
		print_err_desc(err);
		return -1; 
	}
	free(get);
	//Receive data from API endpt
	char buffer[BUF_LEN];
	while(SSL_read(ssl, buffer, BUF_LEN-1) > 0)
		printf("%s", buffer);
	print_err_desc(SSL_get_error(ssl ,err)); 
	SSL_free(ssl);
	close(sock);
	X509_free(cert);
	SSL_CTX_free(ctx);
	return 1; 
}

//Connection header is sometimes necessary to otherwise recv will hang
//Alternative solution is to parse chunked bytes header ret from server
char* get_req(char *host, char *endpt) {
	char *buffer = malloc(sizeof(char) * BUF_LEN);
	//CLRF: Moves cursor to beginning of next line
	int err = snprintf(buffer, BUF_LEN, 
		 "GET %s HTTP/1.1 \r\nHost: %s \r\nConnection: close\r\n\r\n",
		 endpt, host);
	if(err < 0) {
		fprintf(stderr, "GET snprintf\n");
		return NULL; 
	}
	return buffer;
}

//Switch statement cause error's can concurrently occur
//TODO: Make error descriptions more verbose
void print_err_desc(int err) {
	switch (err) {
		case SSL_ERROR_NONE: 
			break;
		case SSL_ERROR_ZERO_RETURN:
			fprintf(stderr, 
				"Peer has closed conn for writing\n");
		case SSL_ERROR_WANT_READ: 
			fprintf(stderr, "ssl_err_read\n");
		case SSL_ERROR_WANT_WRITE:
			fprintf(stderr, "ssl_err_write\n");
		case SSL_ERROR_WANT_CONNECT: 
			fprintf(stderr, "ssl_want_connect\n");
		case SSL_ERROR_WANT_ACCEPT:
			fprintf(stderr, "ssl_want_accept\n"); 
		case SSL_ERROR_WANT_X509_LOOKUP:
			fprintf(stderr, "x509 lookup\n");
		case SSL_ERROR_WANT_ASYNC:
			fprintf(stderr, "async issue\n");
		case SSL_ERROR_WANT_ASYNC_JOB:
			fprintf(stderr, "async\n");
		case SSL_ERROR_WANT_CLIENT_HELLO_CB:
			fprintf(stderr, "callback\n");
		case SSL_ERROR_SYSCALL: 
			fprintf(stderr, "syscall err\n");
		case SSL_ERROR_SSL: 
			fprintf(stderr, "err ssl\n"); 
	}
}
