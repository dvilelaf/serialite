set(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../../../components)
idf_component_register(SRCS "wifi_ap_stub.c" INCLUDE_DIRS "${REPO_COMPONENTS}/wifi_ap/include")
