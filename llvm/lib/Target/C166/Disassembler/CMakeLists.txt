add_llvm_component_library(LLVMC166Disassembler
  C166Disassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  C166Info
  Support

  ADD_TO_COMPONENT
  C166
  )
