#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <ctype.h>

#define CMD_SIZE 2048
#define BUF_SIZE 8192

// Führt Shell-Befehl aus und gibt Ausgabe als String zurück (dynamisch alloziert, muss mit free() freigegeben werden)
char *run_command(const char *cmd) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;

    char *output = malloc(BUF_SIZE);
    size_t len = 0, cap = BUF_SIZE;

    while (fgets(output + len, cap - len, fp)) {
        len = strlen(output);
        if (cap - len < 1024) {
            cap *= 2;
            output = realloc(output, cap);
        }
    }
    pclose(fp);
    return output;
}

// Gibt aktuellen Timestamp als String zurück
char *current_timestamp() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// Entfernt führende und abschließende Whitespaces (in-place)
void trim(char *str) {
    char *start = str;
    while (isspace((unsigned char)*start)) start++;
    if (start != str) memmove(str, start, strlen(start) + 1);

    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) *end-- = '\0';
}

// Case-insensitive Stringvergleich
int strcasecmp_custom(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return tolower((unsigned char)*a) - tolower((unsigned char)*b);
        a++;
        b++;
    }
    return *a - *b;
}

// Sucht nach einem Eintrag mit Namen target in fls-Ausgabe und extrahiert die Inode korrekt
int find_inode_by_name(const char *fls_output, const char *target, char *inode_out, size_t inode_out_size) {
    char *copy = strdup(fls_output);
    char *line = strtok(copy, "\n");
    while (line) {
        char *colon = strchr(line, ':');
        if (colon && *(colon + 1)) {
            char namebuf[128];
            strncpy(namebuf, colon + 1, sizeof(namebuf) - 1);
            namebuf[sizeof(namebuf) - 1] = '\0';
            trim(namebuf);

            if (strcasecmp_custom(namebuf, target) == 0) {
                // Extrahiere die Inode (der Block nach dem letzten Leerzeichen vor dem Doppelpunkt)
                char *pre_colon = colon;
                while (pre_colon > line && !isspace((unsigned char)*(pre_colon - 1))) {
                    pre_colon--;
                }
                while (pre_colon > line && isspace((unsigned char)*(pre_colon - 1))) {
                    pre_colon--;
                }
                size_t inode_len = colon - pre_colon;
                if (inode_len < inode_out_size && inode_len > 0) {
                    strncpy(inode_out, pre_colon, inode_len);
                    inode_out[inode_len] = '\0';
                    free(copy);
                    return 1;
                }
            }
        }
        line = strtok(NULL, "\n");
    }
    free(copy);
    return 0;
}

// Sucht automatisch die Startsektor-Nummer (Offset) der BDP in mmls-Ausgabe
uint64_t find_bdp_offset(const char *image) {
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "mmls \"%s\"", image);
    char *output = run_command(cmd);
    if (!output) {
        fprintf(stderr, "Fehler beim Ausführen von mmls!\n");
        exit(1);
    }

    uint64_t offset = 0;
    char *saveptr1;
    char *line = strtok_r(output, "\n", &saveptr1);
    while (line) {
        if (strstr(line, "Basic data partition")) {
            // Zerlege die Zeile in Tokens (ID  Start  End  Length  Desc ...)
            char *token;
            int col = 0;
            char *saveptr2;
            token = strtok_r(line, " \t", &saveptr2);
            while (token != NULL) {
                col++;
                // In Spalte 3 steht der Offset (Start)
                if (col == 3) {
                    offset = strtoull(token, NULL, 10);
                    break;
                }
                token = strtok_r(NULL, " \t", &saveptr2);
            }
            break;
        }
        line = strtok_r(NULL, "\n", &saveptr1);
    }
    free(output);

    if (offset == 0) {
        fprintf(stderr, "Basic Data Partition Offset konnte nicht automatisch gefunden werden!\n");
        exit(1);
    }
    return offset;
}

// HTML-Header
void write_html_header(FILE *f, const char *expected_user, const char *expected_comp) {
    fprintf(f, "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>WinScope Report</title><style>body{font-family:sans-serif;margin:20px;}pre{background:#eee;padding:10px;border:1px solid #ccc;} .ok{color:green;} .fail{color:red;}</style></head><body>");
    fprintf(f, "<h1>WinScope Report</h1><p>Erstellt: %s</p>", current_timestamp());
    fprintf(f, "<p>Erwarteter Computername: <strong>%s</strong></p>", expected_comp);
    fprintf(f, "<p>Erwarteter Benutzername: <strong>%s</strong></p>\n", expected_user);
}

// HTML-Footer
void write_html_footer(FILE *f) {
    fprintf(f, "</body></html>\n");
}

// RegRipper-Plugins ausführen und Ergebnis ins HTML schreiben
void run_plugin_to_html(FILE *f, const char *hive_path, const char *plugin, const char *section_title, const char *output_dir, const char *expected_value) {
    char hive_full[CMD_SIZE];
    snprintf(hive_full, sizeof(hive_full), "%s/%s", output_dir, hive_path);
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "regripper -r \"%s\" -p %s", hive_full, plugin);
    char *result = run_command(cmd);
    fprintf(f, "<h2>%s (%s)</h2><pre>%s</pre>\n", section_title, plugin, result ? result : "(keine Ausgabe)");

    if (expected_value && result && (strcmp(plugin, "compname") == 0 || strcmp(plugin, "profilelist") == 0)) {
        if (strstr(result, expected_value)) {
            fprintf(f, "<p class='ok'>[✓] %s stimmt mit Erwartung überein.</p>", plugin);
        } else {
            fprintf(f, "<p class='fail'>[✗] %s stimmt NICHT mit Erwartung überein.</p>", plugin);
        }
    }

    free(result);
}

// Weitere Zusatzinfos ins HTML (Partitionstabelle, fsstat, fls)
void run_command_section(FILE *f, const char *image, uint64_t offset) {
    char cmd[CMD_SIZE];

    snprintf(cmd, sizeof(cmd), "mmls \"%s\"", image);
    char *mmls = run_command(cmd);
    fprintf(f, "<h2>Partitionstabelle (mmls)</h2><pre>%s</pre>\n", mmls ? mmls : "(keine Ausgabe)");
    free(mmls);

    snprintf(cmd, sizeof(cmd), "fsstat -o %" PRIu64 " \"%s\"", offset, image);
    char *fsstat = run_command(cmd);
    fprintf(f, "<h2>Filesystem-Statistik (fsstat)</h2><pre>%s</pre>\n", fsstat ? fsstat : "(keine Ausgabe)");
    free(fsstat);

    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\"", offset, image);
    char *fls = run_command(cmd);
    fprintf(f, "<h2>Dateisystemstruktur (fls)</h2><pre>%s</pre>\n", fls ? fls : "(keine Ausgabe)");
    free(fls);
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <image.dd> <expected_user> <expected_comp> <output_dir>\n", argv[0]);
        return 1;
    }

    const char *image = argv[1];
    const char *exp_user = argv[2];
    const char *exp_comp = argv[3];
    const char *out_dir = argv[4];

    char inode_windows[32], inode_system32[32], inode_config[32];
    char inode_system[32], inode_software[32];
    char cmd[CMD_SIZE];
    char *fls_out;

    // Offset wird automatisch bestimmt
    uint64_t offset = find_bdp_offset(image);
    printf("[i] Automatisch ermittelter Offset der Basic Data Partition: %" PRIu64 "\n", offset);

    // 1. Suche "Windows" im Root
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\"", offset, image);
    fls_out = run_command(cmd);
    if (!find_inode_by_name(fls_out, "Windows", inode_windows, sizeof(inode_windows))) {
        fprintf(stderr, "Windows-Verzeichnis nicht gefunden!\n");
        free(fls_out);
        return 1;
    }
    free(fls_out);

    // 2. Suche "System32" in Windows
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\" %s", offset, image, inode_windows);
    fls_out = run_command(cmd);
    if (!find_inode_by_name(fls_out, "System32", inode_system32, sizeof(inode_system32))) {
        fprintf(stderr, "System32-Verzeichnis nicht gefunden!\n");
        free(fls_out);
        return 1;
    }
    free(fls_out);

    // 3. Suche "config" in System32
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\" %s", offset, image, inode_system32);
    fls_out = run_command(cmd);
    if (!find_inode_by_name(fls_out, "config", inode_config, sizeof(inode_config))) {
        fprintf(stderr, "config-Verzeichnis nicht gefunden!\n");
        free(fls_out);
        return 1;
    }
    free(fls_out);

    // 4. Suche "SYSTEM" und "SOFTWARE" in config
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\" %s", offset, image, inode_config);
    fls_out = run_command(cmd);
    if (!find_inode_by_name(fls_out, "SYSTEM", inode_system, sizeof(inode_system))) {
        fprintf(stderr, "SYSTEM-Datei nicht gefunden!\n");
        free(fls_out);
        return 1;
    }
    if (!find_inode_by_name(fls_out, "SOFTWARE", inode_software, sizeof(inode_software))) {
        fprintf(stderr, "SOFTWARE-Datei nicht gefunden!\n");
        free(fls_out);
        return 1;
    }
    free(fls_out);

    snprintf(cmd, sizeof(cmd), "icat -f ntfs -o %" PRIu64 " \"%s\" %s > \"%s/SYSTEM.hive\"", offset, image, inode_system, out_dir);
    system(cmd);
    snprintf(cmd, sizeof(cmd), "icat -f ntfs -o %" PRIu64 " \"%s\" %s > \"%s/SOFTWARE.hive\"", offset, image, inode_software, out_dir);
    system(cmd);

    char html_path[CMD_SIZE];
    snprintf(html_path, sizeof(html_path), "%s/winscope_report.html", out_dir);
    FILE *html = fopen(html_path, "w");
    if (!html) {
        perror("fopen");
        return 1;
    }

    write_html_header(html, exp_user, exp_comp);

    fprintf(html, "<h2>SYSTEM Hive Analyse</h2>");
    run_plugin_to_html(html, "SYSTEM.hive", "compname", "Computername", out_dir, exp_comp);
    run_plugin_to_html(html, "SYSTEM.hive", "usbstor", "USB Historie", out_dir, NULL);
    run_plugin_to_html(html, "SYSTEM.hive", "usbdevices", "USB Geräte", out_dir, NULL);

    fprintf(html, "<h2>SOFTWARE Hive Analyse</h2>");
    run_plugin_to_html(html, "SOFTWARE.hive", "profilelist", "Benutzerprofile", out_dir, exp_user);
    run_plugin_to_html(html, "SOFTWARE.hive", "volinfocache", "VolumeInfoCache", out_dir, NULL);
    run_plugin_to_html(html, "SOFTWARE.hive", "portdev", "Port Devices", out_dir, NULL);

    run_command_section(html, image, offset);
    write_html_footer(html);
    fclose(html);

    printf("\n[+] SYSTEM und SOFTWARE extrahiert und HTML-Report gespeichert unter: %s\n", html_path);
    return 0;
}
