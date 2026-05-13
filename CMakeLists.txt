if(CONFIG_ZMK_CONDITIONAL_UNDERGLOW)
    target_sources(app PRIVATE
        src/conditional_underglow.c
        src/cu_state_sync.c
    )
endif()
