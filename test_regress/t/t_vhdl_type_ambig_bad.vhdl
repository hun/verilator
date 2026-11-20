-- DESCRIPTION: Verilator: VHDL type name imported from two packages
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

package a_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

package b_pkg is
  type pair_t is array (0 to 1) of integer;
end package;

library work;
use work.a_pkg.all;
use work.b_pkg.all;

entity t is
  port (o : out integer);
end;

architecture rtl of t is
  signal x : pair_t;
begin
  o <= 0;
end;
