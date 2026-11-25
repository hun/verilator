-- DESCRIPTION: Verilator: ieee.math_real constants and functions at elaboration and run time
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- The expected values were printed by GHDL from the IEEE 1076.2 body. The constants
-- are exact. The functions of GHDL's body are series approximations, off by up to
-- about 1e-8 in arcsin, arccos and arctan, so they are compared within 1e-7.

library ieee;
use ieee.math_real.all;

entity t is
end entity;

architecture test of t is
  constant tol : real := 1.0e-7;
  -- Folded at elaboration, as a constant of a generate or a subtype range would be
  constant sin_c : real := sin(0.3);
  constant log2_c : real := log2(8.0);
  constant pi_c : real := MATH_PI * 2.0;
  constant abs_c : real := abs(-2.5);

  function near(a, b : real) return boolean is
  begin
    return a - b <= tol and b - a <= tol;
  end function;
begin
  process
    variable x : real;
    variable y : real;
  begin
    -- The constants of ieee.math_real at run time
    if not (MATH_E = 2.718281828459045) then report "MATH_E" severity failure; end if;
    if not (MATH_1_OVER_E = 3.6787944117144233e-1) then report "MATH_1_OVER_E" severity failure; end if;
    if not (MATH_PI = 3.141592653589793) then report "MATH_PI" severity failure; end if;
    if not (MATH_2_PI = 6.283185307179586) then report "MATH_2_PI" severity failure; end if;
    if not (MATH_1_OVER_PI = 3.183098861837907e-1) then report "MATH_1_OVER_PI" severity failure; end if;
    if not (MATH_PI_OVER_2 = 1.5707963267948966) then report "MATH_PI_OVER_2" severity failure; end if;
    if not (MATH_PI_OVER_3 = 1.0471975511965979) then report "MATH_PI_OVER_3" severity failure; end if;
    if not (MATH_PI_OVER_4 = 7.853981633974483e-1) then report "MATH_PI_OVER_4" severity failure; end if;
    if not (MATH_3_PI_OVER_2 = 4.71238898038469) then report "MATH_3_PI_OVER_2" severity failure; end if;
    if not (MATH_LOG_OF_2 = 6.931471805599453e-1) then report "MATH_LOG_OF_2" severity failure; end if;
    if not (MATH_LOG_OF_10 = 2.302585092994046) then report "MATH_LOG_OF_10" severity failure; end if;
    if not (MATH_LOG2_OF_E = 1.4426950408889634) then report "MATH_LOG2_OF_E" severity failure; end if;
    if not (MATH_LOG10_OF_E = 4.342944819032518e-1) then report "MATH_LOG10_OF_E" severity failure; end if;
    if not (MATH_SQRT_2 = 1.4142135623730951) then report "MATH_SQRT_2" severity failure; end if;
    if not (MATH_1_OVER_SQRT_2 = 7.071067811865476e-1) then report "MATH_1_OVER_SQRT_2" severity failure; end if;
    if not (MATH_SQRT_PI = 1.772453850905516) then report "MATH_SQRT_PI" severity failure; end if;
    if not (MATH_DEG_TO_RAD = 1.7453292519943295e-2) then report "MATH_DEG_TO_RAD" severity failure; end if;
    if not (MATH_RAD_TO_DEG = 5.729577951308232e1) then report "MATH_RAD_TO_DEG" severity failure; end if;

    -- Folded constants
    if not (near(sin_c, 2.955202041236212e-1)) then report "constant sin" severity failure; end if;
    if not (near(log2_c, 3.0)) then report "constant log2" severity failure; end if;
    if not (near(pi_c, 6.283185307179586)) then report "constant MATH_PI" severity failure; end if;

    -- Run-time functions of a variable argument
    x := 0.3;
    if not (near(sin(x), 2.955202041236212e-1)) then report "sin" severity failure; end if;
    if not (near(cos(x), 9.553364899106145e-1)) then report "cos" severity failure; end if;
    if not (near(tan(x), 3.093362466990781e-1)) then report "tan" severity failure; end if;
    x := 1.5;
    if not (near(exp(x), 4.481689070338064)) then report "exp" severity failure; end if;
    x := 10.0;
    if not (near(log(x), 2.302585092994046)) then report "log" severity failure; end if;
    x := 1000.0;
    if not (near(log10(x), 3.0)) then report "log10" severity failure; end if;
    x := 8.0;
    if not (near(log2(x), 3.0)) then report "log2" severity failure; end if;
    if not (near(log(x, 2.0), 3.0)) then report "log with base" severity failure; end if;
    x := 2.0;
    if not (near(sqrt(x), 1.414213562373095)) then report "sqrt" severity failure; end if;
    x := 1.0e-3;
    if not (near(sqrt(x), 3.162277660168379e-2)) then report "sqrt small" severity failure; end if;
    x := 0.5;
    if not (near(arcsin(x), 5.23598780667292e-1)) then report "arcsin" severity failure; end if;
    if not (near(arccos(x), 1.0471975461276046)) then report "arccos" severity failure; end if;
    if not (near(arctan(x + 0.5), 7.8539815744037e-1)) then report "arctan" severity failure; end if;
    x := 1.0;
    y := -2.0;
    if not (near(arctan(x, y), 2.677945040601743)) then report "arctan of two" severity failure; end if;
    x := 0.7;
    if not (near(sinh(x), 7.585837018395336e-1)) then report "sinh" severity failure; end if;
    if not (near(cosh(x), 1.255169005630943)) then report "cosh" severity failure; end if;
    if not (near(tanh(x), 6.043677771171636e-1)) then report "tanh" severity failure; end if;
    x := 1.5;
    if not (near(arcsinh(x), 1.1947632172871094)) then report "arcsinh" severity failure; end if;
    x := 2.0;
    if not (near(arccosh(x), 1.3169578969248166)) then report "arccosh" severity failure; end if;
    x := 0.5;
    if not (near(arctanh(x), 5.493061443340549e-1)) then report "arctanh" severity failure; end if;

    -- Rounding and sign: exact, with the body's halfway and zero cases
    x := -2.5;
    if not (ceil(x) = -2.0) then report "ceil" severity failure; end if;
    if not (floor(x) = -3.0) then report "floor" severity failure; end if;
    if not (round(x) = -3.0) then report "round negative halfway" severity failure; end if;
    if not (trunc(x) = -2.0) then report "trunc" severity failure; end if;
    x := 2.5;
    if not (round(x) = 3.0) then report "round halfway" severity failure; end if;
    x := 0.49999999999999994;
    if not (round(x) = 1.0) then report "round below halfway" severity failure; end if;
    x := -2.7;
    if not (trunc(x) = -2.0) then report "trunc negative" severity failure; end if;
    if not (sign(x) = -1.0) then report "sign" severity failure; end if;
    x := 0.0;
    if not (sign(x) = 0.0) then report "sign of zero" severity failure; end if;
    x := 1.5;
    y := 2.5;
    if not (realmax(x, y) = 2.5) then report "realmax" severity failure; end if;
    if not (realmin(x, y) = 1.5) then report "realmin" severity failure; end if;
    x := -2.5;
    if not (abs(x) = 2.5) then report "abs" severity failure; end if;
    if not (abs_c = 2.5) then report "constant abs" severity failure; end if;

    report "all math_real values match";
    wait;
  end process;
end architecture;
