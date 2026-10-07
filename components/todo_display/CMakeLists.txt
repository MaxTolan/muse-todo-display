# ESP-IDF component for the todo display (model + screen).
#
# Not built yet: firmware integration (todo.set_list / todo.show_message
# commands, chat-turn outbox, NVS persistence) comes with the board port.
# The host simulator and tests build these same sources from sim/CMakeLists.txt.
idf_component_register(
    SRCS "todo_model.c" "todo_ui.c" "todo_mascot.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES muse
)
