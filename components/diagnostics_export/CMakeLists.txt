idf_component_register(
    SRCS "src/diagnostics_export.c"
    INCLUDE_DIRS "include"
    REQUIRES event_log
    PRIV_REQUIRES json_writer
)
