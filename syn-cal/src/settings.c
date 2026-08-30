/* settings.c — settings.conf, read and written one key at a time.
 *
 * SynapseOS Project — GPL-2.0-or-later
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#define _GNU_SOURCE
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTINGS_FILE "settings.conf"

static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	for (char *e = s + strlen(s); e > s; e--)
		if (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r') e[-1] = '\0';
		else break;
	return s;
}

char *settings_get(const char *key)
{
	char *path = store_path(SETTINGS_FILE);
	size_t len = 0;
	char *data = read_file(path, &len);
	free(path);
	if (!data) return NULL;

	char *found = NULL;
	for (char *save = NULL, *line = strtok_r(data, "\n", &save);
	     line; line = strtok_r(NULL, "\n", &save)) {
		char *hash = line;
		while (*hash == ' ' || *hash == '\t') hash++;
		if (*hash == '#') continue;

		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		char *k = trim(line);
		char *v = trim(eq + 1);
		if (!strcmp(k, key)) {
			free(found);
			found = xstrdup(v);       /* the LAST one wins, like most conf readers */
		}
	}
	free(data);
	return found;
}

bool settings_set(const char *key, const char *value, char **err)
{
	char *path = store_path(SETTINGS_FILE);
	size_t len = 0;
	char *data = read_file(path, &len);

	buf_t out;
	buf_init(&out);
	buf_addstr(&out, "# syn-cal settings.\n"
	                 "# `syn-cal weekstart` and `syn-cal default` are the supported\n"
	                 "# ways to edit this.\n");

	bool written = false;

	/* ⚠ EVERY OTHER LINE IS COPIED THROUGH, comments included. This file is
	 * small and hand-editable, and a writer that regenerated it would delete
	 * whatever a person had put in it — and every setting it did not personally
	 * know about. */
	if (data) {
		for (char *save = NULL, *line = strtok_r(data, "\n", &save);
		     line; line = strtok_r(NULL, "\n", &save)) {
			char *copy = xstrdup(line);

			char *lead = line;
			while (*lead == ' ' || *lead == '\t') lead++;
			if (*lead == '#') { free(copy); continue; }   /* our header is re-added above */

			char *eq = strchr(line, '=');
			if (!eq) {
				if (*lead) buf_addf(&out, "%s\n", copy);
				free(copy);
				continue;
			}
			*eq = '\0';
			char *k = trim(line);
			if (!strcmp(k, key)) {
				if (value) { buf_addf(&out, "%s = %s\n", key, value); written = true; }
				/* value == NULL: drop the line, which is how a key is removed */
				free(copy);
				continue;
			}
			buf_addf(&out, "%s\n", copy);
			free(copy);
		}
		free(data);
	}

	if (value && !written) buf_addf(&out, "%s = %s\n", key, value);

	bool ok = write_file_atomic(path, out.b, out.len, 0600);
	if (!ok && err) *err = xasprintf("could not write %s", path);
	buf_free(&out);
	free(path);
	return ok;
}
