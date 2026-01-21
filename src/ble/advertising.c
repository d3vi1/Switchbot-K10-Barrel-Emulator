#include "k10_barrel/advertising.h"

#include "k10_barrel/dbus_defs.h"
#include "k10_barrel/log.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

#define K10_BLUEZ_SERVICE "org.bluez"
#define K10_ADV_IFACE "org.bluez.LEAdvertisement1"
#define K10_ADV_MGR_IFACE "org.bluez.LEAdvertisingManager1"
#define K10_ADV_OBJECT "/ro/vilt/SwitchbotBleEmulator/advertisement0"
#define K10_ADV_MAX_LEN 31
#define K10_ADV_FLAGS_LEN 3
#define K10_ADV_PRIMARY_UUID "CBA20D00-224D-11E6-9FB8-0002A5D5C51B"

#define K10_MGMT_OP_ADD_EXT_ADV_PARAMS 0x0054
#define K10_MGMT_OP_ADD_EXT_ADV_DATA 0x0055
#define K10_MGMT_OP_REMOVE_ADV 0x003f
#define K10_MGMT_OP_SET_EXT_ADV_ENABLE 0x0059

#define K10_MGMT_EV_CMD_COMPLETE 0x0001
#define K10_MGMT_ADV_INSTANCE 1

struct k10_hex_bytes {
    uint8_t data[64];
    size_t length;
};

static int k10_adv_append_ad(uint8_t *buffer, size_t *buffer_len, uint8_t ad_type,
                             const uint8_t *data, size_t data_len);
static uint8_t k10_adv_random_battery(void);
static uint8_t k10_adv_next_seq(struct k10_adv_state *state);

static int k10_hci_le_set_advertising_parameters(int dd, uint16_t min_interval,
                                                 uint16_t max_interval, uint8_t advtype,
                                                 uint8_t own_bdaddr_type,
                                                 uint8_t direct_bdaddr_type,
                                                 const bdaddr_t *direct_bdaddr, uint8_t chan_map,
                                                 uint8_t filter, int to);
static int k10_hci_le_set_advertising_data(int dd, uint8_t length, const uint8_t *data, int to);
static int k10_hci_le_set_scan_response_data(int dd, uint8_t length, const uint8_t *data, int to);
static int k10_hci_le_set_advertise_enable(int dd, uint8_t enable, int to);
static int k10_hci_le_set_random_address(int dd, const bdaddr_t *addr, int to);

struct k10_mgmt_hdr {
    uint16_t opcode;
    uint16_t index;
    uint16_t len;
} __attribute__((packed));

struct k10_mgmt_ev_cmd_complete {
    uint16_t opcode;
    uint8_t status;
    uint8_t data[];
} __attribute__((packed));

struct k10_mgmt_cp_add_ext_adv_params {
    uint8_t instance;
    uint32_t flags;
    uint16_t duration;
    uint16_t timeout;
    uint16_t min_interval;
    uint16_t max_interval;
    int8_t tx_power;
} __attribute__((packed));

struct k10_mgmt_cp_add_ext_adv_data {
    uint8_t instance;
    uint8_t adv_len;
    uint8_t scan_rsp_len;
    uint8_t data[];
} __attribute__((packed));

struct k10_mgmt_cp_set_ext_adv_enable_inst {
    uint8_t instance;
    uint16_t duration;
    uint16_t timeout;
} __attribute__((packed));

struct k10_mgmt_cp_set_ext_adv_enable {
    uint8_t enable;
    uint8_t count;
    struct k10_mgmt_cp_set_ext_adv_enable_inst inst[];
} __attribute__((packed));

static int k10_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static int k10_parse_hex_bytes(const char *hex, struct k10_hex_bytes *out) {
    size_t length = 0;
    int high = -1;

    if (hex == NULL || out == NULL) {
        return -1;
    }

    memset(out, 0, sizeof(*out));

    for (size_t i = 0; hex[i] != '\0'; i++) {
        char ch = hex[i];
        int value = 0;

        if (ch == ':' || ch == ' ' || ch == '\t') {
            continue;
        }

        value = k10_hex_value(ch);
        if (value < 0) {
            return -1;
        }

        if (high < 0) {
            high = value;
            continue;
        }

        if (length >= sizeof(out->data)) {
            return -1;
        }

        out->data[length++] = (uint8_t)((high << 4) | value);
        high = -1;
    }

    if (high >= 0) {
        return -1;
    }

    out->length = length;
    return 0;
}

static bool k10_adv_use_mgmt(const struct k10_config *config) {
    if (config == NULL) {
        return false;
    }

    if (config->advertising_backend[0] == '\0') {
        return false;
    }

    return strcasecmp(config->advertising_backend, "mgmt") == 0;
}

static bool k10_adv_use_hci(const struct k10_config *config) {
    if (config == NULL) {
        return false;
    }

    if (config->advertising_backend[0] == '\0') {
        return false;
    }

    return strcasecmp(config->advertising_backend, "hci") == 0;
}

static int k10_adv_build_mfg_payload(struct k10_adv_state *state, uint8_t *payload,
                                     size_t *payload_len) {
    struct k10_hex_bytes bytes = {0};
    uint8_t seq = 1;

    if (payload == NULL || payload_len == NULL) {
        return -1;
    }

    *payload_len = 0;

    if (!state->include_manufacturer_data || state->config.manufacturer_mac_label[0] == '\0') {
        return 0;
    }

    if (k10_parse_hex_bytes(state->config.manufacturer_mac_label, &bytes) != 0 ||
        bytes.length == 0) {
        return -1;
    }

    if (bytes.length >= 6) {
        seq = k10_adv_next_seq(state);
        memcpy(payload, bytes.data, 6);
        payload[6] = seq;
        if (bytes.length > 6) {
            memcpy(payload + 7, bytes.data + 6, bytes.length - 6);
        }
        *payload_len = bytes.length + 1;
    } else {
        memcpy(payload, bytes.data, bytes.length);
        *payload_len = bytes.length;
    }

    if (bytes.length == 8 && *payload_len >= 9) {
        payload[8] = k10_adv_random_battery();
    }

    return 0;
}

static int k10_adv_build_service_data(struct k10_adv_state *state, uint8_t *payload,
                                      size_t *payload_len) {
    struct k10_hex_bytes bytes = {0};

    if (payload == NULL || payload_len == NULL) {
        return -1;
    }

    *payload_len = 0;

    if (!state->include_service_data || state->config.fd3d_service_data_hex[0] == '\0') {
        return 0;
    }

    if (k10_parse_hex_bytes(state->config.fd3d_service_data_hex, &bytes) != 0 ||
        bytes.length == 0) {
        return -1;
    }

    memcpy(payload, bytes.data, bytes.length);
    *payload_len = bytes.length;
    return 0;
}

static int k10_adv_build_buffers(struct k10_adv_state *state, const struct k10_config *config,
                                 uint8_t *adv_buffer, size_t *adv_len, uint8_t *scan_buffer,
                                 size_t *scan_len) {
    uint8_t mfg_payload[64];
    uint8_t svc_payload[64];
    size_t mfg_len = 0;
    size_t svc_len = 0;

    if (state == NULL || config == NULL || adv_buffer == NULL || adv_len == NULL ||
        scan_buffer == NULL || scan_len == NULL) {
        return -EINVAL;
    }

    *adv_len = 0;
    *scan_len = 0;

    if (state->include_manufacturer_data &&
        k10_adv_build_mfg_payload(state, mfg_payload, &mfg_len) == 0 && mfg_len > 0) {
        uint8_t mfg_field[2 + sizeof(mfg_payload)];
        size_t field_len = 0;

        mfg_field[0] = (uint8_t)(config->company_id & 0xff);
        mfg_field[1] = (uint8_t)((config->company_id >> 8) & 0xff);
        memcpy(mfg_field + 2, mfg_payload, mfg_len);
        field_len = mfg_len + 2;
        if (k10_adv_append_ad(adv_buffer, adv_len, 0xff, mfg_field, field_len) != 0) {
            return -EINVAL;
        }
    }

    if (state->include_local_name && config->local_name[0] != '\0') {
        size_t name_len = strlen(config->local_name);
        if (k10_adv_append_ad(adv_buffer, adv_len, 0x09, (const uint8_t *)config->local_name,
                              name_len) != 0) {
            return -EINVAL;
        }
    }

    if (state->include_tx_power) {
        uint8_t tx = 0x00;
        if (k10_adv_append_ad(adv_buffer, adv_len, 0x0a, &tx, 1) != 0) {
            return -EINVAL;
        }
    }

    {
        uint8_t flags = 0x06;
        if (k10_adv_append_ad(adv_buffer, adv_len, 0x01, &flags, 1) != 0) {
            return -EINVAL;
        }
    }

    if (state->include_service_data &&
        k10_adv_build_service_data(state, svc_payload, &svc_len) == 0 && svc_len > 0) {
        uint8_t svc_field[2 + sizeof(svc_payload)];
        size_t field_len = 0;

        svc_field[0] = 0x3d;
        svc_field[1] = 0xfd;
        memcpy(svc_field + 2, svc_payload, svc_len);
        field_len = svc_len + 2;
        if (k10_adv_append_ad(scan_buffer, scan_len, 0x16, svc_field, field_len) != 0) {
            return -EINVAL;
        }
    }

    return 0;
}

static int k10_adv_append_ad(uint8_t *buffer, size_t *buffer_len, uint8_t ad_type,
                             const uint8_t *data, size_t data_len) {
    size_t needed = 0;

    if (buffer == NULL || buffer_len == NULL) {
        return -1;
    }

    needed = 1 + 1 + data_len;
    if (*buffer_len + needed > K10_ADV_MAX_LEN) {
        return -1;
    }

    buffer[*buffer_len] = (uint8_t)(1 + data_len);
    buffer[*buffer_len + 1] = ad_type;
    if (data_len > 0 && data != NULL) {
        memcpy(buffer + *buffer_len + 2, data, data_len);
    }

    *buffer_len += needed;
    return 0;
}

static uint16_t k10_adv_adapter_index(const char *adapter) {
    if (adapter == NULL || strncmp(adapter, "hci", 3) != 0) {
        return 0;
    }

    return (uint16_t)atoi(adapter + 3);
}

static int k10_mgmt_open(struct k10_adv_state *state) {
    struct sockaddr_hci addr;

    if (state == NULL) {
        return -1;
    }

    if (state->mgmt_fd >= 0) {
        return 0;
    }

    state->mgmt_fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_CLOEXEC, BTPROTO_HCI);
    if (state->mgmt_fd < 0) {
        return -errno;
    }

    memset(&addr, 0, sizeof(addr));
    addr.hci_family = AF_BLUETOOTH;
    addr.hci_dev = HCI_DEV_NONE;
    addr.hci_channel = HCI_CHANNEL_CONTROL;

    if (bind(state->mgmt_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        int err = -errno;
        close(state->mgmt_fd);
        state->mgmt_fd = -1;
        return err;
    }

    return 0;
}

static int k10_mgmt_send_cmd(int fd, uint16_t opcode, uint16_t index, const void *data,
                             uint16_t len) {
    struct k10_mgmt_hdr hdr;
    ssize_t written = 0;

    hdr.opcode = opcode;
    hdr.index = index;
    hdr.len = len;

    written = write(fd, &hdr, sizeof(hdr));
    if (written != (ssize_t)sizeof(hdr)) {
        return -errno;
    }

    if (len > 0 && data != NULL) {
        written = write(fd, data, len);
        if (written != (ssize_t)len) {
            return -errno;
        }
    }

    return 0;
}

static int k10_mgmt_wait_cmd_complete(int fd, uint16_t opcode, uint8_t *status) {
    uint8_t buffer[512];

    if (status != NULL) {
        *status = 0xff;
    }

    for (;;) {
        ssize_t r = read(fd, buffer, sizeof(buffer));
        struct k10_mgmt_hdr *hdr = (struct k10_mgmt_hdr *)buffer;
        struct k10_mgmt_ev_cmd_complete *ev = NULL;

        if (r < (ssize_t)sizeof(*hdr)) {
            return -errno;
        }

        if (hdr->opcode != K10_MGMT_EV_CMD_COMPLETE) {
            continue;
        }

        if (r < (ssize_t)(sizeof(*hdr) + sizeof(*ev))) {
            return -EINVAL;
        }

        ev = (struct k10_mgmt_ev_cmd_complete *)(buffer + sizeof(*hdr));
        if (ev->opcode != opcode) {
            continue;
        }

        if (status != NULL) {
            *status = ev->status;
        }
        return ev->status == 0x00 ? 0 : -EIO;
    }
}

static int k10_adv_mgmt_start(struct k10_adv_state *state, const struct k10_config *config) {
    uint8_t adv_buffer[K10_ADV_MAX_LEN];
    uint8_t scan_buffer[K10_ADV_MAX_LEN];
    size_t adv_len = 0;
    size_t scan_len = 0;
    struct k10_mgmt_cp_add_ext_adv_params params;
    uint8_t cmd_buffer[512];
    struct k10_mgmt_cp_add_ext_adv_data *adv_data = NULL;
    uint8_t status = 0;
    uint16_t index = 0;
    int r = 0;

    if (state == NULL || config == NULL) {
        return -EINVAL;
    }

    r = k10_mgmt_open(state);
    if (r < 0) {
        k10_log_error("mgmt open failed: %s", strerror(-r));
        return r;
    }

    index = k10_adv_adapter_index(config->adapter);
    state->mgmt_instance = K10_MGMT_ADV_INSTANCE;

    if (k10_adv_build_buffers(state, config, adv_buffer, &adv_len, scan_buffer, &scan_len) != 0) {
        k10_log_error("mgmt adv data too large");
        return -EINVAL;
    }

    memset(&params, 0, sizeof(params));
    params.instance = state->mgmt_instance;
    params.flags = 0x00010001;
    params.duration = 0;
    params.timeout = 0;
    params.min_interval = 0x00a0;
    params.max_interval = 0x00f0;
    params.tx_power = 0;

    r = k10_mgmt_send_cmd(state->mgmt_fd, K10_MGMT_OP_ADD_EXT_ADV_PARAMS, index, &params,
                          sizeof(params));
    if (r < 0) {
        k10_log_error("mgmt add adv params failed: %s", strerror(-r));
        return r;
    }

    r = k10_mgmt_wait_cmd_complete(state->mgmt_fd, K10_MGMT_OP_ADD_EXT_ADV_PARAMS, &status);
    if (r < 0) {
        k10_log_error("mgmt add adv params rejected: 0x%02x", status);
        return r;
    }

    adv_data = (struct k10_mgmt_cp_add_ext_adv_data *)cmd_buffer;
    adv_data->instance = state->mgmt_instance;
    adv_data->adv_len = (uint8_t)adv_len;
    adv_data->scan_rsp_len = (uint8_t)scan_len;
    memcpy(adv_data->data, adv_buffer, adv_len);
    memcpy(adv_data->data + adv_len, scan_buffer, scan_len);

    r = k10_mgmt_send_cmd(state->mgmt_fd, K10_MGMT_OP_ADD_EXT_ADV_DATA, index, adv_data,
                          (uint16_t)(sizeof(*adv_data) + adv_len + scan_len));
    if (r < 0) {
        k10_log_error("mgmt add adv data failed: %s", strerror(-r));
        return r;
    }

    r = k10_mgmt_wait_cmd_complete(state->mgmt_fd, K10_MGMT_OP_ADD_EXT_ADV_DATA, &status);
    if (r < 0) {
        k10_log_error("mgmt add adv data rejected: 0x%02x", status);
        return r;
    }

    {
        uint8_t enable_buf[sizeof(struct k10_mgmt_cp_set_ext_adv_enable) +
                           sizeof(struct k10_mgmt_cp_set_ext_adv_enable_inst)];
        struct k10_mgmt_cp_set_ext_adv_enable *enable =
            (struct k10_mgmt_cp_set_ext_adv_enable *)enable_buf;

        memset(enable, 0, sizeof(enable_buf));
        enable->enable = 1;
        enable->count = 1;
        enable->inst[0].instance = state->mgmt_instance;
        enable->inst[0].duration = 0;
        enable->inst[0].timeout = 0;

        r = k10_mgmt_send_cmd(state->mgmt_fd, K10_MGMT_OP_SET_EXT_ADV_ENABLE, index, enable,
                              (uint16_t)sizeof(enable_buf));
        if (r < 0) {
            k10_log_error("mgmt enable adv failed: %s", strerror(-r));
            return r;
        }

        r = k10_mgmt_wait_cmd_complete(state->mgmt_fd, K10_MGMT_OP_SET_EXT_ADV_ENABLE, &status);
        if (r < 0) {
            k10_log_error("mgmt enable adv rejected: 0x%02x", status);
            return r;
        }
    }

    state->mgmt_active = true;
    return 0;
}

static int k10_hci_open(struct k10_adv_state *state, const struct k10_config *config) {
    int dev_id = 0;
    int fd = 0;

    if (state == NULL || config == NULL) {
        return -EINVAL;
    }

    if (state->hci_fd >= 0) {
        return 0;
    }

    dev_id = hci_devid(config->adapter);
    if (dev_id < 0) {
        dev_id = hci_get_route(NULL);
    }
    if (dev_id < 0) {
        return -errno;
    }

    fd = hci_open_dev(dev_id);
    if (fd < 0) {
        return -errno;
    }

    state->hci_fd = fd;
    return 0;
}

static int k10_adv_hci_start(struct k10_adv_state *state, const struct k10_config *config) {
    uint8_t adv_buffer[K10_ADV_MAX_LEN];
    uint8_t scan_buffer[K10_ADV_MAX_LEN];
    size_t adv_len = 0;
    size_t scan_len = 0;
    struct k10_hex_bytes bytes = {0};
    bdaddr_t direct_addr = {{0}};
    bdaddr_t random_addr = {{0}};
    uint8_t own_addr_type = 0x00;
    int r = 0;

    if (state == NULL || config == NULL) {
        return -EINVAL;
    }

    r = k10_hci_open(state, config);
    if (r < 0) {
        k10_log_error("hci open failed: %s", strerror(-r));
        return r;
    }

    if (k10_adv_build_buffers(state, config, adv_buffer, &adv_len, scan_buffer, &scan_len) != 0) {
        k10_log_error("hci adv data too large");
        return -EINVAL;
    }

    if (config->manufacturer_mac_label[0] != '\0' &&
        k10_parse_hex_bytes(config->manufacturer_mac_label, &bytes) == 0 && bytes.length >= 6) {
        for (size_t i = 0; i < 6; i++) {
            random_addr.b[i] = bytes.data[5 - i];
        }
        if (k10_hci_le_set_random_address(state->hci_fd, &random_addr, 1000) == 0) {
            own_addr_type = 0x01;
        } else {
            k10_log_error("hci set random address failed: %s", strerror(errno));
        }
    }

    r = k10_hci_le_set_advertising_parameters(state->hci_fd, 0x00a0, 0x00f0, 0x00, own_addr_type,
                                              0x00, &direct_addr, 0x07, 0x00, 1000);
    if (r < 0) {
        k10_log_error("hci set advertising parameters failed: %s", strerror(errno));
        return -errno;
    }

    r = k10_hci_le_set_advertising_data(state->hci_fd, (uint8_t)adv_len, adv_buffer, 1000);
    if (r < 0) {
        k10_log_error("hci set advertising data failed: %s", strerror(errno));
        return -errno;
    }

    r = k10_hci_le_set_scan_response_data(state->hci_fd, (uint8_t)scan_len, scan_buffer, 1000);
    if (r < 0) {
        k10_log_error("hci set scan response data failed: %s", strerror(errno));
        return -errno;
    }

    r = k10_hci_le_set_advertise_enable(state->hci_fd, 0x01, 1000);
    if (r < 0) {
        k10_log_error("hci advertise enable failed: %s", strerror(errno));
        return -errno;
    }

    state->hci_active = true;
    return 0;
}

static int k10_adv_hci_stop(struct k10_adv_state *state) {
    int r = 0;

    if (state == NULL || !state->hci_active) {
        return 0;
    }

    r = k10_hci_le_set_advertise_enable(state->hci_fd, 0x00, 1000);
    if (r < 0) {
        r = -errno;
    }

    if (state->hci_fd >= 0) {
        close(state->hci_fd);
        state->hci_fd = -1;
    }
    state->hci_active = false;
    return r;
}

static int k10_hci_send_req(int dd, uint16_t ocf, void *cp, uint8_t clen, void *rp, uint8_t rlen,
                            int to) {
    struct hci_request req;

    memset(&req, 0, sizeof(req));
    req.ogf = OGF_LE_CTL;
    req.ocf = ocf;
    req.cparam = cp;
    req.clen = clen;
    req.rparam = rp;
    req.rlen = rlen;
    req.event = EVT_CMD_COMPLETE;

    return hci_send_req(dd, &req, to);
}

static int k10_hci_le_set_advertising_parameters(int dd, uint16_t min_interval,
                                                 uint16_t max_interval, uint8_t advtype,
                                                 uint8_t own_bdaddr_type,
                                                 uint8_t direct_bdaddr_type,
                                                 const bdaddr_t *direct_bdaddr, uint8_t chan_map,
                                                 uint8_t filter, int to) {
    le_set_advertising_parameters_cp cp;
    uint8_t status = 0;

    memset(&cp, 0, sizeof(cp));
    cp.min_interval = htobs(min_interval);
    cp.max_interval = htobs(max_interval);
    cp.advtype = advtype;
    cp.own_bdaddr_type = own_bdaddr_type;
    cp.direct_bdaddr_type = direct_bdaddr_type;
    if (direct_bdaddr != NULL) {
        bacpy(&cp.direct_bdaddr, direct_bdaddr);
    }
    cp.chan_map = chan_map;
    cp.filter = filter;

    if (k10_hci_send_req(dd, OCF_LE_SET_ADVERTISING_PARAMETERS, &cp,
                         LE_SET_ADVERTISING_PARAMETERS_CP_SIZE, &status, sizeof(status), to) < 0) {
        return -1;
    }

    if (status) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int k10_hci_le_set_advertising_data(int dd, uint8_t length, const uint8_t *data, int to) {
    le_set_advertising_data_cp cp;
    uint8_t status = 0;

    memset(&cp, 0, sizeof(cp));
    if (length > sizeof(cp.data)) {
        length = (uint8_t)sizeof(cp.data);
    }
    cp.length = length;
    if (data != NULL && length > 0) {
        memcpy(cp.data, data, length);
    }

    if (k10_hci_send_req(dd, OCF_LE_SET_ADVERTISING_DATA, &cp, LE_SET_ADVERTISING_DATA_CP_SIZE,
                         &status, sizeof(status), to) < 0) {
        return -1;
    }

    if (status) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int k10_hci_le_set_scan_response_data(int dd, uint8_t length, const uint8_t *data, int to) {
    le_set_scan_response_data_cp cp;
    uint8_t status = 0;

    memset(&cp, 0, sizeof(cp));
    if (length > sizeof(cp.data)) {
        length = (uint8_t)sizeof(cp.data);
    }
    cp.length = length;
    if (data != NULL && length > 0) {
        memcpy(cp.data, data, length);
    }

    if (k10_hci_send_req(dd, OCF_LE_SET_SCAN_RESPONSE_DATA, &cp, LE_SET_SCAN_RESPONSE_DATA_CP_SIZE,
                         &status, sizeof(status), to) < 0) {
        return -1;
    }

    if (status) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int k10_hci_le_set_advertise_enable(int dd, uint8_t enable, int to) {
    le_set_advertise_enable_cp cp;
    uint8_t status = 0;

    cp.enable = enable;

    if (k10_hci_send_req(dd, OCF_LE_SET_ADVERTISE_ENABLE, &cp, LE_SET_ADVERTISE_ENABLE_CP_SIZE,
                         &status, sizeof(status), to) < 0) {
        return -1;
    }

    if (status) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int k10_hci_le_set_random_address(int dd, const bdaddr_t *addr, int to) {
    le_set_random_address_cp cp;
    uint8_t status = 0;

    if (addr == NULL) {
        return -1;
    }

    memset(&cp, 0, sizeof(cp));
    bacpy(&cp.bdaddr, addr);

    if (k10_hci_send_req(dd, OCF_LE_SET_RANDOM_ADDRESS, &cp, LE_SET_RANDOM_ADDRESS_CP_SIZE, &status,
                         sizeof(status), to) < 0) {
        return -1;
    }

    if (status) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int k10_adv_mgmt_stop(struct k10_adv_state *state, const struct k10_config *config) {
    uint8_t instance = K10_MGMT_ADV_INSTANCE;
    uint8_t status = 0;
    uint16_t index = 0;
    int r = 0;

    if (state == NULL || config == NULL || !state->mgmt_active) {
        return 0;
    }

    index = k10_adv_adapter_index(config->adapter);

    {
        uint8_t enable_buf[sizeof(struct k10_mgmt_cp_set_ext_adv_enable) +
                           sizeof(struct k10_mgmt_cp_set_ext_adv_enable_inst)];
        struct k10_mgmt_cp_set_ext_adv_enable *enable =
            (struct k10_mgmt_cp_set_ext_adv_enable *)enable_buf;

        memset(enable, 0, sizeof(enable_buf));
        enable->enable = 0;
        enable->count = 1;
        enable->inst[0].instance = state->mgmt_instance;
        enable->inst[0].duration = 0;
        enable->inst[0].timeout = 0;

        r = k10_mgmt_send_cmd(state->mgmt_fd, K10_MGMT_OP_SET_EXT_ADV_ENABLE, index, enable,
                              (uint16_t)sizeof(enable_buf));
        if (r < 0) {
            k10_log_error("mgmt disable adv failed: %s", strerror(-r));
            return r;
        }

        r = k10_mgmt_wait_cmd_complete(state->mgmt_fd, K10_MGMT_OP_SET_EXT_ADV_ENABLE, &status);
        if (r < 0) {
            k10_log_error("mgmt disable adv rejected: 0x%02x", status);
            return r;
        }
    }

    r = k10_mgmt_send_cmd(state->mgmt_fd, K10_MGMT_OP_REMOVE_ADV, index, &instance,
                          sizeof(instance));
    if (r < 0) {
        k10_log_error("mgmt remove adv failed: %s", strerror(-r));
        return r;
    }

    r = k10_mgmt_wait_cmd_complete(state->mgmt_fd, K10_MGMT_OP_REMOVE_ADV, &status);
    if (r < 0) {
        k10_log_error("mgmt remove adv rejected: 0x%02x", status);
        return r;
    }

    state->mgmt_active = false;

    if (state->mgmt_fd >= 0) {
        close(state->mgmt_fd);
        state->mgmt_fd = -1;
    }
    return 0;
}

static uint8_t k10_adv_random_battery(void) {
    static bool seeded = false;
    const int min = 50;
    const int max = 75;
    const int span = max - min + 1;

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }

    return (uint8_t)(min + (rand() % span));
}

static uint8_t k10_adv_next_seq(struct k10_adv_state *state) {
    uint8_t current = 1;

    if (state == NULL) {
        return 1;
    }

    if (state->mfg_seq == 0) {
        state->mfg_seq = 1;
    }

    current = state->mfg_seq;
    state->mfg_seq++;
    if (state->mfg_seq == 0) {
        state->mfg_seq = 1;
    }

    return current;
}

static bool k10_uuid_is_hex(const char *uuid) {
    for (size_t i = 0; uuid[i] != '\0'; i++) {
        if (k10_hex_value(uuid[i]) < 0) {
            return false;
        }
    }
    return true;
}

static size_t k10_uuid_bytes(const char *uuid) {
    size_t len = 0;

    if (uuid == NULL) {
        return 0;
    }

    len = strlen(uuid);
    if ((len == 4 || len == 8) && k10_uuid_is_hex(uuid)) {
        return len / 2;
    }
    if (len == 32) {
        return 16;
    }
    if (len == 36) {
        return 16;
    }
    return 16;
}

static size_t k10_adv_uuid_list_length(const char *const *uuids, unsigned int uuid_count) {
    size_t count16 = 0;
    size_t count32 = 0;
    size_t count128 = 0;

    for (unsigned int i = 0; i < uuid_count; i++) {
        size_t bytes = k10_uuid_bytes(uuids[i]);
        if (bytes == 2) {
            count16++;
        } else if (bytes == 4) {
            count32++;
        } else {
            count128++;
        }
    }

    return (count16 ? (2 + 2 * count16) : 0) + (count32 ? (2 + 4 * count32) : 0) +
           (count128 ? (2 + 16 * count128) : 0);
}

static size_t k10_adv_estimated_length(const struct k10_config *config,
                                       const struct k10_adv_state *state,
                                       bool include_service_uuids, bool include_service_data,
                                       bool include_manufacturer, bool include_local_name,
                                       bool include_tx_power) {
    struct k10_hex_bytes bytes = {0};
    size_t length = K10_ADV_FLAGS_LEN;

    if (include_local_name && config->local_name[0] != '\0') {
        length += 2 + strlen(config->local_name);
    }

    if (include_tx_power) {
        length += 3;
    }

    if (include_manufacturer && config->manufacturer_mac_label[0] != '\0' &&
        k10_parse_hex_bytes(config->manufacturer_mac_label, &bytes) == 0 && bytes.length > 0) {
        if (bytes.length >= 6) {
            length += 4 + bytes.length + 1;
        } else {
            length += 4 + bytes.length;
        }
    }

    if (include_service_data && config->fd3d_service_data_hex[0] != '\0' &&
        k10_parse_hex_bytes(config->fd3d_service_data_hex, &bytes) == 0 && bytes.length > 0) {
        length += 4 + bytes.length;
    }

    if (include_service_uuids && state->service_uuid_view_count > 0) {
        length +=
            k10_adv_uuid_list_length(state->service_uuid_view, state->service_uuid_view_count);
    }

    return length;
}

static void k10_adv_select_fields(struct k10_adv_state *state) {
    const char *primary = NULL;
    size_t length = 0;

    state->service_uuid_view_count = 0;
    for (unsigned int i = 0; i < state->config.service_uuid_count; i++) {
        if (strcmp(state->config.service_uuids[i], K10_ADV_PRIMARY_UUID) == 0) {
            primary = state->config.service_uuids[i];
            break;
        }
    }

    if (primary != NULL) {
        state->service_uuid_view[0] = primary;
        state->service_uuid_view_count = 1;
    } else if (state->config.service_uuid_count > 0) {
        state->service_uuid_view[0] = state->config.service_uuids[0];
        state->service_uuid_view_count = 1;
    }

    state->include_service_uuids = state->service_uuid_view_count > 0;
    state->include_service_data = state->config.fd3d_service_data_hex[0] != '\0';
    state->include_manufacturer_data = state->config.manufacturer_mac_label[0] != '\0';
    state->include_local_name = state->config.local_name[0] != '\0';
    state->include_tx_power = state->config.include_tx_power;

    state->service_uuid_view_count = 0;
    state->include_service_uuids = false;

    length = k10_adv_estimated_length(&state->config, state, state->include_service_uuids,
                                      state->include_service_data, state->include_manufacturer_data,
                                      state->include_local_name, state->include_tx_power);

    if (length > K10_ADV_MAX_LEN && state->include_service_data) {
        k10_log_info("advertising payload too large (%zu); disabling service data", length);
        state->include_service_data = false;
        length = k10_adv_estimated_length(
            &state->config, state, state->include_service_uuids, state->include_service_data,
            state->include_manufacturer_data, state->include_local_name, state->include_tx_power);
    }

    if (length > K10_ADV_MAX_LEN && state->include_manufacturer_data) {
        k10_log_info("advertising payload too large (%zu); disabling manufacturer data", length);
        state->include_manufacturer_data = false;
        length = k10_adv_estimated_length(
            &state->config, state, state->include_service_uuids, state->include_service_data,
            state->include_manufacturer_data, state->include_local_name, state->include_tx_power);
    }

    if (length > K10_ADV_MAX_LEN && state->include_tx_power) {
        k10_log_info("advertising payload too large (%zu); disabling TX power", length);
        state->include_tx_power = false;
        length = k10_adv_estimated_length(
            &state->config, state, state->include_service_uuids, state->include_service_data,
            state->include_manufacturer_data, state->include_local_name, state->include_tx_power);
    }

    if (length > K10_ADV_MAX_LEN && state->include_local_name) {
        k10_log_info("advertising payload too large (%zu); disabling local name", length);
        state->include_local_name = false;
        length = k10_adv_estimated_length(
            &state->config, state, state->include_service_uuids, state->include_service_data,
            state->include_manufacturer_data, state->include_local_name, state->include_tx_power);
    }

    if (length > K10_ADV_MAX_LEN) {
        k10_log_error("advertising payload still too large (%zu)", length);
    }
}

static int k10_adv_get_type(sd_bus *bus, const char *path, const char *interface,
                            const char *property, sd_bus_message *reply, void *userdata,
                            sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "s", "peripheral");
}

static int k10_adv_get_service_uuids(sd_bus *bus, const char *path, const char *interface,
                                     const char *property, sd_bus_message *reply, void *userdata,
                                     sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    if (state->include_service_uuids) {
        for (unsigned int i = 0; i < state->service_uuid_view_count; i++) {
            r = sd_bus_message_append(reply, "s", state->service_uuid_view[i]);
            if (r < 0) {
                return r;
            }
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_append_variant_bytes(sd_bus_message *reply, const uint8_t *data, size_t length) {
    int r = sd_bus_message_open_container(reply, 'v', "ay");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append_array(reply, 'y', data, length);
    if (r < 0) {
        return r;
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_manufacturer_data(sd_bus *bus, const char *path, const char *interface,
                                         const char *property, sd_bus_message *reply,
                                         void *userdata, sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;
    struct k10_hex_bytes bytes = {0};
    uint8_t payload[sizeof(bytes.data) + 1];
    size_t payload_len = 0;
    uint8_t seq = 1;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "{qv}");
    if (r < 0) {
        return r;
    }

    if (state->include_manufacturer_data && state->config.manufacturer_mac_label[0] != '\0') {
        if (k10_parse_hex_bytes(state->config.manufacturer_mac_label, &bytes) == 0 &&
            bytes.length > 0) {
            if (bytes.length >= 6) {
                seq = k10_adv_next_seq(state);
                memcpy(payload, bytes.data, 6);
                payload[6] = seq;
                if (bytes.length > 6) {
                    memcpy(payload + 7, bytes.data + 6, bytes.length - 6);
                }
                payload_len = bytes.length + 1;
            } else {
                memcpy(payload, bytes.data, bytes.length);
                payload_len = bytes.length;
            }

            if (bytes.length == 8 && payload_len >= 9) {
                payload[8] = k10_adv_random_battery();
            }

            r = sd_bus_message_open_container(reply, 'e', "qv");
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_append(reply, "q", (uint16_t)state->config.company_id);
            if (r < 0) {
                return r;
            }

            r = k10_adv_append_variant_bytes(reply, payload, payload_len);
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_close_container(reply);
            if (r < 0) {
                return r;
            }
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_service_data(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;
    struct k10_hex_bytes bytes = {0};

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "{sv}");
    if (r < 0) {
        return r;
    }

    if (state->include_service_data && state->config.fd3d_service_data_hex[0] != '\0') {
        if (k10_parse_hex_bytes(state->config.fd3d_service_data_hex, &bytes) == 0 &&
            bytes.length > 0) {
            r = sd_bus_message_open_container(reply, 'e', "sv");
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_append(reply, "s", "FD3D");
            if (r < 0) {
                return r;
            }

            r = k10_adv_append_variant_bytes(reply, bytes.data, bytes.length);
            if (r < 0) {
                return r;
            }

            r = sd_bus_message_close_container(reply);
            if (r < 0) {
                return r;
            }
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_includes(sd_bus *bus, const char *path, const char *interface,
                                const char *property, sd_bus_message *reply, void *userdata,
                                sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    int r = sd_bus_message_open_container(reply, 'a', "s");
    if (r < 0) {
        return r;
    }

    if (state->include_tx_power) {
        r = sd_bus_message_append(reply, "s", "tx-power");
        if (r < 0) {
            return r;
        }
    }

    return sd_bus_message_close_container(reply);
}

static int k10_adv_get_local_name(sd_bus *bus, const char *path, const char *interface,
                                  const char *property, sd_bus_message *reply, void *userdata,
                                  sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)ret_error;

    if (!state->include_local_name) {
        return sd_bus_message_append(reply, "s", "");
    }

    return sd_bus_message_append(reply, "s", state->config.local_name);
}

static int k10_adv_get_discoverable(sd_bus *bus, const char *path, const char *interface,
                                    const char *property, sd_bus_message *reply, void *userdata,
                                    sd_bus_error *ret_error) {
    (void)bus;
    (void)path;
    (void)interface;
    (void)property;
    (void)userdata;
    (void)ret_error;

    return sd_bus_message_append(reply, "b", 1);
}

static int k10_adv_release(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    (void)ret_error;

    state->registered = false;
    k10_log_info("advertising released by BlueZ");

    return sd_bus_reply_method_return(m, "");
}

static const sd_bus_vtable k10_adv_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", k10_adv_release, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Type", "s", k10_adv_get_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceUUIDs", "as", k10_adv_get_service_uuids, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ManufacturerData", "a{qv}", k10_adv_get_manufacturer_data, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("ServiceData", "a{sv}", k10_adv_get_service_data, 0,
                    SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Includes", "as", k10_adv_get_includes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("LocalName", "s", k10_adv_get_local_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_PROPERTY("Discoverable", "b", k10_adv_get_discoverable, 0, SD_BUS_VTABLE_PROPERTY_CONST),
    SD_BUS_VTABLE_END};

static void k10_adv_set_defaults(struct k10_adv_state *state) {
    if (state->object_path[0] == '\0') {
        strncpy(state->object_path, K10_ADV_OBJECT, sizeof(state->object_path) - 1);
    }

    if (state->mgmt_fd == 0) {
        state->mgmt_fd = -1;
    }

    if (state->hci_fd == 0) {
        state->hci_fd = -1;
    }
}

static int k10_adv_register_complete(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    struct k10_adv_state *state = userdata;

    if (ret_error != NULL && sd_bus_error_is_set(ret_error)) {
        k10_log_error("advertising register failed: %s",
                      ret_error->message ? ret_error->message : ret_error->name);
        state->registered = false;
    } else if (sd_bus_message_is_method_error(m, NULL)) {
        const sd_bus_error *error = sd_bus_message_get_error(m);
        k10_log_error("advertising register failed: %s",
                      error && error->message ? error->message : "unknown error");
        state->registered = false;
    } else {
        state->registered = true;
        k10_log_info("advertising registered on %s", state->config.adapter);
    }

    state->pending = false;
    sd_bus_slot_unref(state->pending_slot);
    state->pending_slot = NULL;
    return 1;
}

static int k10_adv_register_async(sd_bus *bus, struct k10_adv_state *state, const char *adapter) {
    sd_bus_message *message = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_message_new_method_call(bus, &message, K10_BLUEZ_SERVICE, adapter_path,
                                       K10_ADV_MGR_IFACE, "RegisterAdvertisement");
    if (r < 0) {
        return r;
    }

    r = sd_bus_message_append(message, "o", state->object_path);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_open_container(message, 'a', "{sv}");
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_message_close_container(message);
    if (r < 0) {
        goto finish;
    }

    r = sd_bus_call_async(bus, &state->pending_slot, message, k10_adv_register_complete, state, 0);
    if (r < 0) {
        k10_log_error("advertising register failed: %s", strerror(-r));
    }

finish:
    sd_bus_message_unref(message);
    return r;
}

static int k10_adv_unregister(sd_bus *bus, const struct k10_adv_state *state, const char *adapter) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    sd_bus_message *reply = NULL;
    char adapter_path[128];
    int r = 0;

    snprintf(adapter_path, sizeof(adapter_path), "/org/bluez/%s", adapter);

    r = sd_bus_call_method(bus, K10_BLUEZ_SERVICE, adapter_path, K10_ADV_MGR_IFACE,
                           "UnregisterAdvertisement", &error, &reply, "o", state->object_path);
    if (r < 0) {
        k10_log_error("advertising unregister failed: %s",
                      error.message ? error.message : strerror(-r));
    }

    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    return r;
}

int k10_adv_start(sd_bus *bus, struct k10_adv_state *state, const struct k10_config *config) {
    int r = 0;

    if (bus == NULL || state == NULL || config == NULL) {
        return -EINVAL;
    }

    k10_adv_set_defaults(state);
    state->config = *config;
    state->mfg_seq = 1;
    k10_adv_select_fields(state);

    if (k10_adv_use_mgmt(config)) {
        if (state->mgmt_active) {
            return 0;
        }

        r = k10_adv_mgmt_start(state, config);
        if (r == 0) {
            state->registered = true;
            state->pending = false;
            k10_log_info("advertising registered via mgmt on %s", config->adapter);
        } else if (r == -EINVAL || r == -EOPNOTSUPP || r == -ENOTSUP || r == -ENOSYS) {
            k10_log_info("mgmt advertising failed (%s), falling back to HCI",
                         strerror(r < 0 ? -r : r));
            r = k10_adv_hci_start(state, config);
            if (r == 0) {
                state->registered = true;
                state->pending = false;
                k10_log_info("advertising registered via hci on %s", config->adapter);
            }
        }
        return r;
    }

    if (k10_adv_use_hci(config)) {
        if (state->hci_active) {
            return 0;
        }

        r = k10_adv_hci_start(state, config);
        if (r == 0) {
            state->registered = true;
            state->pending = false;
            k10_log_info("advertising registered via hci on %s", config->adapter);
        }
        return r;
    }

    if (state->registered || state->pending) {
        return 0;
    }

    if (state->slot == NULL) {
        r = sd_bus_add_object_vtable(bus, &state->slot, state->object_path, K10_ADV_IFACE,
                                     k10_adv_vtable, state);
        if (r < 0) {
            k10_log_error("advertising vtable failed: %s", strerror(-r));
            return r;
        }
    }

    r = k10_adv_register_async(bus, state, config->adapter);
    if (r < 0) {
        sd_bus_slot_unref(state->slot);
        state->slot = NULL;
        return r;
    }

    state->pending = true;
    state->registered = false;
    k10_log_info("advertising register requested on %s", config->adapter);
    return 0;
}

int k10_adv_stop(sd_bus *bus, struct k10_adv_state *state) {
    int r = 0;

    if (bus == NULL || state == NULL) {
        return -EINVAL;
    }

    if (state->mgmt_active) {
        r = k10_adv_mgmt_stop(state, &state->config);
        if (r == 0) {
            state->registered = false;
            state->pending = false;
            k10_log_info("advertising unregistered (mgmt)");
        }
        return r;
    }

    if (state->hci_active) {
        r = k10_adv_hci_stop(state);
        if (r == 0) {
            state->registered = false;
            state->pending = false;
            k10_log_info("advertising unregistered (hci)");
        }
        return r;
    }

    if (!state->registered && !state->pending) {
        return 0;
    }

    if (state->pending_slot != NULL) {
        sd_bus_slot_unref(state->pending_slot);
        state->pending_slot = NULL;
        state->pending = false;
    }

    r = 0;
    if (state->registered) {
        r = k10_adv_unregister(bus, state, state->config.adapter);
    }
    sd_bus_slot_unref(state->slot);
    state->slot = NULL;
    state->registered = false;

    k10_log_info("advertising unregistered");
    return r;
}
