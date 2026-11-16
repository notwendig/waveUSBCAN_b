# waveUSBCAN_b

[English](README.md) · [Sicherheit](docs/SAFETY.md)

Experimenteller Linux-DKMS-Treiber für USB-CAN-B-/CANalyst-II-kompatible Adapter mit USB-ID `04d8:0053`. Er stellt zwei Kanäle als normale SocketCAN-Netzwerkschnittstellen bereit.

## Installation und Tests

Die [englische Anleitung](README.md) enthält getrennte Schritte für Fedora sowie Debian/Ubuntu, Kernel-Abhängigkeiten und Regressionstests. DKMS-Installation und echte CAN-Tests nur auf einem geeigneten Laborrechner durchführen. Vor einem Hardwaretest Adapter-ID, Kernel-Version, Terminierung und Kanalzuordnung kontrollieren.

## Sicherheit und Projektstand

Der Treiber ist ein Prototyp und kein validiertes Bauteil für sicherheitskritische Systeme. Für R-Net- oder Rollstuhlbusse gelten die strengeren Regeln in [docs/SAFETY.md](docs/SAFETY.md). Lizenz und Herkunft stehen in [LICENSE](LICENSE) und [AUTHORS.md](AUTHORS.md).
