set(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../../../components)
idf_component_register(SRCS "terminal_bridge_stub.c" INCLUDE_DIRS "${REPO_COMPONENTS}/terminal_bridge/include" REQUIRES freertos)
