// Unit-Tests für die reinen Funktionen von WinScope. Aufruf: make test
#define WINSCOPE_TEST
#include "../src/winscope.c"

static int checks, failures;

#define CHECK(cond)                                                              \
    do {                                                                         \
        checks++;                                                                \
        if (!(cond)) {                                                           \
            failures++;                                                          \
            fprintf(stderr, "FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                        \
    } while (0)

// mmls-Ausgabe eines Windows-11-Testimages (GPT, vier Partitionen)
static const char *MMLS =
    "GUID Partition Table (EFI)\n"
    "Offset Sector: 0\n"
    "Units are in 512-byte sectors\n"
    "\n"
    "      Slot      Start        End          Length       Description\n"
    "000:  Meta      0000000000   0000000000   0000000001   Safety Table\n"
    "001:  -------   0000000000   0000002047   0000002048   Unallocated\n"
    "002:  Meta      0000000001   0000000001   0000000001   GPT Header\n"
    "003:  Meta      0000000002   0000000033   0000000032   Partition Table\n"
    "004:  000       0000002048   0000206847   0000204800   Basic data partition\n"
    "005:  001       0000206848   0000239615   0000032768   Microsoft reserved partition\n"
    "006:  002       0000239616   0166420479   0166180864   Basic data partition\n"
    "007:  003       0166420480   0167768063   0001347584   \n"
    "008:  -------   0167768064   0167772159   0000004096   Unallocated\n";

static void test_partitions(void) {
    uint64_t starts[MAX_PARTS];
    size_t n = parse_partitions(MMLS, starts, MAX_PARTS);
    CHECK(n == 4);
    CHECK(n == 4 && starts[0] == 2048 && starts[1] == 206848 && starts[2] == 239616 && starts[3] == 166420480);
    CHECK(parse_partitions("", starts, MAX_PARTS) == 0);
    CHECK(parse_partitions("kaputt\n:\n000: x\n", starts, MAX_PARTS) == 0);
    CHECK(parse_partitions(MMLS, starts, 2) == 2); // Obergrenze wird eingehalten
}

static void test_value_matches(void) {
    const char *compname = "compname v.20090727\n\nComputerName    = DESKTOP-G2LNLED\nTCP/IP Hostname = DESKTOP-G2LNLED\n";
    CHECK(value_matches(compname, "ComputerName", '=', 0, "desktop-g2lnled"));
    CHECK(!value_matches(compname, "ComputerName", '=', 0, "DESKTOP"));

    const char *profiles = "Path      : %systemroot%\\system32\\config\\systemprofile\n"
                           "SID       : S-1-5-18\n"
                           "Path      : C:\\Users\\ich\n"
                           "Path      : C:\\Users\\J\xc3\xbcrgen\n"; // Jürgen
    CHECK(value_matches(profiles, "Path", ':', 1, "ich"));
    CHECK(value_matches(profiles, "Path", ':', 1, "ICH"));
    CHECK(!value_matches(profiles, "Path", ':', 1, "ic"));
    CHECK(value_matches(profiles, "Path", ':', 1, "J\xc3\x9cRGEN")); // JÜRGEN
    CHECK(!value_matches(profiles, "Path", ':', 1, "Jurgen"));
}

static void test_casecmp(void) {
    CHECK(utf8_casecmp("Ärger", "\xc3\xa4rger") == 0);
    CHECK(utf8_casecmp("abc", "ABC") == 0);
    CHECK(utf8_casecmp("abc", "abcd") != 0);
    CHECK(utf8_casecmp("", "") == 0);
    CHECK(utf8_casecmp("\xc3", "\xc3") == 0); // abgeschnittenes UTF-8 ohne Überlesen
    CHECK(utf8_casecmp("a\xc3\x97", "a\xc3\xb7") != 0); // × ist kein Großbuchstabe von ÷
}

static void test_hive_state(void) {
    char path[] = "/tmp/winscope-hive-XXXXXX", out[LINE_SIZE];
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    unsigned char clean[12] = {'r', 'e', 'g', 'f', 7, 1, 0, 0, 7, 1, 0, 0};
    CHECK(write(fd, clean, sizeof(clean)) == (ssize_t)sizeof(clean));
    close(fd);
    hive_state(path, out, sizeof(out));
    CHECK(strcmp(out, "sauber (Sequenz 263)") == 0);

    unsigned char dirty[12] = {'r', 'e', 'g', 'f', 8, 1, 0, 0, 7, 1, 0, 0};
    fd = open(path, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, dirty, sizeof(dirty)) == (ssize_t)sizeof(dirty));
    close(fd);
    hive_state(path, out, sizeof(out));
    CHECK(strncmp(out, "unsauber (Sequenz 264 / 263)", 28) == 0);

    fd = open(path, O_WRONLY | O_TRUNC);
    CHECK(fd >= 0 && write(fd, "MZ", 2) == 2);
    close(fd);
    hive_state(path, out, sizeof(out));
    CHECK(strcmp(out, "keine gültige Hive-Signatur") == 0);
    hive_state("/gibt/es/nicht", out, sizeof(out));
    CHECK(strcmp(out, "keine gültige Hive-Signatur") == 0);
    unlink(path);
}

static void test_run_to_file_ueberschreibt_nicht(void) {
    char path[] = "/tmp/winscope-out-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0 && write(fd, "alt", 3) == 3);
    close(fd);
    char *argv[] = {"echo", "neu", NULL};
    CHECK(run_to_file(argv, path) == -1);
    char buf[8] = {0};
    FILE *f = fopen(path, "r");
    CHECK(f && fread(buf, 1, sizeof(buf) - 1, f) == 3 && strcmp(buf, "alt") == 0);
    if (f) fclose(f);
    unlink(path);
    CHECK(run_to_file(argv, path) == 0);
    unlink(path);
}

static void test_helpers(void) {
    char s[] = "  \t wert \r\n";
    CHECK(strcmp(trim(s), "wert") == 0);
    CHECK(is_number("239616") && !is_number("") && !is_number("12a"));
    const char *p = "eins\nzwei";
    char line[8];
    CHECK(next_line(&p, line, sizeof(line)) && strcmp(line, "eins") == 0);
    CHECK(next_line(&p, line, sizeof(line)) && strcmp(line, "zwei") == 0);
    CHECK(!next_line(&p, line, sizeof(line)));
    p = "sehr lange zeile";
    CHECK(next_line(&p, line, sizeof(line)) && strlen(line) == sizeof(line) - 1);

    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    html_escape(f, "<a href=\"x\">&'</a>");
    fclose(f);
    CHECK(strcmp(buf, "&lt;a href=&quot;x&quot;&gt;&amp;&#39;&lt;/a&gt;") == 0);
    free(buf);
}

static void test_sha256_und_row(void) {
    char path[] = "/tmp/winscope-sha-XXXXXX", out[LINE_SIZE];
    int fd = mkstemp(path);
    CHECK(fd >= 0 && write(fd, "abc", 3) == 3);
    close(fd);
    image_sha256(path, out, sizeof(out));
    CHECK(strcmp(out, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    unlink(path);

    char *buf = NULL;
    size_t len = 0;
    FILE *f = open_memstream(&buf, &len);
    write_row(f, "Hive", "<x>");
    write_row(f, "Leer", "");
    fclose(f);
    CHECK(strcmp(buf, "<tr><th>Hive</th><td>&lt;x&gt;</td></tr>\n<tr><th>Leer</th><td>-</td></tr>\n") == 0);
    free(buf);
}

int main(void) {
    // Nur von main() in winscope.c genutzt; die Adresse zählt als Verwendung.
    (void)&check_dependencies;
    (void)&locate_hives;
    (void)&write_plugin;
    test_partitions();
    test_value_matches();
    test_casecmp();
    test_hive_state();
    test_run_to_file_ueberschreibt_nicht();
    test_helpers();
    test_sha256_und_row();
    printf("Unit-Tests: %d Prüfungen, %d Fehler\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
