#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define PATH_SIZE 4096
#define LINE_SIZE 1024
#define NUM_SIZE  32
#define MAX_PARTS 64

#define HIVE_SYSTEM   "/Windows/System32/config/SYSTEM"
#define HIVE_SOFTWARE "/Windows/System32/config/SOFTWARE"

enum check { CHECK_NONE, CHECK_COMPNAME, CHECK_PROFILE };

struct plugin {
    const char *hive;
    const char *name;
    const char *title;
    enum check check;
};

static const struct plugin plugins[] = {
    { "SYSTEM.hive",   "compname",     "Computername",    CHECK_COMPNAME },
    { "SYSTEM.hive",   "usbstor",      "USB Historie",    CHECK_NONE },
    { "SYSTEM.hive",   "usbdevices",   "USB Geräte",      CHECK_NONE },
    { "SOFTWARE.hive", "profilelist",  "Benutzerprofile", CHECK_PROFILE },
    { "SOFTWARE.hive", "volinfocache", "VolumeInfoCache", CHECK_NONE },
    { "SOFTWARE.hive", "portdev",      "Port Devices",    CHECK_NONE },
};

// Startet argv ohne Shell, stdout geht nach out_fd. close_fd wird im Kind geschlossen (-1 = keins).
static pid_t spawn(char *const argv[], int out_fd, int close_fd, int quiet) {
    pid_t pid = fork();
    if (pid != 0) return pid;

    if (close_fd >= 0) close(close_fd);
    if (dup2(out_fd, STDOUT_FILENO) < 0) _exit(127);
    close(out_fd);
    if (quiet) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) dup2(devnull, STDERR_FILENO);
    }
    execvp(argv[0], argv);
    fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
    _exit(127);
}

// Exit-Code des Prozesses, -1 bei Signal oder Fehler
static int wait_exit(pid_t pid) {
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

// Führt argv aus und gibt stdout als String zurück (mit free() freigeben), NULL bei Fehler
static char *run_capture(char *const argv[], int *exit_code, int quiet) {
    int fds[2];
    if (exit_code) *exit_code = -1;
    if (pipe(fds) < 0) return NULL;

    pid_t pid = spawn(argv, fds[1], fds[0], quiet);
    close(fds[1]);
    if (pid < 0) {
        close(fds[0]);
        return NULL;
    }

    size_t len = 0, cap = 8192;
    char *buf = malloc(cap);
    int failed = buf == NULL;
    while (!failed) {
        if (cap - len < 4096) {
            char *tmp = realloc(buf, cap * 2);
            if (!tmp) {
                failed = 1;
                break;
            }
            buf = tmp;
            cap *= 2;
        }
        ssize_t n = read(fds[0], buf + len, cap - len - 1);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) failed = 1;
        if (n <= 0) break;
        len += (size_t)n;
    }
    close(fds[0]);

    int code = wait_exit(pid);
    if (exit_code) *exit_code = code;
    if (failed) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    return buf;
}

// Führt argv aus und schreibt stdout in die Datei path
static int run_to_file(char *const argv[], const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror(path);
        return -1;
    }
    pid_t pid = spawn(argv, fd, -1, 0);
    close(fd);
    return pid < 0 ? -1 : wait_exit(pid);
}

// dir/name nach out, 0 wenn der Pfad nicht passt
static int join_path(char *out, size_t size, const char *dir, const char *name) {
    int n = snprintf(out, size, "%s/%s", dir, name);
    return n > 0 && (size_t)n < size;
}

// Entfernt Leerraum am Anfang und Ende (in-place)
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    size_t len = strlen(s);
    while (len > 0 && strchr(" \t\r\n", s[len - 1])) s[--len] = '\0';
    return s;
}

// Kopiert die nächste Zeile aus *p nach buf, 0 am Ende des Textes
static int next_line(const char **p, char *buf, size_t size) {
    if (!*p || !**p) return 0;
    const char *end = strchr(*p, '\n');
    size_t len = end ? (size_t)(end - *p) : strlen(*p);
    size_t copy = len < size - 1 ? len : size - 1;
    memcpy(buf, *p, copy);
    buf[copy] = '\0';
    *p += end ? len + 1 : len;
    return 1;
}

static int is_number(const char *s) {
    if (!*s) return 0;
    for (; *s; s++) {
        if (*s < '0' || *s > '9') return 0;
    }
    return 1;
}

static void html_escape(FILE *f, const char *s) {
    for (; *s; s++) {
        switch (*s) {
        case '<':  fputs("&lt;", f); break;
        case '>':  fputs("&gt;", f); break;
        case '&':  fputs("&amp;", f); break;
        case '"':  fputs("&quot;", f); break;
        case '\'': fputs("&#39;", f); break;
        default:   fputc(*s, f);
        }
    }
}

// Startsektoren aller belegten Partitionen aus der mmls-Ausgabe (Meta- und Unallocated-Zeilen fallen raus)
static size_t parse_partitions(const char *mmls, uint64_t *starts, size_t max) {
    char line[LINE_SIZE], slot[32], type[32], start[32];
    size_t count = 0;
    const char *p = mmls;

    while (count < max && next_line(&p, line, sizeof(line))) {
        if (sscanf(line, "%31s %31s %31s", slot, type, start) != 3) continue;
        if (slot[strlen(slot) - 1] != ':') continue;
        if (strcmp(type, "Meta") == 0 || type[0] == '-') continue;
        if (!is_number(start)) continue;
        starts[count++] = strtoull(start, NULL, 10);
    }
    return count;
}

// Sucht die Inode zu path per ifind, 1 wenn gefunden
static int find_inode(const char *image, const char *offset, const char *path, char *inode, size_t size) {
    char *argv[] = { "ifind", "-f", "ntfs", "-o", (char *)offset, "-n", (char *)path, (char *)image, NULL };
    int code;
    char *out = run_capture(argv, &code, 1);
    if (!out) return 0;

    char *value = trim(out);
    int found = code == 0 && is_number(value) && strlen(value) < size;
    if (found) strcpy(inode, value);
    free(out);
    return found;
}

// Probiert alle Partitionen (zuletzt Offset 0) und nimmt die erste mit beiden Hives
static int locate_hives(const char *image, const char *mmls, char *offset, size_t offset_size,
                        char *inode_system, char *inode_software, size_t inode_size) {
    uint64_t starts[MAX_PARTS + 1];
    size_t count = mmls ? parse_partitions(mmls, starts, MAX_PARTS) : 0;
    starts[count++] = 0;

    for (size_t i = 0; i < count; i++) {
        snprintf(offset, offset_size, "%" PRIu64, starts[i]);
        if (find_inode(image, offset, HIVE_SYSTEM, inode_system, inode_size) &&
            find_inode(image, offset, HIVE_SOFTWARE, inode_software, inode_size))
            return 1;
    }
    return 0;
}

// Erste Zeile der Ausgabe von argv, leer wenn nicht verfügbar
static void first_line(char *const argv[], char *out, size_t size) {
    int code;
    char *text = run_capture(argv, &code, 1);
    const char *p = text;
    out[0] = '\0';
    if (text && code == 0) next_line(&p, out, size);
    free(text);
    char *t = trim(out);
    memmove(out, t, strlen(t) + 1);
}

static void image_sha256(const char *image, char *out, size_t size) {
    char *sha256sum[] = { "sha256sum", (char *)image, NULL };
    char *shasum[] = { "shasum", "-a", "256", (char *)image, NULL };

    first_line(sha256sum, out, size);
    if (!out[0]) first_line(shasum, out, size);
    char *space = strchr(out, ' ');
    if (space) *space = '\0';
}

// Vergleicht den Wert hinter sep in Zeilen, die mit key beginnen. Bei last_component zählt nur der letzte Pfadteil.
static int value_matches(const char *text, const char *key, char sep, int last_component, const char *expected) {
    char line[LINE_SIZE];
    const char *p = text;
    size_t key_len = strlen(key);

    while (next_line(&p, line, sizeof(line))) {
        char *l = trim(line);
        if (strncasecmp(l, key, key_len) != 0) continue;
        char *value = strchr(l + key_len, sep);
        if (!value) continue;
        value = trim(value + 1);
        if (last_component) {
            char *slash = strrchr(value, '\\');
            if (slash) value = slash + 1;
        }
        if (strcasecmp(value, expected) == 0) return 1;
    }
    return 0;
}

static void write_pre(FILE *f, const char *text, int code) {
    fputs("<pre>", f);
    html_escape(f, text && *text ? text : "(keine Ausgabe)");
    fputs("</pre>\n", f);
    if (code != 0) fprintf(f, "<p class='fail'>Befehl fehlgeschlagen (Exit-Code %d)</p>\n", code);
}

static void write_row(FILE *f, const char *label, const char *value) {
    fprintf(f, "<tr><th>%s</th><td>", label);
    html_escape(f, value && *value ? value : "-");
    fputs("</td></tr>\n", f);
}

static void write_plugin(FILE *f, const struct plugin *pl, const char *out_dir,
                         const char *exp_user, const char *exp_comp) {
    const char *rip = getenv("WINSCOPE_RIP");
    char hive[PATH_SIZE];
    if (!rip || !*rip) rip = "regripper";
    join_path(hive, sizeof(hive), out_dir, pl->hive);

    char *argv[] = { (char *)rip, "-r", hive, "-p", (char *)pl->name, NULL };
    int code;
    char *result = run_capture(argv, &code, 1);

    fprintf(f, "<h3>%s (%s)</h3>\n", pl->title, pl->name);
    write_pre(f, result, code);

    if (pl->check != CHECK_NONE) {
        const char *expected = pl->check == CHECK_COMPNAME ? exp_comp : exp_user;
        int ok = result && (pl->check == CHECK_COMPNAME
                            ? value_matches(result, "ComputerName", '=', 0, expected)
                            : value_matches(result, "Path", ':', 1, expected));
        fprintf(f, "<p class='%s'>[%s] ", ok ? "ok" : "fail", ok ? "✓" : "✗");
        html_escape(f, expected);
        fprintf(f, ok ? " gefunden.</p>\n" : " NICHT gefunden.</p>\n");
    }
    free(result);
}

int main(int argc, char *argv[]) {
    int hash = 1;
    if (argc > 1 && strcmp(argv[1], "--no-hash") == 0) {
        hash = 0;
        argv++;
        argc--;
    }
    if (argc != 5) {
        fprintf(stderr, "Usage: winscope [--no-hash] <image.dd> <expected_user> <expected_comp> <output_dir>\n");
        return 1;
    }

    const char *image = argv[1];
    const char *exp_user = argv[2];
    const char *exp_comp = argv[3];
    const char *out_dir = argv[4];

    if (access(image, R_OK) != 0) {
        perror(image);
        return 1;
    }
    if (mkdir(out_dir, 0755) != 0 && errno != EEXIST) {
        perror(out_dir);
        return 1;
    }

    char system_path[PATH_SIZE], software_path[PATH_SIZE], html_path[PATH_SIZE];
    if (!join_path(system_path, sizeof(system_path), out_dir, "SYSTEM.hive") ||
        !join_path(software_path, sizeof(software_path), out_dir, "SOFTWARE.hive") ||
        !join_path(html_path, sizeof(html_path), out_dir, "winscope_report.html")) {
        fprintf(stderr, "Ausgabepfad zu lang: %s\n", out_dir);
        return 1;
    }

    // mmls wird nur einmal ausgeführt, die Ausgabe dient auch dem Report
    char *mmls_argv[] = { "mmls", (char *)image, NULL };
    int mmls_code;
    char *mmls = run_capture(mmls_argv, &mmls_code, 1);

    char offset[NUM_SIZE], inode_system[NUM_SIZE], inode_software[NUM_SIZE];
    if (!locate_hives(image, mmls_code == 0 ? mmls : NULL, offset, sizeof(offset),
                      inode_system, inode_software, sizeof(inode_system))) {
        fprintf(stderr, "Keine NTFS-Partition mit %s und %s gefunden!\n", HIVE_SYSTEM, HIVE_SOFTWARE);
        free(mmls);
        return 1;
    }
    printf("[i] Windows-Partition bei Offset %s, SYSTEM-Inode %s, SOFTWARE-Inode %s\n",
           offset, inode_system, inode_software);

    char *icat_system[] = { "icat", "-f", "ntfs", "-o", offset, (char *)image, inode_system, NULL };
    char *icat_software[] = { "icat", "-f", "ntfs", "-o", offset, (char *)image, inode_software, NULL };
    struct stat st;
    if (run_to_file(icat_system, system_path) != 0 || stat(system_path, &st) != 0 || st.st_size == 0 ||
        run_to_file(icat_software, software_path) != 0 || stat(software_path, &st) != 0 || st.st_size == 0) {
        fprintf(stderr, "Extraktion der Hives mit icat fehlgeschlagen!\n");
        free(mmls);
        return 1;
    }

    char sha256[LINE_SIZE] = "nicht berechnet (--no-hash)", tsk_version[LINE_SIZE];
    char *version_argv[] = { "mmls", "-V", NULL };
    if (hash) {
        printf("[i] Berechne SHA-256 des Images ...\n");
        image_sha256(image, sha256, sizeof(sha256));
    }
    first_line(version_argv, tsk_version, sizeof(tsk_version));

    char created[64];
    time_t now = time(NULL);
    strftime(created, sizeof(created), "%Y-%m-%d %H:%M:%S %z", localtime(&now));

    FILE *html = fopen(html_path, "w");
    if (!html) {
        perror(html_path);
        free(mmls);
        return 1;
    }

    fputs("<!DOCTYPE html><html><head><meta charset='UTF-8'><title>WinScope Report</title>"
          "<style>body{font-family:sans-serif;margin:20px;}"
          "pre{background:#eee;padding:10px;border:1px solid #ccc;overflow-x:auto;}"
          "th{text-align:left;padding-right:1em;}.ok{color:green;}.fail{color:red;}</style>"
          "</head><body>\n<h1>WinScope Report</h1>\n<table>\n", html);
    write_row(html, "Erstellt", created);
    write_row(html, "Image", image);
    write_row(html, "SHA-256", sha256);
    write_row(html, "Partitions-Offset (Sektoren)", offset);
    write_row(html, "Inode SYSTEM", inode_system);
    write_row(html, "Inode SOFTWARE", inode_software);
    write_row(html, "Sleuth Kit", tsk_version);
    write_row(html, "Erwarteter Computername", exp_comp);
    write_row(html, "Erwarteter Benutzername", exp_user);
    fputs("</table>\n", html);

    const char *current_hive = NULL;
    for (size_t i = 0; i < sizeof(plugins) / sizeof(plugins[0]); i++) {
        if (!current_hive || strcmp(current_hive, plugins[i].hive) != 0) {
            current_hive = plugins[i].hive;
            fprintf(html, "<h2>%.*s Hive Analyse</h2>\n", (int)(strchr(current_hive, '.') - current_hive), current_hive);
        }
        write_plugin(html, &plugins[i], out_dir, exp_user, exp_comp);
    }

    int code;
    fputs("<h2>Partitionstabelle (mmls)</h2>\n", html);
    write_pre(html, mmls, mmls_code);
    free(mmls);

    char *fsstat_argv[] = { "fsstat", "-f", "ntfs", "-o", offset, (char *)image, NULL };
    char *fsstat = run_capture(fsstat_argv, &code, 0);
    fputs("<h2>Filesystem-Statistik (fsstat)</h2>\n", html);
    write_pre(html, fsstat, code);
    free(fsstat);

    char *fls_argv[] = { "fls", "-f", "ntfs", "-o", offset, (char *)image, NULL };
    char *fls = run_capture(fls_argv, &code, 0);
    fputs("<h2>Dateisystemstruktur (fls)</h2>\n", html);
    write_pre(html, fls, code);
    free(fls);

    fputs("</body></html>\n", html);
    if (fclose(html) != 0) {
        perror(html_path);
        return 1;
    }

    printf("\n[+] SYSTEM und SOFTWARE extrahiert und HTML-Report gespeichert unter: %s\n", html_path);
    return 0;
}
