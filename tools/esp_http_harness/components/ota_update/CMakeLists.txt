set(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../../../components)
idf_component_register(SRCS "ota_update_stub.c" "${REPO_COMPONENTS}/ota_update/src/ota_update_policy.c" INCLUDE_DIRS "${REPO_COMPONENTS}/ota_update/include")
