idf_component_register(
    SRCS "hardening.cpp" "main.cpp" "boot.cpp" "card_io.cpp" "pay.cpp" "pay_evm.cpp"
         "pay_tron.cpp" "keccak256.cpp" "eth_addr.cpp" "eth_rlp.cpp"
         "eth_json.cpp" "eth_sig.cpp" "eth_rpc.cpp" "https_post.cpp" "net.cpp" "ui.cpp"
         "ui_hal.cpp" "ui_theme.cpp" "ui_widgets.cpp" "ui_sale.cpp" "ui_admin.cpp"
         "ui_setup.cpp"
         "civil_time.cpp" "tron_tx.cpp" "tron_rpc.cpp"
         "ota.cpp" "provision.cpp" "settings.cpp"
         "logo_img.c" "logo_small.c" "logo_mid.c" "chain_icons.c" "tap_icon.c"
         "fonts/font_inter_14.c" "fonts/font_inter_14_medium.c"
         "fonts/font_pjs_20_medium.c"
         "fonts/font_pjs_20_semibold.c" "fonts/font_pjs_28_semibold.c"
         "fonts/font_pjs_28_light.c" "fonts/font_icons_48.c"
    INCLUDE_DIRS "."
    REQUIRES pn532 driver cryptnox_utils crypto_provider esp32_logger esp32_platform
             secure_channel pn532_nfc_transport
             esp_wifi esp_event esp_netif esp_http_client esp_http_server
             app_update esp_timer
             nvs_flash mbedtls
             json lwip
             arduino-esp32 TFT_eSPI XPT2046_Touchscreen lvgl
)

# LVGL 8 combines LV_PART_*/LV_STATE_* enums with '|', which C++20 flags as
# -Wdeprecated-enum-enum-conversion. Harmless and unavoidable — silence the noise.
# Scope to C++ only: the flag is invalid for the C image sources (cc1 warns).
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)

# BUILD_ID is the first-parent commit count, worked out in the project
# CMakeLists.txt. Only settings.cpp reads it, so only settings.cpp is rebuilt
# when a new commit changes it.
idf_build_get_property(pos_build_id POS_BUILD_ID)
set_source_files_properties(settings.cpp PROPERTIES
    COMPILE_DEFINITIONS "BUILD_ID=${pos_build_id}U")
