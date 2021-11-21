#include <netinet/in.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "devi.c"
#include "css.c"

#ifndef devi_address
#define devi_address "backend.deviantart.com"
#endif

#ifndef devi_port
#define devi_port 8017
#endif

static int devi_read(fd, string, length)
	int fd;
	char *string;
	size_t length;
{
	while (length != 0)
	{
		ssize_t done = read(fd, string, length);
		if (done < 0) return 1;
		string += done;
		length -= done;
	}
	return 0;
}

static int devi_tls_read(ssl, string, length)
	SSL *ssl;
	char *string;
	int length;
{
	while (length != 0)
	{
		int done = SSL_read(ssl, string, length);
		if (done <= 0) return 1;
		string += done;
		length -= done;
	}
	return 0;
}

static int devi_tls_write(ssl, string, length)
	SSL *ssl;
	char *string;
	int length;
{
	while (length != 0)
	{
		int written = SSL_write(ssl, string, length);
		if (written <= 0) return 1;
		string += written;
		length -= written;
	}
	return 0;
}

enum devi_method
{
	devi_invalid,
	devi_get,
	devi_head,
};

static enum devi_method devi_method(fd)
	int fd;
{
	char ch;
	if (devi_read(fd, &ch, 1)) return devi_invalid;
	if (ch == 'G')
	{
		char chs[4];
		if (devi_read(fd, chs, 3)) return devi_invalid;
		chs[3] = '\0';
		if (devi_compare_str("ET ", chs, 3)) return devi_invalid;
		return devi_get;
	}
	if (ch == 'H')
	{
		char chs[5];
		if (devi_read(fd, chs, 4)) return devi_invalid;
		chs[4] = '\0';
		if (devi_compare_str("EAD ", chs, 4)) return devi_invalid;
		return devi_head;
	}
	return devi_invalid;
}

static int devi_resource(fd, string, length, stop)
	int fd;
	char *string;
	int length;
	int *stop;
{
	*string = '\0';
	if (*stop) return 0;
	
	int i;
	for (i = 0 ; i < length - 1 ; i++)
	{
		char ch;
		if (devi_read(fd, &ch, 1)) return 1;
		if (ch == ' ') { *stop = 1; break; }
		if (ch == '?') break;
		string[i] = ch;
	}
	string[i] = '\0';
	return 0;
}

static int devi_query(fd, string, length, stop, ch)
	int fd;
	char *string;
	int length;
	int *stop;
	char ch;
{
	*string = '\0';
	if (*stop) return 0;
	
	int i = 0;
	for (;;)
	{
		for (i = 0 ; i < length - 1 ; i++)
		{
			char ch;
			if (devi_read(fd, &ch, 1)) return 1;
			if (ch == ' ') { *stop = 1; break; }
			if (ch == '&') break;
			string[i] = ch;
		}
		if (i >= 2 && string[0] == ch && string[1] == '=')
			break;
	}
	string[i] = '\0';
	return 0;
}

static int devi_hex(ch)
	char ch;
{
	if ('0' <= ch && ch <= '9')
		return ch - '0';
	else if ('A' <= ch && ch <= 'Z')
		return ch - 'A' + 10;
	else if ('a' <= ch && ch <= 'z')
		return ch - 'a' + 10;
	else
		return -1;
}

static void devi_percent(string)
	char *string;
{
	int length = strlen(string);
	
	int i = 0;
	for (int j = 0 ; j < length ; i++, j++)
	{
		string[i] = string[j];
		
		if (string[j] == '+') string[i] = ' ';
		
		if (string[j] != '%') continue;
		
		int a = devi_hex(string[j + 1]);
		if (a < 0) continue;
		
		int b = devi_hex(string[j + 2]);
		if (b < 0) continue;
		
		int c = a * 0x10 + b;
		if (c == '\0') continue;
		
		string[i] = c;
		
		j += 2;
	}
	string[i] = '\0';
}

static void devi_400(fd)
	int fd;
{
	static char response[] =
		"HTTP/1.1 400 Bad Request\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"bad request";
	
	devi_write(fd, response, sizeof response - 1);
}

static int devi_parse(str)
	char *str;
{
	int length = strlen(str);
	if (length == 0) return -1;
	if (length > 3) return -1;
	if (str[0] == '0') return -1;
	
	int n = 0;
	for (int i = 0 ; i < length ; i++)
	{
		if (str[i] < '0') return -1;
		if (str[i] > '9') return -1;
		n *= 10;
		n += str[i] - '0';
	}
	return n;
}

static int devi_format(str, n)
	char *str;
	unsigned int n;
{
	if (n > 99999) return 1;
	if (n > 9999) str++;
	if (n > 999) str++;
	if (n > 99) str++;
	if (n > 9) str++;
	str[1] = '\0';
	
	do *str-- = '0' + n % 10;
	while (n /= 10);
	
	return 0;
}

static int devi_fetch(ssl)
	SSL *ssl;
{
	static char ok0[] = "HTTP/1.0 200 OK\r\n";
	static char ok1[] = "HTTP/1.1 200 OK\r\n";
	static char match[sizeof ok0];
	
	int l = sizeof ok0 - 1;
	
	if (devi_tls_read(ssl, match, l)) return 1;
	if (strncmp(ok0, match, l) && strncmp(ok1, match, l)) return 1;
	
	for (;;)
	{
		char ch;
		if (devi_tls_read(ssl, &ch, 1)) return 1;
		if (ch != '\n') continue;
		if (devi_tls_read(ssl, &ch, 1)) return 1;
		if (ch == '\n') break;
		if (ch != '\r') continue;
		if (devi_tls_read(ssl, &ch, 1)) return 1;
		if (ch != '\n') return 1;
		break;
	}
	
	return 0;
}

#define devi_tls_str(ssl, str) do if (devi_tls_write(ssl, (str), sizeof (str) - 1)) { devi_400(fd); goto clean_ssl; } while (0)
#define devi_tls_out(ssl, str) do if (devi_tls_write(ssl, (str), strlen(str))) { devi_400(fd); goto clean_ssl; } while (0)

int main()
{
	static char html[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/html\r\n"
		"\r\n";
	
	static char css[] =
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: text/css\r\n"
		"\r\n";
	
	static struct sockaddr_in address = {};
	
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(devi_port);
	
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)
		return 1;
	if (bind(sock, (void *) &address, sizeof address) != 0)
		return 1;
	if (listen(sock, 0x80) != 0)
		return 1;
	
	while (1)
	{
		int fd = accept(sock, NULL, NULL);
		if (fd < 0) continue;
		
		enum devi_method method = devi_method(fd);
		if (method == devi_invalid) { devi_400(fd); goto clean; }
		
		int stop = 0;
		
		static char resource[64];
		if (devi_resource(fd, resource, sizeof resource, &stop)) { devi_400(fd); goto clean; }
		
		if (!strcmp(resource, "/devi.css"))
		{
			devi_write(fd, css, sizeof css - 1);
			if (method == devi_get)
				devi_write(fd, devi_css, sizeof devi_css - 1);
			
			goto clean;
		}
		
		if (!strcmp(resource, "/favicon.ico")) { devi_400(fd); goto clean; }
		
		if (resource[0] != '/') { devi_400(fd); goto clean; }
		
		static char query[512];
		if (devi_query(fd, query, sizeof query, &stop, 'q')) { devi_400(fd); goto clean; }
		
		if (resource[1])
		{
			if (query[0] == '\0')
			{
				strcat(query, "q=by:");
			}
			else
			{
				if (strlen(query) + strlen(resource) + 4 > sizeof query) { devi_400(fd); goto clean; }
				strcat(query, "+by:");
			}
			strcat(query, resource + 1);
		}
		
		int page = 1;
		static char page_name[4];
		if (devi_query(fd, page_name, sizeof page_name, &stop, 'p')) { devi_400(fd); goto clean; }
		if (page_name[0] != '\0')
		{
			page = devi_parse(page_name + 2);
			if (page <= 0) { devi_400(fd); goto clean; }
		}
		
		static char offset[8];
		devi_format(offset, (page - 1) * 60);
		
		static char prev_[8];
		char *prev = prev_;
		if (page == 1) prev = NULL;
		else devi_format(prev, page - 1);
		
		static char next[8];
		devi_format(next, page + 1);
		
		struct addrinfo *info;
		struct addrinfo hints = {};
		
		if (getaddrinfo(devi_address, NULL, &hints, &info) != 0)
		{
			devi_400(fd);
			goto clean;
		}
		
		struct sockaddr *address;
		size_t size;
		for (struct addrinfo *res = info ; res ; res = res->ai_next)
		{
			switch (res->ai_family)
			{
				default:
					devi_400(fd);
					goto clean_address;
				case AF_INET:
				case AF_INET6:
					address = (struct sockaddr *) res->ai_addr;
					break;
			}
			switch (res->ai_family)
			{
				case AF_INET:
					((struct sockaddr_in *) address)->sin_port = htons(443);
					size = sizeof (struct sockaddr_in);
					break;
				case AF_INET6:
					((struct sockaddr_in6 *) address)->sin6_port = htons(443);
					size = sizeof (struct sockaddr_in6);
					break;
			}
		}
		
		int sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0) { devi_400(fd); goto clean_address; }
		if (connect(sock, address, size) != 0) { devi_400(fd); goto clean_sock; }
		
		SSL *ssl = tls_create_context(0, TLS_V13);
		
		if (!ssl) { devi_400(fd); goto clean_sock; }
		if (SSL_set_fd(ssl, sock) != 1) { devi_400(fd); goto clean_ssl; }
		tls_sni_set(ssl, devi_address);
		if (SSL_connect(ssl) != 1) { devi_400(fd); goto clean_ssl; }
		
		devi_tls_str(ssl, "GET /rss.xml?");
		devi_tls_out(ssl, query);
		devi_tls_str(ssl, "&offset=");
		devi_tls_out(ssl, offset);
		devi_tls_str(ssl,
			" HTTP/1.0\r\n"
			"Host: " devi_address "\r\n"
			"Connection: close\r\n"
			"\r\n");
		
		if (devi_fetch(ssl)) { devi_400(fd); goto clean_ssl; }
		
		devi_percent(query);
		if (query[0] == '\0') query[2] = '\0';
		
		devi_write(fd, html, sizeof html - 1);
		
		if (method == devi_get)
			devi(ssl, fd, query + 2, prev, next);
		
		clean_ssl:
		SSL_shutdown(ssl);
		SSL_CTX_free(ssl);
		clean_sock:
		close(sock);
		clean_address:
		freeaddrinfo(info);
		clean:
		shutdown(fd, SHUT_RDWR);
		close(fd);
	}
}
