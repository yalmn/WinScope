#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define CMD_SIZE 2048
#define BUF_SIZE 8192
#define WIN_MOUNT_PREFIX "/c"
#define PATH_SYSTEM_HIVE WIN_MOUNT_PREFIX "/Windows/System32/config/SYSTEM"
#define PATH_SOFTWARE_HIVE WIN_MOUNT_PREFIX "/Windows/System32/config/SOFTWARE"

// Führt einen Befehl aus und liefert seine Ausgabe als malloc’ed string zurück
char *run_command(const char *cmd)
{
    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        fprintf(stderr, "popen failed: %s\n", strerror(errno));
        return NULL;
    }
    char *output = malloc(BUF_SIZE);
    size_t capacity = BUF_SIZE;
    size_t len = 0;
    while (fgets(output + len, capacity - len, fp))
    {
        len = strlen(output);
        if (capacity - len < 1024)
        {
            capacity *= 2;
            output = realloc(output, capacity);
        }
    }
    pclose(fp);
    return output;
}

// Gibt einen formatierten Zeitstempel zurück (z.B. "2025-05-16 14:23:01")
char *current_timestamp()
{
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    static char buffer[20];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return buffer;
}

// HTML-Header mit CSS für bessere Formatierung
void write_html_header(FILE *out, const char *title)
{
    fprintf(out,
            "<!DOCTYPE html>\n"
            "<html lang=\"de\">\n"
            "<head>\n"
            "  <meta charset=\"UTF-8\">\n"
            "  <title>%s</title>\n"
            "  <style>\n"
            "    body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }\n"
            "    h1 { color: #333; border-bottom: 2px solid #666; padding-bottom: 5px; }\n"
            "    h2 { color: #444; margin-top: 30px; }\n"
            "    h3 { color: #555; margin-top: 15px; }\n"
            "    pre { background: #fff; padding: 10px; border: 1px solid #ccc; overflow-x: auto; }\n"
            "    .timestamp { font-size: 0.9em; color: #666; margin-bottom: 20px; }\n"
            "    .section { background: #fff; padding: 15px; border-radius: 5px;\n"
            "               box-shadow: 0 2px 4px rgba(0,0,0,0.1); margin-bottom: 20px; }\n"
            "    .eval { display: flex; gap: 20px; }\n"
            "    .eval p { flex: 1; background: #eef; padding: 10px;\n"
            "              border-radius: 3px; text-align: center; }\n"
            "  </style>\n"
            "</head>\n"
            "<body>\n"
            "<h1>%s</h1>\n",
            title, title);
}

// HTML-Footer
void write_html_footer(FILE *out)
{
    fprintf(out, "</body>\n</html>\n");
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "Usage: %s <image.dd> <compname> <username> <output_dir>\n", argv[0]);
        return 1;
    }
    const char *image = argv[1];
    const char *exp_comp = argv[2];
    const char *exp_user = argv[3];
    const char *out_dir = argv[4];

    char cmd[CMD_SIZE];

    // 1) Hive-Extraktion
    if (snprintf(cmd, CMD_SIZE,
                 "icat -r %s %s > %s/SYSTEM.hive",
                 image, PATH_SYSTEM_HIVE, out_dir) >= CMD_SIZE)
    {
        fprintf(stderr, "ERROR: SYSTEM hive extraction command too long\n");
        return 1;
    }
    system(cmd);

    if (snprintf(cmd, CMD_SIZE,
                 "icat -r %s %s > %s/SOFTWARE.hive",
                 image, PATH_SOFTWARE_HIVE, out_dir) >= CMD_SIZE)
    {
        fprintf(stderr, "ERROR: SOFTWARE hive extraction command too long\n");
        return 1;
    }
    system(cmd);

    // 2) Regripper auf den extrahierten Hives
    char path_system[CMD_SIZE], path_software[CMD_SIZE];
    snprintf(path_system, CMD_SIZE, "%s/SYSTEM.hive", out_dir);
    snprintf(path_software, CMD_SIZE, "%s/SOFTWARE.hive", out_dir);

    if (snprintf(cmd, CMD_SIZE,
                 "regripper -r %s -p compname,usbtor,usbdevices",
                 path_system) >= CMD_SIZE)
    {
        fprintf(stderr, "ERROR: Regripper SYSTEM command too long\n");
        return 1;
    }
    char *out_ripper_sys = run_command(cmd);

    if (snprintf(cmd, CMD_SIZE,
                 "regripper -r %s -p profillist,volinfocache,portdev",
                 path_software) >= CMD_SIZE)
    {
        fprintf(stderr, "ERROR: Regripper SOFTWARE command too long\n");
        return 1;
    }
    char *out_ripper_soft = run_command(cmd);

    // 3) Sleuth Kit-Systeminfos
    snprintf(cmd, CMD_SIZE, "mmls %s", image);
    char *out_mmls = run_command(cmd);
    snprintf(cmd, CMD_SIZE, "fsstat %s", image);
    char *out_fsstat = run_command(cmd);
    snprintf(cmd, CMD_SIZE, "fls -r %s", image);
    char *out_fls = run_command(cmd);

    // 4) HTML-Report schreiben
    char report_path[CMD_SIZE];
    snprintf(report_path, CMD_SIZE, "%s/winscope_report.html", out_dir);

    FILE *html = fopen(report_path, "w");
    if (!html)
    {
        perror("fopen");
        return 1;
    }

    write_html_header(html, "WinScope Report");
    fprintf(html, "<div class=\"timestamp\">Erstellt am: %s</div>\n", current_timestamp());

    // 1. Hive-Analyse
    fprintf(html, "<div class=\"section\">\n");
    fprintf(html, "  <h2>1. Hive-Analyse</h2>\n");
    fprintf(html, "  <h3>1.1 SYSTEM (%s)</h3>\n", PATH_SYSTEM_HIVE);
    fprintf(html, "  <pre>%s</pre>\n", out_ripper_sys);
    fprintf(html, "  <h3>1.2 SOFTWARE (%s)</h3>\n", PATH_SOFTWARE_HIVE);
    fprintf(html, "  <pre>%s</pre>\n", out_ripper_soft);
    fprintf(html, "</div>\n");

    // 2. Systeminfos
    fprintf(html, "<div class=\"section\">\n");
    fprintf(html, "  <h2>2. Systeminfos</h2>\n");
    fprintf(html, "  <h3>2.1 mmls</h3>\n<pre>%s</pre>\n", out_mmls);
    fprintf(html, "  <h3>2.2 fsstat</h3>\n<pre>%s</pre>\n", out_fsstat);
    fprintf(html, "  <h3>2.3 fls</h3>\n<pre>%s</pre>\n", out_fls);
    fprintf(html, "</div>\n");

    // 3. Evaluierung
    int comp_ok = (strstr(out_ripper_sys, exp_comp) != NULL);
    int user_ok = (strstr(out_ripper_soft, exp_user) != NULL);
    fprintf(html, "<div class=\"section eval\">\n");
    fprintf(html, "  <p><strong>Rechner:</strong> %s – %s</p>\n",
            exp_comp, comp_ok ? "✔️ korrekt" : "❌ falsch");
    fprintf(html, "  <p><strong>User:</strong>    %s – %s</p>\n",
            exp_user, user_ok ? "✔️ korrekt" : "❌ falsch");
    fprintf(html, "</div>\n");

    write_html_footer(html);
    fclose(html);

    // Aufräumen
    free(out_ripper_sys);
    free(out_ripper_soft);
    free(out_mmls);
    free(out_fsstat);
    free(out_fls);

    printf("Report gespeichert in: %s\n", report_path);
    return 0;
}
