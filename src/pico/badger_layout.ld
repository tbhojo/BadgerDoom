/* A separately installed WHX starts here; the executable must not erase it. */
ASSERT(__flash_binary_end <= __badger_whx_start, "Badger Doom firmware overlaps the WHX slot")
