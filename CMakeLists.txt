if(CONFIG_ZMK_CONDITIONAL_UNDERGLOW AND ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
    target_sources(app PRIVATE src/conditional_underglow.c)
endif()
