/* In the window but at an odd address, which names no word. */
SECTIONS {
  .text 0x0 : { *(.text) }
  .bitbss 0xFD01 : { *(.bitbss) }
}
