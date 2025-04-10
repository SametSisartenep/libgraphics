TEXT _memsetl(SB),1,$0
	CLD
	MOVQ	RARG, DI
	MOVL	c+8(FP), AX
	MOVQ	n+16(FP), CX

	/* if not a multiple of 2, do longs at a time */
	MOVQ	CX, BX
	ANDQ	$1, BX
	JNE	longs

	/* set whole vlongs */
	MOVL	AX, BX
	SHLQ	$32, BX
	ORQ	BX, AX
	SHRQ	$1, CX
	REP;	STOSQ
	RET
longs:
	REP;	STOSL
	RET
