-- DESCRIPTION: Verilator: N30 real precision is implementation-defined (GHDL 31, native 30)
--
-- Reproducer, not a suite test: the finding is a value difference no test can pin as right. Lint it by hand with
--   bin/verilator --language vhdl --lint-only --top-module <top> <file>
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

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
