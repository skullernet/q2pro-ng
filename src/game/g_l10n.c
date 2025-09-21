// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "shared/files.h"

#define L10N_FILE   "localization/loc_english.txt"

typedef struct {
    const char *key;
    const char *value;
} message_t;

static message_t messages[4096];
static int nb_messages;

static char message_data[0x80000];
static int message_size;

static int messagecmp(const void *p1, const void *p2)
{
    return strcmp(((const message_t *)p1)->key, ((const message_t *)p2)->key);
}

static char *copy_string(const char *s)
{
    size_t len = strlen(s) + 1;

    if (len > sizeof(message_data) - message_size)
        G_Error("Message data overflow");

    char *d = message_data + message_size;
    message_size += len;

    memcpy(d, s, len);
    return d;
}

static void parse_line(const char *data)
{
    char *key, *val, *p, *s;

    key = COM_Parse(&data);
    p = COM_Parse(&data);
    val = COM_Parse(&data);
    if (!data || strcmp(p, "="))
        return;

    // remove %junk%%junk% prefixes
    while (*val == '%') {
        if (!(p = strchr(val + 1, '%')))
            break;
        val = p + 1;
    }

    // unescape linefeeds
    s = p = val;
    while (*s) {
        if (*s == '\\' && s[1] == 'n') {
            *p++ = '\n';
            s += 2;
        } else {
            *p++ = *s++;
        }
    }
    *p = 0;

    if (nb_messages == q_countof(messages))
        G_Error("Too many messages");

    message_t *m = &messages[nb_messages++];
    m->key = copy_string(key);
    m->value = copy_string(val);
}

void G_FreeL10nFile(void)
{
    nb_messages = 0;
    message_size = 0;
}

void G_LoadL10nFile(void)
{
    char buf[MAX_QPATH], *s, *p;
    qhandle_t f;
    int ret;

    G_FreeL10nFile();

    ret = trap_FS_OpenFile(L10N_FILE, &f, FS_MODE_READ);
    if (!f) {
        trap_FS_ErrorString(ret, buf, sizeof(buf));
        G_Printf("Couldn't open %s: %s\n", L10N_FILE, buf);
        return;
    }

    ret = trap_FS_ReadFile(message_data, sizeof(message_data) - 1, f);
    trap_FS_CloseFile(f);
    if (ret < 0) {
        trap_FS_ErrorString(ret, buf, sizeof(buf));
        G_Printf("Couldn't read %s: %s\n", L10N_FILE, buf);
        return;
    }

    message_data[ret] = 0;
    s = message_data;
    while (1) {
        p = strchr(s, '\n');
        if (p)
            *p = 0;
        parse_line(s);
        if (p)
            s = p + 1;
        else
            break;
    }

    qsort(messages, nb_messages, sizeof(messages[0]), messagecmp);

    G_DPrintf("Loaded %d messages from %s\n", nb_messages, L10N_FILE);
}

const char *G_GetL10nString(const char *key)
{
    int left = 0;
    int right = nb_messages - 1;

    while (left <= right) {
        int i = (left + right) / 2;
        int r = strcmp(key, messages[i].key);
        if (r < 0)
            right = i - 1;
        else if (r > 0)
            left = i + 1;
        else
            return messages[i].value;
    }

    return NULL;
}
