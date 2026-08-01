/*
 * session_export.c: local patch.
 *
 * Bundles one or more saved sessions' raw settings files into a single
 * ".pex" ("PuTTY export") file, for moving profiles between machines,
 * optionally protected by a password. Pure logic only -- no GTK here,
 * see unix/session_export_gtk.c for the dialogs that drive this.
 *
 * Deliberately does NOT reuse pwcrypt.c's per-Unix-user key file
 * (~/.putty/.pwkey): that key never leaves this machine, which is the
 * whole point for the auto-login password (defends against casual
 * disclosure of the *local* config, not a determined attacker with
 * this account). An export is explicitly meant to leave the machine,
 * so there is no local secret to lean on -- the only thing standing
 * between the ciphertext and the plaintext is the password the user
 * types at export time. The encryption key is derived from that
 * password via Argon2id (the same audited KDF this build already uses
 * for encrypted .ppk v3 private keys, see sshpubk.c), not looked up
 * from anywhere else, so a stolen/patched copy of this binary gains
 * nothing without the password -- there is no "client refuses without
 * a password" check to bypass, because without the password there is
 * no key, and without the key the ciphertext is just noise.
 *
 * Container format (before any encryption): a plain binary blob using
 * PuTTY's own SSH-style length-prefixed strings (marshal.h) --
 *   uint32                     number of sessions
 *   repeated per session:
 *     string                   session name
 *     string                   raw bytes of that session's settings
 *                              file, verbatim (whatever save_settings()
 *                              already wrote) -- copied byte-for-byte
 *                              via session_file_path() rather than
 *                              re-serialised from a Conf, so export
 *                              can't drift from whatever format
 *                              settings.c actually uses.
 *
 * File-on-disk format:
 *   PLAIN:
 *     "PUTTY-EXPORT-2 PLAIN\n"
 *     <container bytes, verbatim>
 *   ENC:
 *     "PUTTY-EXPORT-2 ENC\n"
 *     "mem=<KB> passes=<N> parallel=<N>\n"
 *     "salt=<32 hex chars>\n"
 *     "iv=<32 hex chars>\n"
 *     <AES-256-CBC ciphertext bytes of the PKCS#7-padded container>
 * The three Argon2 parameters and the salt are public (same status as
 * a salt always has); they're stored so decrypt uses the EXACT same
 * derivation the encrypting machine used, rather than re-running
 * argon2_choose_passes()'s timing calibration, which would derive a
 * different key on a different machine (see sshpubk.c's ppk v3 code,
 * which this mirrors).
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include "putty.h"
#include "ssh.h"

#define PEX_SALT_LEN 16
#define PEX_IV_LEN 16
#define PEX_KEY_LEN 32

static bool pex_random_bytes(void *buf, size_t len)
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

static const char pex_hexdigits[] = "0123456789abcdef";

static char *pex_bytes_to_hex(const unsigned char *buf, size_t len)
{
    char *out = snewn(len * 2 + 1, char);
    size_t i;
    for (i = 0; i < len; i++) {
        out[i*2] = pex_hexdigits[buf[i] >> 4];
        out[i*2+1] = pex_hexdigits[buf[i] & 0xF];
    }
    out[len*2] = '\0';
    return out;
}

static int pex_hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes exactly `outlen' bytes of hex from a NUL-terminated string;
 * false if it's the wrong length or contains non-hex characters. */
static bool pex_hex_to_bytes_fixed(const char *hex, unsigned char *out,
                                   size_t outlen)
{
    size_t i;
    if (strlen(hex) != outlen * 2)
        return false;
    for (i = 0; i < outlen; i++) {
        int hi = pex_hexval(hex[i*2]), lo = pex_hexval(hex[i*2+1]);
        if (hi < 0 || lo < 0)
            return false;
        out[i] = (unsigned char)((hi << 4) | lo);
    }
    return true;
}

/* Reads a whole file into a freshly allocated buffer. Returns NULL on
 * any error. *len is set to the file size. */
static unsigned char *pex_read_whole_file(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    long size;
    unsigned char *buf;
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    size = ftell(fp);
    if (size < 0) { fclose(fp); return NULL; }
    if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    buf = snewn((size_t)size, unsigned char);
    if (size > 0 && fread(buf, 1, (size_t)size, fp) != (size_t)size) {
        fclose(fp);
        sfree(buf);
        return NULL;
    }
    fclose(fp);
    *len = (size_t)size;
    return buf;
}

/* Reads one header line (up to and including its '\n') out of a
 * buffer, returning it as a fresh NUL-terminated string with the
 * newline stripped, and advancing *pos past it. Returns NULL (leaving
 * *pos unchanged) if there's no newline before the end of the
 * buffer. */
static char *pex_read_line(const unsigned char *buf, size_t len,
                           size_t *pos)
{
    size_t start = *pos, i;
    for (i = start; i < len; i++) {
        if (buf[i] == '\n') {
            char *line = snewn(i - start + 1, char);
            memcpy(line, buf + start, i - start);
            line[i - start] = '\0';
            *pos = i + 1;
            return line;
        }
    }
    return NULL;
}

/* Derives a PEX_KEY_LEN-byte key from `password' + `salt' into
 * `key_out', choosing (and returning via *mem/*passes/*parallel) the
 * Argon2id parameters fresh -- used only when ENCRYPTING, so the
 * chosen passes count can be stored alongside the salt for decrypt to
 * reproduce exactly. */
static void pex_derive_key_choosing_passes(
    const char *password, const unsigned char salt[PEX_SALT_LEN],
    unsigned char key_out[PEX_KEY_LEN],
    uint32_t *mem, uint32_t *passes, uint32_t *parallel)
{
    strbuf *out = strbuf_new_nm();
    ptrlen empty = PTRLEN_LITERAL("");

    *mem = 8192;                      /* 8 MB, same default as puttygen */
    *parallel = 1;

    argon2_choose_passes(
        Argon2id, *mem, 200 /* ms */, passes, *parallel, PEX_KEY_LEN,
        ptrlen_from_asciz(password), make_ptrlen(salt, PEX_SALT_LEN),
        empty, empty, out);

    memcpy(key_out, out->u, PEX_KEY_LEN);
    strbuf_free(out);
}

/* Re-derives the same key on import, given the parameters that were
 * stored at export time -- never re-calibrates timing here, or a
 * faster/slower machine would derive a different key from the same
 * password. */
static void pex_derive_key_fixed(
    const char *password, const unsigned char salt[PEX_SALT_LEN],
    uint32_t mem, uint32_t passes, uint32_t parallel,
    unsigned char key_out[PEX_KEY_LEN])
{
    strbuf *out = strbuf_new_nm();
    ptrlen empty = PTRLEN_LITERAL("");

    argon2(Argon2id, mem, passes, parallel, PEX_KEY_LEN,
          ptrlen_from_asciz(password), make_ptrlen(salt, PEX_SALT_LEN),
          empty, empty, out);

    memcpy(key_out, out->u, PEX_KEY_LEN);
    strbuf_free(out);
}

bool export_sessions(const char *const *names, int nnames,
                     const char *password, const char *destpath,
                     char **errmsg)
{
    strbuf *container;
    int idx;
    FILE *fp;
    bool ok = false;

    *errmsg = NULL;

    if (nnames <= 0) {
        *errmsg = dupstr("No sessions selected to export.");
        return false;
    }

    container = strbuf_new_nm();
    put_uint32(container, (unsigned long)nnames);
    for (idx = 0; idx < nnames; idx++) {
        char *path = session_file_path(names[idx]);
        size_t filelen;
        unsigned char *filedata = pex_read_whole_file(path, &filelen);
        sfree(path);
        if (!filedata) {
            *errmsg = dupprintf("Could not read saved session '%s'.",
                                names[idx]);
            strbuf_free(container);
            return false;
        }
        put_stringz(container, names[idx]);
        put_string(container, filedata, filelen);
        smemclr(filedata, filelen);
        sfree(filedata);
    }

    fp = fopen(destpath, "wb");
    if (!fp) {
        *errmsg = dupprintf("Could not create '%s'.", destpath);
        strbuf_free(container);
        return false;
    }

    if (!password || !*password) {
        fprintf(fp, "PUTTY-EXPORT-2 PLAIN\n");
        if (fwrite(container->u, 1, container->len, fp) == container->len)
            ok = true;
    } else {
        unsigned char salt[PEX_SALT_LEN], iv[PEX_IV_LEN];
        unsigned char key[PEX_KEY_LEN];
        uint32_t mem, passes, parallel;
        size_t padded_len, pad, i;
        unsigned char *buf;
        char *salthex, *ivhex;

        if (!pex_random_bytes(salt, sizeof(salt)) ||
            !pex_random_bytes(iv, sizeof(iv))) {
            *errmsg = dupstr("Could not generate random salt/IV.");
            fclose(fp);
            strbuf_free(container);
            remove(destpath);
            return false;
        }

        pex_derive_key_choosing_passes(password, salt, key,
                                       &mem, &passes, &parallel);

        pad = 16 - (container->len % 16);      /* PKCS#7, always 1..16 */
        padded_len = container->len + pad;
        buf = snewn(padded_len, unsigned char);
        memcpy(buf, container->u, container->len);
        for (i = container->len; i < padded_len; i++)
            buf[i] = (unsigned char)pad;

        aes256_encrypt_pubkey(key, iv, buf, (int)padded_len);

        salthex = pex_bytes_to_hex(salt, PEX_SALT_LEN);
        ivhex = pex_bytes_to_hex(iv, PEX_IV_LEN);
        fprintf(fp, "PUTTY-EXPORT-2 ENC\n");
        fprintf(fp, "mem=%lu passes=%lu parallel=%lu\n",
               (unsigned long)mem, (unsigned long)passes,
               (unsigned long)parallel);
        fprintf(fp, "salt=%s\n", salthex);
        fprintf(fp, "iv=%s\n", ivhex);
        if (fwrite(buf, 1, padded_len, fp) == padded_len)
            ok = true;

        sfree(salthex);
        sfree(ivhex);
        smemclr(buf, padded_len);
        sfree(buf);
        smemclr(key, sizeof(key));
    }

    fclose(fp);
    strbuf_free(container);

    if (!ok) {
        *errmsg = dupprintf("Error writing '%s'.", destpath);
        remove(destpath);
    }
    return ok;
}

PexFileKind export_file_kind(const char *srcpath, char **errmsg)
{
    unsigned char *filebuf;
    size_t filelen, pos = 0;
    char *modeline;
    PexFileKind kind;

    *errmsg = NULL;
    filebuf = pex_read_whole_file(srcpath, &filelen);
    if (!filebuf) {
        *errmsg = dupprintf("Could not read '%s'.", srcpath);
        return PEX_BAD_FILE;
    }

    modeline = pex_read_line(filebuf, filelen, &pos);
    if (!modeline) {
        kind = PEX_BAD_FILE;
    } else if (!strcmp(modeline, "PUTTY-EXPORT-2 PLAIN")) {
        kind = PEX_PLAIN;
    } else if (!strcmp(modeline, "PUTTY-EXPORT-2 ENC")) {
        kind = PEX_ENCRYPTED;
    } else {
        kind = PEX_BAD_FILE;
    }
    if (kind == PEX_BAD_FILE && !*errmsg)
        *errmsg = dupstr("Not a recognised PuTTY session export file.");

    sfree(modeline);
    sfree(filebuf);
    return kind;
}

/* One parsed-but-not-yet-written session from an import bundle. */
struct pex_pending_session {
    char *name;
    unsigned char *data;
    size_t datalen;
};

static void pex_pending_free(struct pex_pending_session *pending, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        sfree(pending[i].name);
        if (pending[i].data) {
            smemclr(pending[i].data, pending[i].datalen);
            sfree(pending[i].data);
        }
    }
    sfree(pending);
}

/* Parses a (already decrypted, if applicable) container into an array
 * of pending sessions, without writing anything to disk yet -- lets
 * the caller check for name collisions and ask for confirmation
 * before anything is overwritten. */
static bool pex_parse_container(const unsigned char *data, size_t len,
                                struct pex_pending_session **out,
                                int *nout, char **errmsg)
{
    BinarySource src[1];
    unsigned long n, i;
    struct pex_pending_session *pending;

    BinarySource_BARE_INIT(src, data, len);
    n = get_uint32(src);
    if (get_err(src) || n > 100000) {   /* sanity cap, not a real limit */
        *errmsg = dupstr("Corrupted export file (bad session count).");
        return false;
    }

    pending = snewn(n, struct pex_pending_session);
    for (i = 0; i < n; i++) {
        ptrlen name = get_string(src);
        ptrlen filedata = get_string(src);
        if (get_err(src)) {
            *errmsg = dupstr("Corrupted export file (truncated).");
            pex_pending_free(pending, (int)i);
            return false;
        }
        pending[i].name = mkstr(name);
        pending[i].datalen = filedata.len;
        pending[i].data = snewn(filedata.len ? filedata.len : 1,
                                unsigned char);
        if (filedata.len)
            memcpy(pending[i].data, filedata.ptr, filedata.len);
    }

    *out = pending;
    *nout = (int)n;
    return true;
}

static bool pex_decrypt_to_container(
    const unsigned char *filebuf, size_t filelen, size_t pos,
    const char *password, unsigned char **container_out,
    size_t *container_len_out, bool *wrong_password, char **errmsg)
{
    char *paramline, *saltline, *ivline;
    unsigned long mem, passes, parallel;
    unsigned char salt[PEX_SALT_LEN], iv[PEX_IV_LEN];
    unsigned char key[PEX_KEY_LEN];
    size_t ctlen;
    unsigned char *ct;
    unsigned char pad;

    paramline = pex_read_line(filebuf, filelen, &pos);
    saltline = paramline ? pex_read_line(filebuf, filelen, &pos) : NULL;
    ivline = saltline ? pex_read_line(filebuf, filelen, &pos) : NULL;

    if (!paramline || !saltline || !ivline ||
        sscanf(paramline, "mem=%lu passes=%lu parallel=%lu",
              &mem, &passes, &parallel) != 3 ||
        strncmp(saltline, "salt=", 5) != 0 ||
        strncmp(ivline, "iv=", 3) != 0 ||
        !pex_hex_to_bytes_fixed(saltline + 5, salt, PEX_SALT_LEN) ||
        !pex_hex_to_bytes_fixed(ivline + 3, iv, PEX_IV_LEN)) {
        *errmsg = dupstr("Corrupted export file (bad header).");
        sfree(paramline); sfree(saltline); sfree(ivline);
        return false;
    }

    ctlen = filelen - pos;
    if (ctlen == 0 || ctlen % 16 != 0) {
        *errmsg = dupstr("Corrupted export file (bad length).");
        sfree(paramline); sfree(saltline); sfree(ivline);
        return false;
    }

    pex_derive_key_fixed(password, salt, (uint32_t)mem, (uint32_t)passes,
                         (uint32_t)parallel, key);

    ct = snewn(ctlen, unsigned char);
    memcpy(ct, filebuf + pos, ctlen);
    aes256_decrypt_pubkey(key, iv, ct, (int)ctlen);
    smemclr(key, sizeof(key));
    sfree(paramline); sfree(saltline); sfree(ivline);

    pad = ct[ctlen - 1];
    if (pad < 1 || pad > 16 || pad > ctlen) {
        *wrong_password = true;
        *errmsg = dupstr("Wrong password, or the file is corrupted.");
        smemclr(ct, ctlen);
        sfree(ct);
        return false;
    }

    *container_out = ct;
    *container_len_out = ctlen - pad;
    return true;
}

bool import_sessions(const char *srcpath, const char *password,
                     pex_confirm_overwrite_fn confirm, void *confirm_ctx,
                     int *nimported, bool *wrong_password, char **errmsg)
{
    unsigned char *filebuf;
    size_t filelen, pos = 0;
    char *modeline;
    struct pex_pending_session *pending = NULL;
    int npending = 0, i;
    unsigned char *container = NULL;
    size_t container_len = 0;
    bool container_needs_free = false;
    bool ok;
    const char **collisions;
    int ncollide;

    *errmsg = NULL;
    *nimported = 0;
    *wrong_password = false;

    filebuf = pex_read_whole_file(srcpath, &filelen);
    if (!filebuf) {
        *errmsg = dupprintf("Could not read '%s'.", srcpath);
        return false;
    }

    modeline = pex_read_line(filebuf, filelen, &pos);
    if (!modeline ||
        (strcmp(modeline, "PUTTY-EXPORT-2 PLAIN") != 0 &&
         strcmp(modeline, "PUTTY-EXPORT-2 ENC") != 0)) {
        *errmsg = dupstr("Not a recognised PuTTY session export file.");
        sfree(modeline);
        sfree(filebuf);
        return false;
    }

    if (!strcmp(modeline, "PUTTY-EXPORT-2 PLAIN")) {
        container = filebuf + pos;
        container_len = filelen - pos;
    } else {
        if (!password || !*password) {
            *errmsg = dupstr(
                "This export is password-protected. Enter the password"
                " to import it.");
            sfree(modeline);
            sfree(filebuf);
            return false;
        }
        if (!pex_decrypt_to_container(filebuf, filelen, pos, password,
                                      &container, &container_len,
                                      wrong_password, errmsg)) {
            sfree(modeline);
            sfree(filebuf);
            return false;
        }
        container_needs_free = true;
    }

    ok = pex_parse_container(container, container_len, &pending, &npending,
                             errmsg);
    if (container_needs_free) {
        smemclr(container, container_len);
        sfree(container);
    }
    sfree(modeline);
    sfree(filebuf);
    if (!ok)
        return false;

    /* Check for existing sessions of the same name before writing
     * anything -- caller decides (via `confirm') whether to proceed. */
    collisions = snewn(npending, const char *);
    ncollide = 0;
    for (i = 0; i < npending; i++) {
        char *path = session_file_path(pending[i].name);
        bool exists = (access(path, F_OK) == 0);
        sfree(path);
        if (exists)
            collisions[ncollide++] = pending[i].name;
    }

    if (ncollide > 0 && confirm && !confirm(confirm_ctx, collisions,
                                            ncollide)) {
        sfree(collisions);
        pex_pending_free(pending, npending);
        return false;                 /* declined, *errmsg stays NULL */
    }
    sfree(collisions);

    for (i = 0; i < npending; i++) {
        char *path = session_file_path(pending[i].name);
        FILE *fp = fopen(path, "wb");
        if (!fp) {
            *errmsg = dupprintf("Could not write session '%s'.",
                                pending[i].name);
            sfree(path);
            pex_pending_free(pending, npending);
            return false;
        }
        if (pending[i].datalen &&
            fwrite(pending[i].data, 1, pending[i].datalen, fp) !=
            pending[i].datalen) {
            *errmsg = dupprintf("Error writing session '%s'.",
                                pending[i].name);
            fclose(fp);
            sfree(path);
            pex_pending_free(pending, npending);
            return false;
        }
        fclose(fp);
        sfree(path);
        (*nimported)++;
    }

    pex_pending_free(pending, npending);
    return true;
}
