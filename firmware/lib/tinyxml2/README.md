# tinyxml2 (vendored)

Nur `tinyxml2.cpp` + `tinyxml2.h` aus https://github.com/leethomason/tinyxml2
(zlib-Lizenz, Hinweis siehe Dateikopf). Bewusst manuell vendort statt per
`lib_deps`:

- Der PlatformIO-Registry-Fork `sepastian/tinyxml2` lässt sich unter Windows
  nicht installieren (enthält einen kaputten Symlink).
- Ein direkter Git-Checkout des Original-Repos bringt `contrib/` und
  `xmltest.cpp` (Testsuite) mit, die den Flash-Verbrauch spürbar aufblähen
  (+ca. 220 KB) ohne Nutzen fuers Geraet.

Stand: Checkout vom 2026-07-08. Bei Bedarf manuell aktualisieren (nur die
beiden Dateien ersetzen).
