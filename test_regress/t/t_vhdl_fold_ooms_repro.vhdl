-- DESCRIPTION: Verilator: N32 a pure call whose actual is not static folds forever (87 GB before the OOM killer)
--
-- Reproducer, not a suite test: the finding is unbounded memory, so running it in the suite would kill the host -- never lint it without a ulimit. Lint it by hand with
--   bin/verilator --language vhdl --vhdl-frontend native --lint-only --top-module <top> <file>
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package p is
  function width_of(v : integer) return integer;
end package p;

package body p is
  function width_of(v : integer) return integer is
    variable i : integer := 0;
  begin
    while 2 ** i < v loop
      i := i + 1;
    end loop;
    return i;
  end function width_of;
end package body p;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.p.all;

entity t is
  port (
    ctrl : in  unsigned(7 downto 0);
    o    : out integer
  );
end entity t;

architecture rtl of t is
  -- The initializer reads a signal, so the constant has no value while the fold runs
  constant d_c : integer := width_of(to_integer(ctrl));
begin
  o <= d_c;
end architecture rtl;
