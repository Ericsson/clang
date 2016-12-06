set(LLVM_LINK_COMPONENTS
  ${LLVM_TARGETS_TO_BUILD}
  asmparser
  support
  mc
  )

add_clang_executable(clang-cmdline-arch-extractor
  ClangCmdlineArchExtractor.cpp
  )

target_link_libraries(clang-cmdline-arch-extractor
  clangTooling
  clangBasic
  clangFrontend
  clangRewriteFrontend
  )

install(TARGETS clang-cmdline-arch-extractor
  RUNTIME DESTINATION bin)
