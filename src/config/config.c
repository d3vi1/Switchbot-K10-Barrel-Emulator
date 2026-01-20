#include "k10_barrel/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define K10_MAX_LINE 256

static void k10_config_set_defaults(struct k10_config *config) {
    memset(config, 0, sizeof(*config));
    strncpy(config->adapter, "hci0", sizeof(config->adapter) - 1);
    strncpy(config->local_name, "WoS1MB", sizeof(config->local_name) - 1);
    config->company_id = 0x0969;
    config->include_tx_power = true;
    config->fw_major = 1;
    config->fw_minor = 0;
}

static char *k10_trim(char *value) {
    char *end = NULL;

    while (*value != '\0' && isspace((unsigned char)*value)) {
        value++;
    }

    if (*value == '\0') {
        return value;
    }

    end = value + strlen(value) - 1;
    while (end > value && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }

    return value;
}

static void k10_copy_string(char *dest, size_t dest_size, const char *src) {
    if (dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static int k10_parse_bool(const char *value, bool *out_value) {
    if (strcasecmp(value, "true") == 0) {
        *out_value = true;
        return 0;
    }

    if (strcasecmp(value, "false") == 0) {
        *out_value = false;
        return 0;
    }

    return -1;
}

static int k10_parse_uint(const char *value, unsigned int *out_value) {
    char *end = NULL;
    unsigned long parsed = 0;

    errno = 0;
    parsed = strtoul(value, &end, 0);
    if (errno != 0 || end == value) {
        return -1;
    }

    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return -1;
        }
        end++;
    }

    *out_value = (unsigned int)parsed;
    return 0;
}

static int k10_parse_string(char *value, char *out, size_t out_size) {
    size_t len = 0;

    value = k10_trim(value);
    len = strlen(value);

    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        value[len - 1] = '\0';
        value++;
    }

    k10_copy_string(out, out_size, value);
    return 0;
}

static void k10_reset_service_uuids(struct k10_config *config) {
    config->service_uuid_count = 0;
    memset(config->service_uuids, 0, sizeof(config->service_uuids));
}

static int k10_parse_service_uuids(char *value, struct k10_config *config) {
    char *cursor = NULL;

    value = k10_trim(value);
    if (*value != '[') {
        return -1;
    }

    k10_reset_service_uuids(config);
    value++;
    cursor = value;

    while (*cursor != '\0') {
        char *item_start = NULL;
        char *item_end = NULL;

        while (*cursor != '\0' && isspace((unsigned char)*cursor)) {
            cursor++;
        }

        if (*cursor == ']') {
            return 0;
        }

        if (*cursor == ',') {
            cursor++;
            continue;
        }

        if (*cursor != '"') {
            return -1;
        }

        item_start = cursor + 1;
        item_end = strchr(item_start, '"');
        if (item_end == NULL) {
            return -1;
        }

        if (config->service_uuid_count < K10_MAX_UUIDS) {
            size_t length = (size_t)(item_end - item_start);
            if (length >= sizeof(config->service_uuids[0])) {
                length = sizeof(config->service_uuids[0]) - 1;
            }
            memcpy(config->service_uuids[config->service_uuid_count], item_start, length);
            config->service_uuids[config->service_uuid_count][length] = '\0';
            config->service_uuid_count++;
        }

        cursor = item_end + 1;
    }

    return 0;
}

static int k10_apply_config_line(char *line, struct k10_config *config) {
    char *equals = NULL;
    char *key = NULL;
    char *value = NULL;

    if (line == NULL) {
        return -1;
    }

    equals = strchr(line, '=');
    if (equals == NULL) {
        return 0;
    }

    *equals = '\0';
    key = k10_trim(line);
    value = k10_trim(equals + 1);

    if (*key == '\0' || *value == '\0') {
        return 0;
    }

    if (strcmp(key, "adapter") == 0) {
        return k10_parse_string(value, config->adapter, sizeof(config->adapter));
    }

    if (strcmp(key, "local_name") == 0) {
        return k10_parse_string(value, config->local_name, sizeof(config->local_name));
    }

    if (strcmp(key, "company_id") == 0) {
        return k10_parse_uint(value, &config->company_id);
    }

    if (strcmp(key, "manufacturer_mac_label") == 0) {
        return k10_parse_string(value, config->manufacturer_mac_label,
                                sizeof(config->manufacturer_mac_label));
    }

    if (strcmp(key, "service_uuids") == 0) {
        return k10_parse_service_uuids(value, config);
    }

    if (strcmp(key, "fd3d_service_data_hex") == 0) {
        return k10_parse_string(value, config->fd3d_service_data_hex,
                                sizeof(config->fd3d_service_data_hex));
    }

    if (strcmp(key, "include_tx_power") == 0) {
        return k10_parse_bool(value, &config->include_tx_power);
    }

    if (strcmp(key, "fw_major") == 0) {
        return k10_parse_uint(value, &config->fw_major);
    }

    if (strcmp(key, "fw_minor") == 0) {
        return k10_parse_uint(value, &config->fw_minor);
    }

    return 0;
}

int k10_config_load(const char *path, struct k10_config *out_config) {
    FILE *file = NULL;
    char line[K10_MAX_LINE];

    if (out_config == NULL) {
        return -1;
    }

    k10_config_set_defaults(out_config);

    if (path == NULL) {
        return 0;
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *comment = NULL;
        char *cursor = line;

        comment = strpbrk(cursor, "#;");
        if (comment != NULL) {
            *comment = '\0';
        }

        cursor = k10_trim(cursor);
        if (*cursor == '\0') {
            continue;
        }

        k10_apply_config_line(cursor, out_config);
    }

    fclose(file);
    return 0;
}

int k10_config_save(const char *path, const struct k10_config *config) {
    FILE *file = NULL;

    if (path == NULL || config == NULL) {
        return -1;
    }

    file = fopen(path, "w");
    if (file == NULL) {
        return -1;
    }

    fprintf(file, "adapter = \"%s\"\n", config->adapter);
    fprintf(file, "local_name = \"%s\"\n", config->local_name);
    fprintf(file, "company_id = 0x%04X\n", config->company_id);
    fprintf(file, "manufacturer_mac_label = \"%s\"\n", config->manufacturer_mac_label);

    fprintf(file, "service_uuids = [");
    for (unsigned int i = 0; i < config->service_uuid_count; i++) {
        fprintf(file, "\"%s\"", config->service_uuids[i]);
        if (i + 1 < config->service_uuid_count) {
            fprintf(file, ", ");
        }
    }
    fprintf(file, "]\n");

    fprintf(file, "fd3d_service_data_hex = \"%s\"\n", config->fd3d_service_data_hex);
    fprintf(file, "include_tx_power = %s\n", config->include_tx_power ? "true" : "false");
    fprintf(file, "fw_major = %u\n", config->fw_major);
    fprintf(file, "fw_minor = %u\n", config->fw_minor);

    fclose(file);
    return 0;
}
