set(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../../../components)
idf_component_register(SRCS "usb_console_stub.c" INCLUDE_DIRS "${REPO_COMPONENTS}/usb_console/include")
