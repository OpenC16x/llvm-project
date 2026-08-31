/* The bit sections put somewhere that is RAM but is not bit addressable. */
SECTIONS {
  .text 0x0 : { *(.text) }
  .bitbss 0xC000 : { *(.bitbss) }
}
