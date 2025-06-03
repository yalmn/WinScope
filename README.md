[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

# WinScope

**WinScope** ist ein forensisches Kommandozeilen-Tool zur automatisierten Metaanalyse von Windows-Disk-Images.  
Es extrahiert automatisch Registry-Hives, führt gezielte RegRipper-Plugins aus, analysiert das Dateisystem mit Sleuth Kit  
und erstellt einen umfangreichen HTML-Report mit Bewertung erwarteter Artefakte.

## Merkmale

- **Automatische Hive-Suche & Extraktion**  
  Findet SYSTEM- und SOFTWARE-Hives schrittweise per `fls` und extrahiert sie direkt mit `icat`.

- **Intelligente RegRipper-Analyse**  
  Führt spezifische Plugins aus und prüft, ob Rechnername und Benutzername mit Erwartungen übereinstimmen.

- **Sleuth Kit-Systemanalyse**  
  Nutzt `mmls`, `fsstat` und `fls`, um Partitionen und Dateisystemstruktur zu dokumentieren.

- **HTML-Report mit Bewertung**  
  Zeitgestempelter Bericht mit übersichtlicher Darstellung aller Ergebnisse und farblicher Bewertung.

## Voraussetzungen

- Linux-System mit installierter Software:
  - `gcc` (GNU Compiler Collection)
  - `mmls`, `fls`, `fsstat`, `icat` (aus The Sleuth Kit)
  - `perl` + RegRipper (`rip.pl`) mit Plugins:
    - SYSTEM: `compname`, `usbstor`, `usbdevices`
    - SOFTWARE: `profilelist`, `volinfocache`, `portdev`

## Projektstruktur

winscope/  
├── src/  
│   └── winscope.c         # Hauptprogramm in C  
├── Makefile               # Build-Regeln  
├── install.sh             # Optionales Installationsskript  
└── README.md              # Diese Beschreibung  

## Build & Installation

cd winscope
make all # kompiliert winscope
sudo ./install.sh # (optional) installiert nach /usr/local/bin

## Nutzung

winscope <image.dd> <username> <compname> <output_dir>


- `<image.dd>`: Pfad zum Windows-RAW-Image (z. B. `.dd`)
- `<username>`: Erwarteter Benutzername (aus SOFTWARE-Hive)
- `<compname>`: Erwarteter Rechnername (aus SYSTEM-Hive)
- `<output_dir>`: Zielverzeichnis für extrahierte Hives und den HTML-Report

## Beispiel

./winscope case01.dd alice CORP-PC01 reports/


Erzeugt:

- `reports/SYSTEM.hive`
- `reports/SOFTWARE.hive`
- `reports/winscope_report.html`

## HTML-Report enthält

1. Hive-Analyse: Ausgabe der RegRipper-Plugins  
2. Dateisysteminformationen: Ausgabe von `mmls`, `fsstat`, `fls`  
3. Evaluierung: Abgleich von Rechner- und Benutzernamen mit Vorgaben

## Hinweise

- Der Offset der Basic Data Partition wird automatisch über `mmls` bestimmt.
- Die Registry-Dateien werden rekursiv über folgende Pfade gesucht:  
  Windows → System32 → config → SYSTEM, SOFTWARE
- Die Plugin-Ausgabe wird direkt im HTML angezeigt und bewertet.

## Lizenz

Dieses Projekt steht unter der [MIT License](LICENSE) © 2025 [yalmn](https://github.com/yalmn/)
