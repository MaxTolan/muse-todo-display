# ESP-IDF component for the todo display: model, screen and device glue.
# The host simulator and tests build the model and screen from sim/CMakeLists.txt.
#
# WHOLE_ARCHIVE: todo_glue.c registers itself with the SDK (muse_ext.h) from a
# constructor, and nothing else references it.
idf_component_register(
    SRCS "todo_model.c" "todo_ui.c" "todo_mascot.c" "todo_glue.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES muse
    PRIV_REQUIRES nvs_flash esp_netif esp_timer
    WHOLE_ARCHIVE
)
