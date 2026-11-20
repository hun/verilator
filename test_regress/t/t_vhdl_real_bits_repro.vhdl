-- DESCRIPTION: Verilator: N30 real precision is implementation-defined (GHDL 31, native 30)
--
-- Reproducer, not a suite test: the finding is a value difference no test can pin as right. Lint it by hand with
--   bin/verilator --language vhdl --lint-only --top-module <top> <file>
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.math_real.all;

entity t is
  port (o : out integer);
end entity t;

architecture rtl of t is
  -- GHDL holds 2147483647 as 2^31 and answers 31; a double answers 30
  constant bits_c : integer := integer(log2(real(integer'high)));
begin
  o <= bits_c;
end architecture rtl;
