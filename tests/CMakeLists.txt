
add_executable(ubip_buffer_test
    main.cpp
    )

target_link_libraries(ubip_buffer_test PUBLIC ubip::buffer)

add_test(NAME ubip_buffer_test COMMAND ubip_buffer_test)