add_executable(test_json test_json.c)
target_link_libraries(test_json PRIVATE poe_json)
target_compile_options(test_json PRIVATE -Wall -Wextra -Werror -pedantic)

add_test(NAME test_json COMMAND test_json)
