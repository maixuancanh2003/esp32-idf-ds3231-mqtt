set(COMPONENT_SRCS main.c ds3231.c i2cdev.c)
set(COMPONENT_ADD_INCLUDEDIRS ".")

register_component()
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")