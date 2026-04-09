function(embed_binary INPUT_PATH INPUT_FILE OUTPUT_OBJ_VAR)
  string(TOLOWER ${OUTPUT_OBJ_VAR} OUTPUT_FILE_NAME)
  # Derive output object file path inside build dir based on OUTPUT_OBJ_VAR
  set(output_obj "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_NAME}.o")

  # Compute the symbol name the same way objcopy does for -I binary:
  # replace every non-alphanumeric character with underscore.
  string(REGEX REPLACE "[^a-zA-Z0-9]" "_" sym_name "${INPUT_FILE}")

  if(APPLE)
    # Apple platforms (macOS, iOS): use assembly with .incbin
    # objcopy ELF format is not available; .incbin works with Clang on all Apple targets.
    # On Apple the C compiler prepends an underscore to symbols, so the C declaration
    # `extern char _binary_foo_start[]` maps to linker symbol `__binary_foo_start`.
    set(asm_file "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE_NAME}.S")

    file(WRITE "${asm_file}"
      "    .section __DATA,__const\n"
      "    .globl __binary_${sym_name}_start\n"
      "    .globl __binary_${sym_name}_end\n"
      "    .p2align 4\n"
      "__binary_${sym_name}_start:\n"
      "    .incbin \"${INPUT_PATH}/${INPUT_FILE}\"\n"
      "__binary_${sym_name}_end:\n"
    )

    # Build assembler flags for cross-compilation (iOS, etc.)
    set(_asm_flags "")
    if(CMAKE_OSX_ARCHITECTURES)
      list(APPEND _asm_flags "-arch" "${CMAKE_OSX_ARCHITECTURES}")
    endif()
    if(CMAKE_OSX_SYSROOT)
      list(APPEND _asm_flags "-isysroot" "${CMAKE_OSX_SYSROOT}")
    endif()
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
      if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        list(APPEND _asm_flags "-mios-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
      else()
        list(APPEND _asm_flags "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
      endif()
    endif()

    add_custom_command(
      OUTPUT ${output_obj}
      COMMAND ${CMAKE_C_COMPILER} ${_asm_flags}
        -c "${asm_file}" -o "${output_obj}"
      WORKING_DIRECTORY ${INPUT_PATH}
      DEPENDS ${INPUT_PATH}/${INPUT_FILE} ${asm_file}
      COMMENT "Embedding ${INPUT_PATH}/${INPUT_FILE} as ${output_obj}"
      VERBATIM
    )
  else()
    # Linux / Android: use objcopy to create ELF object from binary
    # Determine objcopy arch based on ANDROID_ABI or standard CMake variables
    if(ANDROID_ABI)
      # Android-specific ABI detection
      if(${ANDROID_ABI} STREQUAL "armeabi-v7a")
        set(objcopy_arch "arm")
        set(objcopy_bits "32")
      elseif(${ANDROID_ABI} STREQUAL "arm64-v8a")
        set(objcopy_arch "aarch64")
        set(objcopy_bits "64")
      elseif(${ANDROID_ABI} STREQUAL "x86")
        set(objcopy_arch "i386")
        set(objcopy_bits "32")
      elseif(${ANDROID_ABI} STREQUAL "x86_64")
        set(objcopy_arch "x86-64")
        set(objcopy_bits "64")
      else()
        message(FATAL_ERROR "Unsupported ANDROID_ABI: ${ANDROID_ABI}")
      endif()
    else()
      # Generic platform detection using CMAKE_SYSTEM_PROCESSOR
      set(proc ${CMAKE_SYSTEM_PROCESSOR})

      # Normalize processor name variations
      if(proc MATCHES "^(x86_64|AMD64|amd64|x64)$")
        set(objcopy_arch "x86-64")
        set(objcopy_bits "64")
      elseif(proc MATCHES "^(i386|i686|x86|X86)$")
        set(objcopy_arch "i386")
        set(objcopy_bits "32")
      elseif(proc MATCHES "^(aarch64|arm64|ARM64)$")
        set(objcopy_arch "aarch64")
        set(objcopy_bits "64")
      elseif(proc MATCHES "^(arm|armv7|armv7l|armv7-a|armhf)$")
        set(objcopy_arch "arm")
        set(objcopy_bits "32")
      elseif(proc MATCHES "^(riscv64|RISCV64)$")
        set(objcopy_arch "riscv64")
        set(objcopy_bits "64")
      elseif(proc MATCHES "^(riscv32|RISCV32)$")
        set(objcopy_arch "riscv32")
        set(objcopy_bits "32")
      elseif(proc MATCHES "^(powerpc64|ppc64|PPC64)$")
        set(objcopy_arch "powerpc64")
        set(objcopy_bits "64")
      elseif(proc MATCHES "^(powerpc|ppc|PPC)$")
        set(objcopy_arch "powerpc")
        set(objcopy_bits "32")
      elseif(proc MATCHES "^(mips64)$")
        set(objcopy_arch "mips64")
        set(objcopy_bits "64")
      elseif(proc MATCHES "^(mips)$")
        set(objcopy_arch "mips")
        set(objcopy_bits "32")
      else()
        message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}. Please add support for this platform.")
      endif()
    endif()

    # Temporary object file before adding GNU-stack note
    set(temp_obj "${output_obj}.tmp")

    # Create the custom command to embed the file
    add_custom_command(
            OUTPUT ${output_obj}
            # Step 1: Create binary object
            COMMAND ${CMAKE_OBJCOPY}
            -I binary
            -O elf${objcopy_bits}-${objcopy_arch}
            ${INPUT_FILE} ${temp_obj}
            # Step 2: Add GNU-stack note section (mark stack as non-executable)
            COMMAND ${CMAKE_OBJCOPY}
            --add-section .note.GNU-stack=/dev/null
            --set-section-flags .note.GNU-stack=noload,readonly
            ${temp_obj} ${output_obj}
            # Step 3: Clean up temp file
            COMMAND ${CMAKE_COMMAND} -E remove ${temp_obj}
            WORKING_DIRECTORY ${INPUT_PATH}
            DEPENDS ${INPUT_PATH}/${INPUT_FILE}
            COMMENT "Embedding ${INPUT_PATH}/${INPUT_FILE} as ${output_obj}"
            VERBATIM
    )
  endif()

  # Set OUTPUT_OBJ_VAR in parent scope to the generated object file
  set(${OUTPUT_OBJ_VAR} ${output_obj} PARENT_SCOPE)
endfunction()
