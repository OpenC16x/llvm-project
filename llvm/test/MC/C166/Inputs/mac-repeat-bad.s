;; The counts that cannot be written, and the forms that cannot be repeated.
;; Read by mac-repeat.s; not a test on its own, so it has no RUN line.
        repeat 0 times comac r2, [r3+]
        repeat 1 times comac r2, [r3+]
        repeat 32 times comac r2, [r3+]
        repeat 3 times coload r2, [r3+]
        repeat 3 times comul r2, [r3+]
        repeat 3 times cocmp r2, [r3+]
        repeat 3 times coadd r2, r3
        repeat 3 times coshl #4
