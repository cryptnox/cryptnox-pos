# ESP-IDF wrapper component for the TFT_eSPI git submodule.
# Upstream sources live untouched in TFT_eSPI/ (pinned via the gitlink).
idf_component_register(SRCS "TFT_eSPI/TFT_eSPI.cpp"
                    INCLUDE_DIRS "TFT_eSPI"
                    PRIV_REQUIRES arduino-esp32)
