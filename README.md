[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

# WinScope

**WinScope** ist ein forensisches Kommandozeilen-Tool zur automatisierten Metaanalyse von Windows-Disk-Images.
Es sucht die Windows-Partition, extrahiert die Registry-Hives SYSTEM und SOFTWARE, führt ausgewählte RegRipper-Plugins aus,
dokumentiert das Dateisystem mit The Sleuth Kit und erstellt daraus einen HTML-Report.

## Merkmale

- **Automatische Partitionserkennung**
  Alle Partitionen aus `mmls` werden geprüft (GPT und MBR). Hat das Image keine Partitionstabelle, wird Offset 0 verwendet.

- **Direkte Hive-Extraktion**
  Die Inodes von `Windows/System32/config/SYSTEM` und `SOFTWARE` werden per `ifind -n` bestimmt und mit `icat` extrahiert.

- **RegRipper-Analyse mit Abgleich**
  Rechnername (`compname`) und Benutzerprofile (`profilelist`) werden exakt und ohne Beachtung der Groß-/Kleinschreibung mit den Vorgaben verglichen.

- **Nachvollziehbarer HTML-Report**
  Enthält Zeitstempel mit Zeitzone, SHA-256 des Images, Offset, Inodes und Sleuth-Kit-Version. Alle Tool-Ausgaben werden HTML-escaped.

- **Keine Shell**
  Externe Programme werden direkt per `execvp` gestartet. Pfade mit Leerzeichen oder Sonderzeichen sind unproblematisch.

## Voraussetzungen

- Linux oder macOS mit:
  - `gcc` oder `clang`, `make`
  - The Sleuth Kit: `mmls`, `ifind`, `icat`, `fsstat`, `fls`
  - RegRipper mit den Plugins `compname`, `usbstor`, `usbdevices`, `profilelist`, `volinfocache`, `portdev`
  - `sha256sum` oder `shasum`

Standardmäßig wird RegRipper als `regripper` aufgerufen (z. B. Debian/Kali-Paket).
Ein anderer Aufruf lässt sich über `WINSCOPE_RIP` setzen, etwa `WINSCOPE_RIP=rip.pl`.

## Projektstruktur

```
winscope/
├── src/
│   └── winscope.c      # Hauptprogramm
├── docs/               # Sequenzdiagramm
├── Makefile            # Build und Installation
└── README.md
```

## Build & Installation

```sh
make
sudo make install                 # nach /usr/local/bin
make install PREFIX=$HOME/.local  # ohne root
```

## Nutzung

```sh
winscope [--no-hash] <image.dd> <username> <compname> <output_dir>
```

- `<image.dd>`: Windows-RAW-Image (ganze Platte oder einzelne Partition)
- `<username>`: erwarteter Benutzername (Profilordner im SOFTWARE-Hive)
- `<compname>`: erwarteter Rechnername (SYSTEM-Hive)
- `<output_dir>`: Zielverzeichnis, wird bei Bedarf angelegt
- `--no-hash`: SHA-256 des Images nicht berechnen (spart bei großen Images viel Zeit)

## Beispiel

```sh
winscope case01.dd alice CORP-PC01 reports/
```

Erzeugt:

- `reports/SYSTEM.hive`
- `reports/SOFTWARE.hive`
- `reports/winscope_report.html`

## HTML-Report enthält

1. Falldaten: Zeitstempel, Image, SHA-256, Offset, Inodes, Sleuth-Kit-Version, Vorgaben
2. SYSTEM- und SOFTWARE-Analyse: Ausgabe der RegRipper-Plugins mit Bewertung
3. Dateisysteminformationen: Ausgabe von `mmls`, `fsstat` und `fls`

## Hinweise

- Das Tool bricht mit Exit-Code 1 ab, wenn keine Partition beide Hives enthält oder die Extraktion fehlschlägt.
- Schlägt ein einzelnes RegRipper-Plugin oder Sleuth-Kit-Tool fehl, steht der Exit-Code im Report.
- Beim Abgleich zählt beim Benutzernamen nur der letzte Teil von `ProfileImagePath`, `alice` passt also nicht auf `alice2`.

## Lizenz

Dieses Projekt steht unter der [MIT License](LICENSE) © 2025 [yalmn](https://github.com/yalmn/)
