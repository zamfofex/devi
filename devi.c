#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include "tlse/tlse.h"
#include "sxml/sxml.h"

#define devi_str(fd, str) if (devi_write(fd, (str), sizeof (str) - 1)) return 1
#define devi_out(fd, str) if (devi_write(fd, (str), strlen(str))) return 1

static int devi_write(fd, string, length)
	int fd;
	char *string;
	size_t length;
{
	while (length != 0)
	{
		ssize_t written = write(fd, string, length);
		if (written < 0) return 1;
		string += written;
		length -= written;
	}
	return 0;
}

/*
static int devi_html(fd, string, length)
	int fd;
	char *string;
	int length;
{
	int i;
	for (i = 0 ; i < length ; i++)
	{
		if (string[i] == '\0') break;
		if (string[i] == '<' || string[i] == '&' || string[i] == '\'')
		{
			if (devi_write(fd, string, i)) return 1;
			if (devi_write(fd, "&#x", 3)) return 1;
			
			char x = string[i] >> 4;
			if (x > 9) x += 'A' + 10;
			else x += '0';
			
			devi_write(fd, &x, 1);
			
			char x2 = string[i] & 0xF;
			if (x2 > 9) x2 += 'A' + 10;
			else x2 += '0';
			
			devi_write(fd, &x2, 1);
			
			devi_write(fd, ";", 1);
			
			string += i + 1;
			i = 0;
		}
	}
	return devi_write(fd, string, i);
}
*/

static int devi_compare(a, b, la, lb)
	char *a;
	char *b;
	int la;
	int lb;
{
	if (la != lb) return 1;
	return strncmp(a, b, la);
}

#define devi_compare_str(a, b, l) devi_compare((a), (b), sizeof (a) - 1, (l))

enum devi_state
{
	devi_start,
	devi_ignore,
	devi_deviation,
	devi_data,
};

static int devi(ssl, fd, query, prev, next)
	SSL *ssl;
	int fd;
	char *query;
	char *prev, *next;
{
	static char buffer[2048];
	static sxmltok_t tokens[16];
	
	unsigned int count = sizeof tokens / sizeof *tokens;
	
	int length = SSL_read(ssl, buffer, sizeof buffer);
	if (length < 0) return 1;
	
	enum devi_state state = devi_start;
	
	static char page_title[256];
	
	static char url[1024];
	static char title[256];
	static char deviant[256];
	static char avatar[256];
	static char thumbnail[1024];
	
	page_title[0] = '\0';
	url[0] = '\0';
	title[0] = '\0';
	deviant[0] = '\0';
	avatar[0] = '\0';
	thumbnail[0] = '\0';
	
	char *data = NULL;
	
	char *attribute = NULL;
	
	sxml_t sxml;
	sxml_init(&sxml);
	
	devi_str(fd,
		"<!doctype html>\n"
		"<html lang='en'>\n"
		"<meta charset='utf-8'>\n"
		"<meta name='viewport' content='width=device-width'>\n"
		"<link rel='stylesheet' href='/devi.css'>\n"
		"\n");
	
	for (;;)
	{
		sxml.ntokens = 0;
		sxmlerr_t err = sxml_parse(&sxml, buffer, length, tokens, count);
		
		if (err == SXML_SUCCESS) break;
		if (err == SXML_ERROR_XMLINVALID) return 1;
		
		for (unsigned int i = 0 ; i < sxml.ntokens ; i++)
		{
			sxmltok_t token = tokens[i];
			
			if (token.type == SXML_STARTTAG)
			{
				char *name = buffer + token.startpos;
				int length = token.endpos - token.startpos;
				
				if (!devi_compare_str("item", name, length))
				{
					data = NULL;
					state = devi_deviation;
				}
				else if (state == devi_start)
				{
					if (!devi_compare_str("title", name, length))
						data = page_title,
						state = devi_data;
				}
				else if (state == devi_deviation)
				{
					if (!devi_compare_str("media:content", name, length))
					{
						if (url[0] == '\0')
							data = url,
							attribute = "url",
							state = devi_deviation;
					}
					else if (!devi_compare_str("media:credit", name, length))
					{
						if (deviant[0] == '\0')
							data = deviant,
							state = devi_data;
						else if (avatar[0] == '\0')
							data = avatar,
							state = devi_data;
					}
					else if (!devi_compare_str("media:thumbnail", name, length))
					{
						if (thumbnail[0] == '\0')
							data = thumbnail,
							attribute = "url",
							state = devi_deviation;
					}
					else if (!devi_compare_str("title", name, length))
					{
						if (title[0] == '\0')
							data = title,
							state = devi_data;
					}
				}
				
				if (attribute != NULL)
				{
					unsigned int size = token.size;
					
					unsigned int j;
					for (j = 0 ; j < size ; j++) 
					{
						sxmltok_t token = tokens[i + j];
						
						if (token.type != SXML_CDATA) continue;
						
						char *name = buffer + token.startpos;
						int length = token.endpos - token.startpos;
						
						if (devi_compare(attribute, name, strlen(attribute), length)) continue;
						
						int len = 0;
						for (j++ ; j < size ; j++)
						{
							sxmltok_t token = tokens[i + j];
							if (token.type != SXML_CHARACTER) break;
							
							char *buffer2 = buffer + token.startpos;
							int length = token.endpos - token.startpos;
							if (length > 1023 - len) length = 1023 - len;
							
							strncpy(data + len, buffer2, length);
							len += length;
							data[len] = '\0';
						}
						
						break;
					}
					
					data = NULL;
					attribute = NULL;
				}
				
				i += token.size;
				continue;
			}
			
			if (token.type == SXML_ENDTAG)
			{
				char *name = buffer + token.startpos;
				int length = token.endpos - token.startpos;
				
				state = devi_deviation;
				
				if (!devi_compare_str("item", name, length))
				{
					devi_str(fd, "\t<p> <a target='_blank' href='");
					devi_out(fd, url);
					devi_str(fd, "'><img src='");
					devi_out(fd, thumbnail);
					devi_str(fd, "'> <b>");
					devi_out(fd, title);
					devi_str(fd, "</b> </a> <a href='/");
					devi_out(fd, deviant);
					devi_str(fd, "'><img src='");
					devi_out(fd, avatar);
					devi_str(fd, "'> by ");
					devi_out(fd, deviant);
					devi_str(fd, "</a> </p>\n");
					
					url[0] = '\0';
					title[0] = '\0';
					deviant[0] = '\0';
					avatar[0] = '\0';
					thumbnail[0] = '\0';
					
					state = devi_ignore;
				}
				
				if (data == page_title)
				{
					devi_str(fd, "<title> ");
					devi_out(fd, data);
					devi_str(fd, " </title>\n\n<form action='/'> <header> <a href='/'> <h1> ");
					devi_out(fd, data);
					devi_str(fd, " </h1> </a> <input name='q' placeholder='Search…' value='");
					devi_out(fd, query);
					devi_str(fd, "'> </header> </form>\n\n<div>\n");
					
					state = devi_ignore;
				}
				data = NULL;
			}
			
			if (token.type == SXML_CHARACTER || token.type == SXML_CDATA)
			{
				if (data == NULL) continue;
				
				char *buffer2 = buffer + token.startpos;
				int length = token.endpos - token.startpos;
				
				int len = strlen(data);
				if (length > 255 - len)
					length = 255 - len;
				
				// TODO: Support CDATA.
				if (token.type == SXML_CHARACTER)
					strncpy(data + len, buffer2, length);
				
				data[len + length] = '\0';
			}
		}
		
		if (err == SXML_ERROR_BUFFERDRY)
		{
			length -= sxml.bufferpos;
			memmove(buffer, buffer + sxml.bufferpos, length);
			
			int length2 = SSL_read(ssl, buffer + length, sizeof buffer - length);
			if (length2 < 0) return 1;
			
			length += length2;
			
			sxml.bufferpos = 0;
			continue;
		}
	}
	
	devi_str(fd, "</div>\n");
	if (!prev && !next) return 0;
	
	devi_str(fd, "\n<footer> <p> ");
	if (prev)
	{
		devi_str(fd, "<a href='/?q=");
		devi_out(fd, query);
		devi_str(fd, "&amp;p=");
		devi_out(fd, prev);
		devi_str(fd, "'>back</a> ");
	}
	if (prev && next) devi_str(fd, "| ");
	if (next)
	{
		devi_str(fd, "<a href='/?q=");
		devi_out(fd, query);
		devi_str(fd, "&amp;p=");
		devi_out(fd, next);
		devi_str(fd, "'>next</a> ");
	}
	devi_str(fd, "</p> </footer>\n");
	
	return 0;
}
