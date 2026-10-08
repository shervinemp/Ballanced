/* Ends the code and data segments on 32-byte boundaries. elf2dol copies each
   segment's size into the DOL header as it is, and Dolphin builds from May to
   August 2026 refuse to boot a DOL whose section sizes are not multiples of 32.

   Link with "-T DolAlign.ld -T rvl.ld", in that order: INSERT moves every
   statement before it, so this script has to come before rvl.ld. */

SECTIONS {
	.dolpad.text : {
		BYTE(0);
		. = ALIGN(32);
	}
} INSERT AFTER .text;

SECTIONS {
	.dolpad.data : {
		BYTE(0);
		. = ALIGN(32);
	}
} INSERT AFTER .sdata;
