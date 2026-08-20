add_llvm_component_library(LLVMC166Info
  C166TargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  C166
  )
