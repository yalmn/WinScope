#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>

#define CMD_SIZE 2048
#define BUF_SIZE 8192

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

char *current_timestamp() {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

uint64_t prompt_for_offset(const char *image) {
    char cmd[CMD_SIZE];
    snprintf(cmd, sizeof(cmd), "mmls \"%s\"", image);
    printf("--- Partitionstabelle ---\n");
    system(cmd);

    char input[64];
    printf("\n[?] Bitte Offset der Basic Data Partition eingeben: ");
    fgets(input, sizeof(input), stdin);
    return strtoull(input, NULL, 10);
}

void prompt_and_run_fls(const char *image, uint64_t offset, char *inode_out, const char *description) {
    char cmd[CMD_SIZE], input[32];
    printf("\n--- %s ---\n", description);
    printf("Befehl: fls -o %" PRIu64 " -f ntfs %s\n", offset, image);
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\"", offset, image);
    system(cmd);
    printf("\n[?] Bitte Inode für '%s' eingeben: ", description);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    strncpy(inode_out, input, 31);
    inode_out[31] = '\0';
}

void prompt_and_run_fls_on_inode(const char *image, uint64_t offset, const char *inode, char *next_inode_out, const char *description) {
    char cmd[CMD_SIZE], input[32];
    printf("\n--- %s ---\n", description);
    printf("Befehl: fls -o %" PRIu64 " -f ntfs %s %s\n", offset, image, inode);
    snprintf(cmd, sizeof(cmd), "fls -o %" PRIu64 " -f ntfs \"%s\" %s", offset, image, inode);
    system(cmd);
    printf("\n[?] Bitte Inode für '%s' eingeben: ", description);
    fgets(input, sizeof(input), stdin);
    input[strcspn(input, "\n")] = 0;
    strncpy(next_inode_out, input, 31);
    next_inode_out[31] = '\0';
}

void write_html_header(FILE *f, const char *expected_user, const char *expected_comp) {
    fprintf(f, "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>WinScope Report</title><style>body{font-family:sans-serif;margin:20px;}pre{background:#eee;padding:10px;border:1px solid #ccc;} .ok{color:green;} .fail{color:red;}</style></head><body>");
    fprintf(f, "<h1>WinScope Report</h1><p>Erstellt: %s</p>", current_timestamp());
    fprintf(f, "<p>Erwarteter Computername: <strong>%s</strong></p>", expected_comp);
    fprintf(f, "<p>Erwarteter Benutzername: <strong>%s</strong></p>\n", expected_user);
}

void write_html_footer(FILE *f) {
    fprintf(f, "</body></html>\n");
}

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

    uint64_t offset = prompt_for_offset(image);

    prompt_and_run_fls(image, offset, inode_windows, "Windows-Verzeichnis");
    prompt_and_run_fls_on_inode(image, offset, inode_windows, inode_system32, "System32-Verzeichnis");
    prompt_and_run_fls_on_inode(image, offset, inode_system32, inode_config, "config-Verzeichnis");
    prompt_and_run_fls_on_inode(image, offset, inode_config, inode_system, "SYSTEM-Datei");
    prompt_and_run_fls_on_inode(image, offset, inode_config, inode_software, "SOFTWARE-Datei");

    char cmd[CMD_SIZE];
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
