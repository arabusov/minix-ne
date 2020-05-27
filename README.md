# MINIX 1.7.5
That was a boring evening when I for the second time tried to
install MINIX on my IBM PS/2 8530-286. And unfortunatelly,
only with version 1.7.5 I could complete the setup program
with pcem (or QEMU?). There were some issues to work with this,
but now after fixing them, it runs almonst fine.

## Fixed Issues
### Disk
Of course MINIX can't understand my compact flash. But relying
on XTIDE BIOS with switching back to the real mode system
works... slow, but works. So, `hd=bios` boot monitor parameter
fixes the issue (or, at least, postpones the actual solution).

### Keyboard with IBM PS/2 Model 30-286
My lovely Model M keyboard was not working
with MINIX (but in pcem everything was fine...).
I found on google groups, that the problem can be caused
by the famous A20 gate. And I succeed to run MINIX in
the real mode... But of course I wanted to run it
in protected mode to have full access to the memory (1MB I have).
So, finally, I fixed the issue (hint: A20 must be controlled
in the PS/2 style, not in AT style, unusual for the Model 30).

## Features
### Dual-screen MINIX
I have a monochrome (IBM 5151-like) monitor, and obviously I want
to use it with my PS/2. But of course it has a VGA output and
VGA controller on board, and it's impossible to switch it off.
But then I want to use two monitors simultaneously! Now it
almost works, but I have a bug with the not-wiped last line
during scrolling.

## Bug list
  1. ~~Not-wiped last line during scrolling.~~
  2. Monochrome display doesn't change the cursor position
## TODO list
  1. Networking
  2. Compact Flash driver
