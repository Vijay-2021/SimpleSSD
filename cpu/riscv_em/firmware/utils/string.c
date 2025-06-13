/** @author Levente Kurusa <levex@linux.com> **/
#include "string.h"
#include <stddef.h>
#include <stdint.h>
#include "utils.h"
#include "memory.h"
size_t strlen(const string str)
{
	size_t i = 0;
	while(str[i] != 0) i++;
	return i;
}

void atoi(char *str, int* a)
{
	int k = 0;
	while(*str)
	{
		k = (k<<3)+(k<<1)+(*str)-'0';
		str++;
	}
	*a = k;
}

size_t strcrl(string str, const char what, const char with)
{
	size_t i = 0;
	while(str[i] != 0)
	{
		if(str[i] == what) str[i] = with;
		i++;
	}
	return i;
}

size_t strcount(string str, char c)
{
	size_t i = 0;
	while(*str--)
		if(*str == c) i++;
	return i;
}

size_t str_backspace(string str, char c)
{
	size_t i = strlen(str);
	i--;
	while(i)
	{
		i--;
		if(str[i] == c)
		{
			str[i+1] = 0;
			return 1;
		}
	}
	return 0;
}

size_t strsplit(string str, char delim)
{
	size_t n = 0;
	uint32_t i = 0;
	while(str[i])
	{
		if(str[i] == delim)
		{
			str[i] = 0;
			n++;
		}
		i++;
	}
	n++;
	return n;
}

size_t str_begins_with(const string str, const string with)
{
	size_t j = strlen(with);
	size_t i = 0;
	size_t ret = 1;
	while(with[j] != 0)
	{
		if(str[i] != with[i]) { ret = 0; break; }
		j--;
		i++;
	}
	return ret;
}

size_t strcmp(string str1, string str2)
{
	size_t res=0;
	while (!(res = *(unsigned char*)str1 - *(unsigned char*)str2) && *str2)
		++str1, ++str2;

	return res;
}

string strncpy(string dest, const string src, size_t n)
{
	return (string) memcpy(dest, src, n);
}

string strcpy(string dest, string src)
{
	return strncpy(dest, src, strlen(src) + 1);
}

extern int strchr(string delim, char c) {
    while (*delim) {
        if (*delim == c) return 1;
        delim++;
    }
    return 0;
}

string strtok(string str, string delim)
{
    static char *next;
    if (str) next = str;
    if (!next) return NULL;

    // Skip leading delimiters
    while (*next && strchr(delim, *next)) next++;
    if (!*next) return NULL;

    char *start = next;

    // Find end of token
    while (*next && !strchr(delim, *next)) next++;

    if (*next) {
        *next = '\0';
        next++;
    } else {
        next = NULL;
    }

    return start;
}
