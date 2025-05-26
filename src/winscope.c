#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

#define CMD_SIZE 2048
#define BUF_SIZE 8192

char *run_command(const char *cmd)
{
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

char *current_timestamp()
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

int find_inode(const char *image, const char *target_path, uint64_t offset, char *inode_buf, size_t buf_size)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "fls -r -o %" PRIu64 " %s", offset, image);

    FILE *fp = popen(cmd, "r");
    if (!fp) return 0;

    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, target_path)) {
            char *colon = strchr(line, ':');
            if (colon && colon - line > 5) {
                snprintf(inode_buf, buf_size, "%.*s", (int)(colon - line - 5), line + 5);
                pclose(fp);
                return 1;
            }
        }
    }
    pclose(fp);
    return 0;
}

void write_html_header(FILE *out, const char *title)
{
    fprintf(out,
        "<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"UTF-8\"><title>%s</title>\n"
        "<style>body{font-family:sans-serif;margin:20px;background:#f5f5f5}h1{color:#333}"
        "pre{background:#fff;padding:10px;border:1px solid #ccc;overflow-x:auto}</style></head><body>\n"
        "<h1>%s</h1>\n", title, title);
}

void write_html_footer(FILE *out)
{
    fprintf(out, "</body></html>\n");
}

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr, "Usage: %s <image.dd> <expected_comp> <expected_user> <output_dir>\n", argv[0]);
        return 1;
    }

    const char *image = argv[1];
    const char *exp_comp = argv[2];
    const char *exp_user = argv[3];
    const char *out_dir = argv[4];

    uint64_t offset = 1058816; // Offset der BDP (zuvor mit mmls prüfen)
    char cmd[CMD_SIZE], inode_sys[32], inode_software[32];

    if (!find_inode(image, "Windows/System32/config/SYSTEM", offset, inode_sys, sizeof(inode_sys))) {
        fprintf(stderr, "[!] SYSTEM-Inode nicht gefunden.\n");
        return 1;
    }

    if (!find_inode(image, "Windows/System32/config/SOFTWARE", offset, inode_software, sizeof(inode_software))) {
        fprintf(stderr, "[!] SOFTWARE-Inode nicht gefunden.\n");
        return 1;
    }

    snprintf(cmd, CMD_SIZE, "icat -f ntfs -o %" PRIu64 " %s %s > %s/SYSTEM.hive", offset, image, inode_sys, out_dir);
    system(cmd);

    snprintf(cmd, CMD_SIZE, "icat -f ntfs -o %" PRIu64 " %s %s > %s/SOFTWARE.hive", offset, image, inode_software, out_dir);
    system(cmd);

    char report_path[CMD_SIZE];
    snprintf(report_path, sizeof(report_path), "%s/winscope_report.html", out_dir);
    FILE *html = fopen(report_path, "w");
    if (!html) {
        perror("fopen");
        return 1;
    }

    write_html_header(html, "WinScope Report");
    fprintf(html, "<div>Erstellt am: %s</div>\n", current_timestamp());

    const char *plugins_sys[] = { "compname", "usbtor", "usbdevices" };
    const char *plugins_soft[] = { "profillist", "volinfocache", "portdev" };

    fprintf(html, "<h2>SYSTEM-Analyse</h2>\n");
    for (int i = 0; i < 3; i++) {
        snprintf(cmd, CMD_SIZE, "regripper -r %s/SYSTEM.hive -p %s", out_dir, plugins_sys[i]);
        char *out = run_command(cmd);
        fprintf(html, "<h3>%s</h3><pre>%s</pre>\n", plugins_sys[i], out ? out : "(keine Ausgabe)");
        free(out);
    }

    fprintf(html, "<h2>SOFTWARE-Analyse</h2>\n");
    for (int i = 0; i < 3; i++) {
        snprintf(cmd, CMD_SIZE, "regripper -r %s/SOFTWARE.hive -p %s", out_dir, plugins_soft[i]);
        char *out = run_command(cmd);
        fprintf(html, "<h3>%s</h3><pre>%s</pre>\n", plugins_soft[i], out ? out : "(keine Ausgabe)");
        free(out);
    }

    fprintf(html, "<h2>Evaluierung</h2><pre>");
    snprintf(cmd, CMD_SIZE, "strings %s/SYSTEM.hive", out_dir);
    char *sys_data = run_command(cmd);
    snprintf(cmd, CMD_SIZE, "strings %s/SOFTWARE.hive", out_dir);
    char *soft_data = run_command(cmd);

    int comp_ok = (sys_data && strstr(sys_data, exp_comp));
    int user_ok = (soft_data && strstr(soft_data, exp_user));
    fprintf(html, "Hostname: %s → %s\n", exp_comp, comp_ok ? "OK" : "NICHT GEFUNDEN");
    fprintf(html, "Benutzer: %s → %s\n", exp_user, user_ok ? "OK" : "NICHT GEFUNDEN");
    fprintf(html, "</pre>\n");

    free(sys_data);
    free(soft_data);
    write_html_footer(html);
    fclose(html);

    printf("Report gespeichert unter: %s\n", report_path);
    return 0;
}
