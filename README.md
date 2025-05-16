# WinScope

**WinScope** ist ein Kommandozeilen-Tool zur forensischen Metaanalyse von Windows-Disk-Images. Das Tool extrahiert Windows-Registry-Hives, führt RegRipper-Plugins aus, sammelt Systeminformationen mit Sleuth Kit und erstellt einen ausführlichen HTML-Report inklusive Zeitstempel und Evaluierung.

## Merkmale

- **Hive-Extraktion**: Liest SYSTEM- und SOFTWARE-Hive direkt aus dem Image via `icat`.
- **RegRipper-Analyse**: Führt ausgewählte Plugins aus (`compname`, `usbtor`, `usbdevices`, `profillist`, `volinfocache`, `portdev`).
- **Sleuth Kit-Systeminfos**: Verwendet `mmls`, `fsstat` und `fls` zur Dateisystem-Analyse.
- **HTML-Report**: Generiert einen übersichtlichen Report mit Zeitstempel, Hive-Resultaten, Systeminfos und einer Evaluierung, ob Rechnername und Benutzername mit den Erwartungen übereinstimmen.

## Voraussetzungen

- Linux-Umgebung mit folgendem Software-Stack:
  - `gcc` (GNU C Compiler)
  - `icat`, `mmls`, `fsstat`, `fls` (The Sleuth Kit)
  - Perl mit RegRipper (`rip.pl`) und die Plugins:
    - `compname`, `usbtor`, `usbdevices` für SYSTEM-Hive
    - `profillist`, `volinfocache`, `portdev` für SOFTWARE-Hive

## Verzeichnisstruktur

```
winscope/
├── src/
│   └── winscope.c     # Quellcode
├── Makefile           # Build- und Clean-Regeln
├── install.sh         # Installationsskript
└── README.md          # Projektbeschreibung
```

## Build & Installation

1. **Kompilieren**
   ```bash
   cd winscope
   make all
   ```
2. **Installation** (optional, installiert `winscope` nach `/usr/local/bin`)
   ```bash
   sudo ./install.sh
   ```

## Nutzung

```bash
Usage: winscope <image.dd> <compname> <username> <output_dir>
```

- `<image.dd>`: Pfad zum Windows-Disk-Image (RAW/`.dd`).
- `<compname>`: Erwarteter Rechnername (Registry-Value aus SYSTEM-Hive).
- `<username>`: Erwarteter Benutzername (aus SOFTWARE-Hive).
- `<output_dir>`: Existierendes Verzeichnis, in dem der Report abgelegt wird.

### Beispiel

```bash
./winscope case01.dd CORP-PC01 alice /home/user/reports
```

- Erzeugt `/home/user/reports/winscope_report.html`
- Der Report enthält:
  - **Erstellt am:** Zeitstempel
  - **1. Hive-Analyse:** Ergebnisse der RegRipper-Plugins
  - **2. Systeminfos:** Ausgabe von `mmls`, `fsstat` und `fls`
  - **3. Evaluierung:** Abgleich von Rechner- und Benutzernamen
