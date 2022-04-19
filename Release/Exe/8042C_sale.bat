avrdude -p t2313 -c stk500v2 -P avrdoper -U flash:w:8042C.hex:i -U lfuse:w:0xFF:m -U hfuse:w:0xDB:m -U efuse:w:0xFF:m -U lock:w:0x3C:m
pause