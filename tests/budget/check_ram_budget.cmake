# Reports how much of the RP2350's main RAM a linked ELF uses outside the
# engine block, and fails if that exceeds the platform reserve.
#
#   cmake -DNM=<arm-none-eabi-nm> -DELF=<file> -DRESERVE=<bytes>
#         -P check_ram_budget.cmake
#
# Main RAM runs from 0x20000000 to __HeapLimit. Statics end at __end__; the
# heap gets whatever lies between. The SDK's linker script insists on
# HEAP_MIN bytes of heap, so that counts as platform use too.

set(RAM_ORIGIN 536870912) # 0x20000000
set(HEAP_MIN 2048)        # PICO_HEAP_SIZE default

execute_process(COMMAND ${NM} -S ${ELF}
  OUTPUT_VARIABLE symbols RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "cannot read symbols from ${ELF}")
endif()

function(symbol_address name out)
  if(NOT symbols MATCHES "(^|\n)([0-9a-f]+) [A-Za-z] ${name}\n")
    message(FATAL_ERROR "symbol ${name} not found in ${ELF}")
  endif()
  math(EXPR value "0x${CMAKE_MATCH_2}")
  set(${out} ${value} PARENT_SCOPE)
endfunction()

symbol_address(__end__ statics_end)
symbol_address(__HeapLimit ram_end)
if(NOT symbols MATCHES "(^|\n)[0-9a-f]+ ([0-9a-f]+) [A-Za-z] g_engine_block\n")
  message(FATAL_ERROR "g_engine_block not found in ${ELF}")
endif()
math(EXPR block "0x${CMAKE_MATCH_2}")

math(EXPR main_ram "${ram_end} - ${RAM_ORIGIN}")
math(EXPR platform_statics "${statics_end} - ${RAM_ORIGIN} - ${block}")
math(EXPR platform_use "${platform_statics} + ${HEAP_MIN}")
math(EXPR margin "${RESERVE} - ${platform_use}")

message("main RAM            ${main_ram} bytes")
message("engine block        ${block} bytes")
message("platform statics    ${platform_statics} bytes")
message("minimum heap        ${HEAP_MIN} bytes")
message("platform reserve    ${RESERVE} bytes, ${platform_use} in use, ${margin} left")

math(EXPR expected_block "${main_ram} - ${RESERVE}")
if(NOT block EQUAL expected_block)
  message(FATAL_ERROR
    "engine block is ${block} bytes, expected main RAM minus reserve = ${expected_block}")
endif()
if(margin LESS 0)
  message(FATAL_ERROR "platform use exceeds the reserve by ${margin} bytes")
endif()
