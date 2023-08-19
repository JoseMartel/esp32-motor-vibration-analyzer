#include "stdio.h"
#include "string.h"
#include "OE_https_ota.h"
#include "math.h"
bool
check_version_string(uint8_t *ver_str) {
	unsigned dot_counter = 0;
	for (unsigned i = 0; i < strlen((const char *)ver_str); i++) {
		if (ver_str[i] == '.')
			dot_counter++;
	}
	if (dot_counter == 2) {
		return true;
	} else {
		return false;
	}
}

bool
str2num(uint8_t *str, uint8_t len, uint8_t *num) {
	*num = 0;

	for (unsigned i = 0; i < len; i++) {
		*num = *num + (str[i] - '0') * (int)(pow(10, len - (i + 1)));
	}

	return true;
}

bool
get_version_from_string(uint8_t *version, uint8_t *major, uint8_t *minor,
						uint8_t *build) {
	uint8_t buil[3];
	uint8_t maj[3];
	uint8_t min[3];
	if (check_version_string(version)) {
		uint8_t i = 0;
		uint8_t j = 0;
		while (version[i] != '.') {
			maj[j] = version[i];
			i++;
			j++;
		}
		str2num(maj, j, major);
		// printf("major: %d\n", *major);
		i++;
		j = 0;
		while (version[i] != '.') {
			min[j] = version[i];
			i++;
			j++;
		}
		str2num(min, j, minor);
		// printf("minor: %d\n", *minor);
		i++;
		j = 0;
		while (version[i] != 0) {
			buil[j] = version[i];
			i++;
			j++;
		}
		str2num(buil, j, build);
		// printf("build: %d\n", *minor);
		return true;
	}
	return false;
}

/* function to check a newer version */
bool
check_version(uint8_t *new_version) {
	uint8_t build_new = 0;
	uint8_t major_new = 0;
	uint8_t minor_new = 0;

    uint8_t build_old = 0;
	uint8_t major_old = 0;
	uint8_t minor_old = 0;

	/* convert new version to int */
	get_version_from_string(new_version, &major_new, &minor_new, &build_new);
	get_version_from_string((uint8_t *)CONFIG_APP_PROJECT_VER, &major_old, &minor_old, &build_old);

	/* compare versions */
	if (major_new < major_old) {
		return false;
	} else if (major_new > major_old) {
		return true;
	}
	if (minor_new < minor_old) {
		return false;
	} else if (minor_new > minor_old) {
		return true;
	}
	if (build_new <= build_old) {
		return false;
	} else {
		return true;
	}
}