// Copyright (c) 2010-2010 Synology Inc. All rights reserved.
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int GetKeyValue(const char *szFile, const char *szKey, char *szValue, int cbValue);
int FileFindKey(const char *szFile, const char *szKey);

/**
 * @return <ul>
 *  <li>-1: Error
 *  <li> 0: Key not found
 *  <li> 1: Found
 *  </ul>
 */
static int FileGetLineByKey(const char *szFile, const char *szKey, char **pszLine, size_t *pcbLine)
{
	int found = -1;
	int keylen = strlen(szKey);
	int length = 0;
	char *szPtr1 = NULL;
	char *szPtr2 = NULL;
	char *szEnd = NULL;
	FILE *pFile = NULL;

	if ((NULL == szFile) || (NULL == szKey) || (NULL == pszLine) || (NULL == pcbLine)) {
		goto END;
	}
	if (NULL == (pFile = fopen(szFile, "r"))) {
		goto END;
	}
	while (-1 != (length = getline(pszLine, pcbLine, pFile))) {
		szPtr1 = *pszLine;
		szEnd = *pszLine + length;
		*szEnd = '\0';

		// Skip blank prefix
		while (((*szPtr1 == ' ') || (*szPtr1 == '\t')) && (szPtr1 < szEnd)) {
			szPtr1++;
		}
		// Skip comment or empty line
		if ((*szPtr1 == '#') || (*szPtr1 == '\n') || (*szPtr1 == '\0')) {
			continue;
		}
		if (0 != strncmp(szKey, szPtr1, keylen)) {
			continue;
		}
		szPtr2 = szPtr1 + keylen;
		if ((*szPtr2 == ' ') || (*szPtr2 == '=') || (*szPtr2 == '\t') || 
		    (*szPtr2 == '\n') || (*szPtr2 == '\r')) {
			// Yes, we found the key
			found = 1;
			goto END;
		}
	}
	found = 0;
END:
	if (NULL != pFile) {
		fclose(pFile);
	}
	return found;
}

/**
 * @return <ul>
 *  <li>-1: Error
 *  <li> 0: Key not found
 *  <li> 1: Found
 *  </ul>
 */
int GetKeyValue(const char *szFile, const char *szKey, char *szValue, int cbValue)
{
	int error = -1;
	int ret = -1;
	size_t cbBuf = 0;
	char *szBuf = NULL;
	char *szHead = NULL;
	char *szEnd = NULL;

	if ((NULL == szFile) || (NULL == szKey) || (NULL == szValue) || (0 >= cbValue)) {
		goto END;
	}
	ret = FileGetLineByKey(szFile, szKey, &szBuf, &cbBuf);
	if (1 != ret) {
		error = ret;
		goto END;
	}
	// Point to the begin of value
	if (NULL == (szHead = strchr(szBuf, '='))) {
		error = 0;
		goto END;
	}
	do {
		szHead++;
	} while (((*szHead == ' ') || (*szHead == '\t')) && (*szHead != '\0'));

	// Point to the end of value and skip blanks in the end of string
	szEnd = szBuf + strlen(szBuf);
	do {
		szEnd--;
	} while(((*szEnd == ' ') || (*szEnd == '\t') || (*szEnd == '\n') || (*szEnd == '\0')));

	// Skip the double quote ""
	if ((*szHead == '"') && (*szEnd == '"')) {
		szHead++;
		szEnd--;
	}
	if ((szEnd + 1) >= szHead) {
		*(szEnd + 1) = 0;
	} else {
		*szHead = 0;
	}
	
	if (cbValue < strlen(szHead) + 1) {
		fprintf(stderr, "%s(%d) Buffer is too small", __FILE__, __LINE__);
		goto END;
	}
	snprintf(szValue, cbValue, "%s", szHead);
	error = 1;
END:
	if (NULL != szBuf) {
		free(szBuf);
	}
	return error;
}

/**
 * @return <ul>
 *  <li>-1: Error
 *  <li> 0: Key not found
 *  <li> 1: Found
 *  </ul>
 */
int FileFindKey(const char *szFile, const char *szKey)
{
	int error = -1;
	size_t cbBuf = 0;
	char *szBuf = NULL;

	if ((NULL == szFile) || (NULL == szKey)) {
		goto END;
	}
	error = FileGetLineByKey(szFile, szKey, &szBuf, &cbBuf);
END:
	if (NULL != szBuf) {
		free(szBuf);
	}
	return error;
}
