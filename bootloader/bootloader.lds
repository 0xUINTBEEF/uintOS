OUTPUT(uintOS_bootloader);

SECTIONS
{
  .startup_code 0x7c00 :
  {
    *(.text)
  }
  .runtime_data :
  {
    *(.data)
  }
}
