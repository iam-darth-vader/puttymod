/*
 * Local patch: detect the remote OS/distro from the cleartext SSH
 * identification banner (sent by the server before any encryption), and
 * store a short icon slug in the session's DistroIcon setting.
 *
 * This does a tiny standalone TCP probe (read the first line) with a short
 * timeout. It touches neither PuTTY's SSH backend nor its network layer, so
 * it can't destabilise a live connection.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <time.h>

#include "putty.h"

#define PROBE_TIMEOUT_SEC 4

/* Banner substring -> icon slug. First match wins, so order matters:
 * more specific / derivative distros must come before their base. */
static const struct { const char *needle; const char *slug; } distro_sig[] = {
    { "astra",      "astra"    },   /* Astra Linux (Debian-based; only fires
                                        if the SSH banner's version string
                                        actually contains "astra") */
    { "raspbian",   "raspbian" },
    { "ubuntu",     "ubuntu"   },
    { "debian",     "debian"   },
    { "freebsd",    "freebsd"  },
    { "rosssh",    "mikrotik" },   /* MikroTik RouterOS */
    { "mikrotik",   "mikrotik" },
    { "for_windows","windows"  },   /* OpenSSH_for_Windows_x.y */
    { "windows",    "windows"  },
    { "dropbear",   "dropbear" },
    { "openwrt",    "openwrt"  },
    { "opnsense",   "opnsense" },
    { "pfsense",    "pfsense"  },
    { "cisco",      "cisco"    },
    { "rocky",      "rocky"    },
    { "almalinux",  "alma"     },
    { "alma",       "alma"     },
    { "centos",     "centos"   },
    { "rhel",       "redhat"   },
    { "red hat",    "redhat"   },
    { "redhat",     "redhat"   },
    { ".el7",       "redhat"   },
    { ".el8",       "redhat"   },
    { ".el9",       "redhat"   },
    { "fedora",     "fedora"   },
    { "suse",       "suse"     },
    { "gentoo",     "gentoo"   },
    { "arch",       "arch"     },
    { "alpine",     "alpine"   },
    { "nixos",      "nixos"    },
    { "manjaro",    "manjaro"  },
    { "void",       "void"     },
    { "oracle",     "oracle"   },
    { "amazon",     "amazon"   },
    { NULL, NULL }
};

static const char *classify_banner(const char *banner)
{
    char *low = dupstr(banner);
    int i;
    const char *slug = NULL;
    for (i = 0; low[i]; i++)
        low[i] = tolower((unsigned char)low[i]);
    for (i = 0; distro_sig[i].needle; i++) {
        if (strstr(low, distro_sig[i].needle)) {
            slug = distro_sig[i].slug;
            break;
        }
    }
    /* Recognised something that speaks SSH but no specific tag -> generic. */
    if (!slug && strstr(low, "ssh-"))
        slug = "linux";
    sfree(low);
    return slug;
}

/* Open TCP, read the server's identification line, classify. Returns a
 * static slug string or NULL. Blocks at most ~PROBE_TIMEOUT_SEC. */
static const char *probe_distro_slug(const char *host, int port)
{
    struct addrinfo hints, *res = NULL, *ai;
    char portstr[16];
    int fd = -1;
    const char *slug = NULL;

    if (!host || !*host)
        return NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    sprintf(portstr, "%d", port > 0 ? port : 22);
    if (getaddrinfo(host, portstr, &hints, &res) != 0)
        return NULL;

    for (ai = res; ai && fd < 0; ai = ai->ai_next) {
        struct timeval tv;
        fd_set wf;
        int flags, err;
        socklen_t elen = sizeof(err);

        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0)
            continue;

        flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0 &&
            errno != EINPROGRESS) {
            close(fd); fd = -1; continue;
        }

        FD_ZERO(&wf); FD_SET(fd, &wf);
        tv.tv_sec = PROBE_TIMEOUT_SEC; tv.tv_usec = 0;
        if (select(fd + 1, NULL, &wf, NULL, &tv) <= 0) {
            close(fd); fd = -1; continue;
        }
        err = 0;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        if (err != 0) {
            close(fd); fd = -1; continue;
        }
    }
    freeaddrinfo(res);
    if (fd < 0)
        return NULL;

    {
        char buf[1024];
        size_t got = 0;
        time_t deadline = time(NULL) + PROBE_TIMEOUT_SEC;

        /* Accumulate until we have a full line containing the SSH id string.
         * Handles slow servers, fragmented reads, and any pre-banner lines
         * a server may legally send before its "SSH-2.0-..." identification. */
        while (got < sizeof(buf) - 1) {
            struct timeval tv;
            fd_set rf;
            time_t now = time(NULL);
            ssize_t n;

            if (now >= deadline)
                break;
            FD_ZERO(&rf); FD_SET(fd, &rf);
            tv.tv_sec = deadline - now; tv.tv_usec = 0;
            if (select(fd + 1, &rf, NULL, NULL, &tv) <= 0)
                break;
            n = read(fd, buf + got, sizeof(buf) - 1 - got);
            if (n <= 0)
                break;
            got += (size_t)n;
            buf[got] = '\0';
            if (strstr(buf, "SSH-") && strchr(buf, '\n'))
                break;
        }
        if (got > 0) {
            buf[got] = '\0';
            slug = classify_banner(buf);
        }
    }
    close(fd);
    return slug;
}

/*
 * Public entry point: probe the host in `conf`, and if we recognise the OS,
 * store the icon slug in `conf` and persist it to the named saved session.
 */
void detect_and_store_distro(const char *sessionname, Conf *conf)
{
    const char *host;
    const char *slug;
    int port;

    if (!sessionname || !*sessionname)
        return;                        /* not a saved session: nowhere to store */
    if (conf_get_int(conf, CONF_protocol) != PROT_SSH)
        return;                        /* only SSH has this banner */

    host = conf_get_str(conf, CONF_host);
    if (host) {
        const char *at = strrchr(host, '@');   /* skip any user@ prefix */
        if (at) host = at + 1;
    }
    port = conf_get_int(conf, CONF_port);

    slug = probe_distro_slug(host, port);
    if (!slug)
        return;

    /* Only rewrite the session if the icon actually changed. */
    if (strcmp(conf_get_str(conf, CONF_distro_icon), slug) != 0) {
        conf_set_str(conf, CONF_distro_icon, slug);
        {
            char *err = save_settings(sessionname, conf);
            sfree(err);
        }
    }
}
