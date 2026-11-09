set(REPO_COMPONENTS ${CMAKE_CURRENT_LIST_DIR}/../../../../components)
# Header only: the harness never serves HTTPS.
idf_component_register(INCLUDE_DIRS "${REPO_COMPONENTS}/local_tls_identity/include" REQUIRES https_fingerprint)
