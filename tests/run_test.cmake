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
    message(FATAL_ERROR "zxgw exited with ${result}\n${errors}")
endif()

file(READ ${EXPECTED} expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT actual STREQUAL expected)
    get_filename_component(name ${INPUT} NAME_WE)
    file(WRITE ${WORKDIR}/${name}.actual "${actual}")
    message(FATAL_ERROR "Output differs from ${EXPECTED}\n--- actual (also in ${WORKDIR}/${name}.actual) ---\n${actual}")
endif()
