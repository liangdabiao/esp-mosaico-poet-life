idf_component_register(
    SRCS "main.c" "game.c" "poets_game.c" "generated/poets_data.c"
         "pl_avatar.c" "generated/pl_avatar_data.c"
         "pl_font.c" "pl_text.c" "pl_cjk_glyph_data.c"
         "pl_save.c" "pl_store.c" "pl_sfx.c" "rtttl_player.c"
    INCLUDE_DIRS "." "generated"
    REQUIRES mosaico_game_app mosaico_raylib_fast esp-gsp
             nvs_flash esp-mosaico-bsp esp_codec_dev)

# GSP needs an image to bind the Raylib framebuffer. This generated placeholder
# is embedded in the application; no external game resource partition is used.
set(canvas_png "${CMAKE_CURRENT_BINARY_DIR}/canvas.png")
execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/make_canvas_png.py"
    "${canvas_png}" RESULT_VARIABLE canvas_result)
if(NOT canvas_result EQUAL 0)
    message(FATAL_ERROR "Could not generate the game canvas placeholder")
endif()
set(CANVAS_IMAGE "${canvas_png}")
configure_file("scene.json.in" "${CMAKE_CURRENT_BINARY_DIR}/scene.json" @ONLY)
gsp_add_bundle(${COMPONENT_LIB}
    SCENES "${CMAKE_CURRENT_BINARY_DIR}/scene.json"
    PIXEL_FORMAT rgb565)
