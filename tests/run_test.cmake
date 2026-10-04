# Runs one test: feeds tests/NAME.in to "zxgw --batch" and compares the output
# with tests/NAME.out.  Used by ctest (see CMakeLists.txt).

file(MAKE_DIRECTORY ${WORKDIR})
if(PROGRAM)
    # a program that brings its own input (the extension example)
    set(command ${PROGRAM})
    set(input_option)
    set(INPUT ${PROGRAM})
else()
    set(command ${ZXGW} --batch --geometry 32x24)
    set(input_option INPUT_FILE ${INPUT})
endif()
execute_process(
    COMMAND ${command}
    ${input_option}
    OUTPUT_VARIABLE actual
    ERROR_VARIABLE errors
    RESULT_VARIABLE result
    WORKING_DIRECTORY ${WORKDIR}
    TIMEOUT 60)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "zxgw exited with ${result}\n${errors}\n--- output so far ---\n${actual}")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT actual STREQUAL expected)
    get_filename_component(name ${INPUT} NAME_WE)
    file(WRITE ${WORKDIR}/${name}.actual "${actual}")
    # Show the first line that differs, with control characters made visible.
    string(REPLACE "\n" ";" actual_lines "${actual}")
    string(REPLACE "\n" ";" expected_lines "${expected}")
    list(LENGTH actual_lines n_actual)
    list(LENGTH expected_lines n_expected)
    set(i 0)
    set(first_difference "")
    while(i LESS n_actual OR i LESS n_expected)
        set(a "<missing>")
        set(e "<missing>")
        if(i LESS n_actual)
            list(GET actual_lines ${i} a)
        endif()
        if(i LESS n_expected)
            list(GET expected_lines ${i} e)
        endif()
        if(NOT a STREQUAL e)
            math(EXPR line "${i} + 1")
            string(REPLACE "\r" "<CR>" a "${a}")
            string(REPLACE "\t" "<TAB>" a "${a}")
            set(first_difference "line ${line}\n  expected: [${e}]\n  actual:   [${a}]")
            break()
        endif()
        math(EXPR i "${i} + 1")
    endwhile()
    message(FATAL_ERROR "Output differs from ${EXPECTED}\nFirst difference: ${first_difference}\n--- actual (also in ${WORKDIR}/${name}.actual) ---\n${actual}")
endif()
