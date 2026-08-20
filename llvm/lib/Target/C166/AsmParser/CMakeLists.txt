add_llvm_component_library(LLVMC166AsmParser
  C166AsmParser.cpp

  LINK_COMPONENTS
  MC
  MCParser
  C166Desc
  C166Info
  Support

  ADD_TO_COMPONENT
  C166
  )
