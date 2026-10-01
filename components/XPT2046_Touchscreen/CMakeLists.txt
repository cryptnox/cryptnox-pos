# ESP-IDF wrapper component for the XPT2046_Touchscreen git submodule.
# Upstream sources live untouched in XPT2046_Touchscreen/ (pinned via
# the gitlink).
idf_component_register(
    SRCS         "XPT2046_Touchscreen/XPT2046_Touchscreen.cpp"
    INCLUDE_DIRS "XPT2046_Touchscreen"
    PRIV_REQUIRES arduino-esp32
)
