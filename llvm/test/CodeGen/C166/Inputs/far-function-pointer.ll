; An initialiser holding a far function's address is the same mistake as
; taking it in code, and is caught while the constant is being lowered.
declare i16 @far_callee(i16) "far"
@fp = global ptr @far_callee
