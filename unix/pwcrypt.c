/*
 * pwcrypt.c: local patch.
 *
 * At-rest encryption for the "auto-login password" field (Connection >
 * Data). The session file never holds plaintext: it stores an AES-256-CBC
 * blob ("ivhex:cthex"), encrypted with a 256-bit key that is generated
 * once per Unix user account and kept in ~/.putty/.pwkey, mode 0600 --
 * so the ciphertext isn't portable to another user or machine without
 * that file. This defends against casual disclosure (browsing configs,
 * an accidental backup/sync of ~/.putty/sessions), not against a fully
 * compromised account, which can always just read the key file too.
 *
 * The GUI (config.c) never decrypts a stored password back into the
 * edit box -- see password_handler() -- decryption only happens right
 * before a connection is made (unix/window.c), to hand the plaintext to
 * PuTTY's existing cmdline-password mechanism.
 *
 * Reuses PuTTY's own audited AES-256 block routines (aes256_encrypt_pubkey
 * / aes256_decrypt_pubkey, already linked in for SSH and for encrypted
 * .ppk private keys) rather than adding a new crypto dependency.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "putty.h"
#include "ssh.h"

#define PWKEY_LEN 32
#define PWIV_LEN 16

static char *pwkey_path(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return NULL;
    return dupprintf("%s/.putty/.pwkey", home);
}

static bool read_random_bytes(void *buf, size_t len)
{
    int fd = open("/dev/urandom", O_RDONLY);
    size_t got = 0;
    if (fd < 0)
        return false;
    while (got < len) {
        ssize_t n = read(fd, (char *)buf + got, len - got);
        if (n <= 0) { close(fd); return false; }
        got += (size_t)n;
    }
    close(fd);
    return true;
}

/* Returns false (and leaves key zeroed) if a key could neither be read
 * nor created -- callers must treat that as "cannot encrypt/decrypt". */
static bool get_user_key(unsigned char key[PWKEY_LEN])
{
    char *path = pwkey_path();
    char *dir;
    int fd;
    ssize_t n;

    memset(key, 0, PWKEY_LEN);
    if (!path)
        return false;

    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        n = read(fd, key, PWKEY_LEN);
        close(fd);
        sfree(path);
        return n == PWKEY_LEN;
    }

    /* No key yet: create ~/.putty (if needed) and generate one. */
    dir = dupprintf("%s", path);
    {
        char *slash = strrchr(dir, '/');
        if (slash) *slash = '\0';
    }
    mkdir(dir, 0700);
    sfree(dir);

    if (!read_random_bytes(key, PWKEY_LEN)) {
        sfree(path);
        return false;
    }

    fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        /* Lost a race with another instance creating it first: just
         * read back whatever it wrote. */
        fd = open(path, O_RDONLY);
        if (fd < 0) { sfree(path); return false; }
        n = read(fd, key, PWKEY_LEN);
        close(fd);
        sfree(path);
        return n == PWKEY_LEN;
    }
    n = write(fd, key, PWKEY_LEN);
    close(fd);
    sfree(path);
    return n == PWKEY_LEN;
}

static const char hexdigits[] = "0123456789abcdef";

static char *bytes_to_hex(const unsigned char *buf, size_t len)
{
    char *out = snewn(len*2 + 1, char);
    size_t i;
    for (i = 0; i < len; i++) {
        out[i*2] = hexdigits[buf[i] >> 4];
        out[i*2+1] = hexdigits[buf[i] & 0xF];
    }
    out[len*2] = '\0';
    return out;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Returns NULL on malformed input. *outlen is set to the decoded length. */
static unsigned char *hex_to_bytes(const char *hex, size_t *outlen)
{
    size_t hexlen = strlen(hex);
    size_t i;
    unsigned char *out;
    if (hexlen % 2 != 0)
        return NULL;
    out = snewn(hexlen / 2, unsigned char);
    for (i = 0; i < hexlen / 2; i++) {
        int hi = hexval(hex[i*2]), lo = hexval(hex[i*2+1]);
        if (hi < 0 || lo < 0) { sfree(out); return NULL; }
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    *outlen = hexlen / 2;
    return out;
}

char *encrypt_password_for_storage(const char *plaintext)
{
    unsigned char key[PWKEY_LEN], iv[PWIV_LEN];
    size_t plen, padded_len, pad, i;
    unsigned char *buf;
    char *ivhex, *cthex, *result;

    if (!plaintext || !*plaintext)
        return dupstr("");

    if (!get_user_key(key) || !read_random_bytes(iv, PWIV_LEN)) {
        /* Can't encrypt safely -- refuse to store rather than fall
         * back to plaintext. */
        return dupstr("");
    }

    plen = strlen(plaintext);
    pad = 16 - (plen % 16);         /* PKCS#7, always 1..16 */
    padded_len = plen + pad;
    buf = snewn(padded_len, unsigned char);
    memcpy(buf, plaintext, plen);
    for (i = plen; i < padded_len; i++)
        buf[i] = (unsigned char)pad;

    aes256_encrypt_pubkey(key, iv, buf, (int)padded_len);

    ivhex = bytes_to_hex(iv, PWIV_LEN);
    cthex = bytes_to_hex(buf, padded_len);
    result = dupprintf("%s:%s", ivhex, cthex);

    smemclr(buf, padded_len);
    sfree(buf);
    sfree(ivhex);
    sfree(cthex);
    smemclr(key, sizeof(key));
    return result;
}

char *decrypt_password_from_storage(const char *stored)
{
    unsigned char key[PWKEY_LEN];
    const char *colon;
    char *ivhex, *cthex;
    unsigned char *iv, *ct;
    size_t ivlen, ctlen;
    char *result;

    if (!stored || !*stored)
        return dupstr("");

    colon = strchr(stored, ':');
    if (!colon)
        return dupstr("");

    ivhex = dupprintf("%.*s", (int)(colon - stored), stored);
    cthex = dupstr(colon + 1);

    iv = hex_to_bytes(ivhex, &ivlen);
    ct = hex_to_bytes(cthex, &ctlen);
    sfree(ivhex);
    sfree(cthex);

    if (!iv || !ct || ivlen != PWIV_LEN || ctlen == 0 || ctlen % 16 != 0 ||
        !get_user_key(key)) {
        sfree(iv);
        sfree(ct);
        return dupstr("");
    }

    aes256_decrypt_pubkey(key, iv, ct, (int)ctlen);
    smemclr(key, sizeof(key));

    /* Strip PKCS#7 padding. */
    {
        unsigned char pad = ct[ctlen - 1];
        size_t plen;
        if (pad < 1 || pad > 16 || pad > ctlen) {
            sfree(iv);
            smemclr(ct, ctlen);
            sfree(ct);
            return dupstr("");
        }
        plen = ctlen - pad;
        result = snewn(plen + 1, char);
        memcpy(result, ct, plen);
        result[plen] = '\0';
    }

    sfree(iv);
    smemclr(ct, ctlen);
    sfree(ct);
    return result;
}

bool has_stored_password(Conf *conf)
{
    const char *stored = conf_get_str(conf, CONF_password);
    return stored && *stored;
}
